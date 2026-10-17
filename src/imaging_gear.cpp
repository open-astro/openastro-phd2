/*
 *  imaging_gear.cpp
 *  PHD Guiding
 *
 *  See imaging_gear.h and design/IMAGING_GEAR_LOG.md.
 */

#include "imaging_gear.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace ImagingGear
{
namespace
{
    const char *UNKNOWN = "unknown";

    std::string Fmt(const char *fmt, ...)
    {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    bool Known(const std::optional<double>& v)
    {
        return v.has_value() && std::isfinite(*v);
    }

    // a known, finite value formatted with fmt, else "unknown"
    std::string Num(const std::optional<double>& v, const char *fmt)
    {
        return Known(v) ? Fmt(fmt, *v) : UNKNOWN;
    }

    std::string Int(const std::optional<int>& v, const char *fmt = "%d")
    {
        return v ? Fmt(fmt, *v) : UNKNOWN;
    }

    // text from the server: no line breaks or commas, which delimit the log's records and values
    std::string Clean(std::string s)
    {
        for (char& c : s)
        {
            if ((unsigned char) c < 0x20)
                c = ' ';
            else if (c == ',')
                c = ';';
        }
        return s;
    }

    std::string Str(const std::optional<std::string>& v)
    {
        return v && !v->empty() ? Clean(*v) : UNKNOWN;
    }

    // telescope focal length / aperture: 0 means "left blank", so unknown
    std::optional<double> PositiveMm(const std::optional<double>& metres)
    {
        if (Known(metres) && *metres > 0.0)
            return *metres * 1000.0;
        return std::nullopt;
    }

    std::string SensorTypeName(const std::optional<int>& t)
    {
        static const char *names[] = { "Monochrome", "Color", "RGGB", "CMYG", "CMYG2", "LRGB" };
        if (t && *t >= 0 && *t < (int) (sizeof(names) / sizeof(names[0])))
            return names[*t];
        return UNKNOWN;
    }

    std::string FilterName(const FilterWheel& fw)
    {
        if (fw.position && *fw.position >= 0 && *fw.position < (int) fw.names.size())
            return Clean(fw.names[*fw.position]);
        return UNKNOWN;
    }

    std::string FilterPosition(const FilterWheel& fw)
    {
        if (!fw.position || *fw.position < 0)
            return UNKNOWN;
        if (fw.names.empty())
            return Fmt("%d", *fw.position + 1);
        return Fmt("%d of %d", *fw.position + 1, (int) fw.names.size());
    }

    std::string FocusOffset(const FilterWheel& fw)
    {
        if (fw.position && *fw.position >= 0 && *fw.position < (int) fw.focusOffsets.size())
            return Fmt("%d", fw.focusOffsets[*fw.position]);
        return UNKNOWN;
    }

    std::string FilterValues(const FilterWheel& fw)
    {
        return "Filter = " + FilterName(fw) + ", Position = " + FilterPosition(fw) + ", Focus offset = " + FocusOffset(fw);
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        return s;
    }

    // guide/imaging focal length ratio; unset when either focal length is unknown
    std::optional<double> GuideRatio(const Snapshot& s, double guideFocalLengthMm)
    {
        if (!s.telescope || guideFocalLengthMm <= 0.0)
            return std::nullopt;
        std::optional<double> imagingFocalMm = PositiveMm(s.telescope->focalLengthM);
        if (!imagingFocalMm)
            return std::nullopt;
        return guideFocalLengthMm / *imagingFocalMm;
    }

    bool OffAxisGuider(double guideRatio)
    {
        return std::fabs(guideRatio - 1.0) <= 0.15;
    }

    // set_imaging_gear_devices parameters, also the keys of get_imaging_gear's "overrides"
    struct OverrideField
    {
        const char *name;
        std::optional<int> Overrides::*field;
    };
    const OverrideField OVERRIDE_FIELDS[] = {
        { "camera", &Overrides::camera },       { "filterwheel", &Overrides::filterWheel },
        { "focuser", &Overrides::focuser },     { "rotator", &Overrides::rotator },
        { "telescope", &Overrides::telescope }, { "observingconditions", &Overrides::conditions },
    };

    // JSON values: null for everything the log writes as "unknown"
    const char *JNULL = "null";

    std::string JText(const std::string& s)
    {
        if (s.empty())
            return JNULL;
        std::string out = "\"";
        for (char c : s)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if ((unsigned char) c < 0x20)
                    out += Fmt("\\u%04x", (unsigned char) c);
                else
                    out += c; // UTF-8 passes through
            }
        }
        return out + "\"";
    }

    std::string JText(const std::optional<std::string>& v)
    {
        return v ? JText(*v) : JNULL;
    }

    std::string JNum(const std::optional<double>& v)
    {
        return Known(v) ? Fmt("%.6g", *v) : JNULL;
    }

    std::string JInt(const std::optional<int>& v)
    {
        return v ? Fmt("%d", *v) : JNULL;
    }

    std::string JBool(const std::optional<bool>& v)
    {
        return v ? (*v ? "true" : "false") : JNULL;
    }

    std::string JDevice(int number)
    {
        return number >= 0 ? Fmt("%d", number) : JNULL;
    }

    // the device of this type the guider is connected to, else the only one
    std::optional<int> Pick(const std::vector<ConfiguredDevice>& devices, const char *type, int connected = -1)
    {
        if (connected >= 0)
            return connected;
        std::optional<int> found;
        for (const ConfiguredDevice& d : devices)
        {
            if (Lower(d.type) != type)
                continue;
            if (found)
                return std::nullopt;
            found = d.number;
        }
        return found;
    }

    bool Configured(const std::vector<ConfiguredDevice>& devices, const char *type, int number)
    {
        return std::any_of(devices.begin(), devices.end(),
                           [&](const ConfiguredDevice& d) { return d.number == number && Lower(d.type) == type; });
    }

    // the overridden device if it is configured, else the selection rule
    std::optional<int> Choose(const std::vector<ConfiguredDevice>& devices, const char *type,
                              const std::optional<int>& override, int connected = -1)
    {
        if (override)
            return Configured(devices, type, *override) ? override : std::nullopt;
        return Pick(devices, type, connected);
    }
}

std::string CleanText(std::string s)
{
    return Clean(std::move(s));
}

std::optional<double> PixelScale(const Snapshot& s)
{
    if (!s.telescope || !s.camera)
        return std::nullopt;
    std::optional<double> focalMm = PositiveMm(s.telescope->focalLengthM);
    const Camera& c = *s.camera;
    if (!focalMm || !Known(c.pixelSizeX) || *c.pixelSizeX <= 0.0)
        return std::nullopt;
    int bin = c.binX && *c.binX > 0 ? *c.binX : 1;
    return 206.265 * *c.pixelSizeX * bin / *focalMm;
}

std::string HeaderLines(const Snapshot& s, double guideFocalLengthMm)
{
    if (s.source == Source::NotAlpaca)
        return "Equipment source = not Alpaca\n";
    if (s.source == Source::Unavailable)
        return "Equipment source = unavailable\n";

    std::string out = "Equipment source = " + Clean(s.server) + "\n";

    if (s.telescope)
    {
        const Telescope& t = *s.telescope;
        std::optional<double> imagingFocalMm = PositiveMm(t.focalLengthM);
        std::optional<double> apertureMm = PositiveMm(t.apertureM);
        std::optional<double> ratio;
        if (imagingFocalMm && apertureMm)
            ratio = *imagingFocalMm / *apertureMm;
        out += "Imaging telescope = " + Str(t.name) + ", Focal length = " + Num(imagingFocalMm, "%.0f mm") +
            ", Aperture = " + Num(apertureMm, "%.0f mm") + ", Focal ratio = " + Num(ratio, "f/%.1f") + "\n";
    }

    if (s.camera)
    {
        const Camera& c = *s.camera;
        std::string pixelSize =
            Known(c.pixelSizeX) && Known(c.pixelSizeY) ? Fmt("%.2f x %.2f um", *c.pixelSizeX, *c.pixelSizeY) : UNKNOWN;
        std::string sensorSize = c.sizeX && c.sizeY ? Fmt("%d x %d px", *c.sizeX, *c.sizeY) : UNKNOWN;
        std::string binning = c.binX && c.binY ? Fmt("%dx%d", *c.binX, *c.binY) : UNKNOWN;
        std::string cooler = c.coolerOn ? (*c.coolerOn ? "On" : "Off") : UNKNOWN;
        out += "Imaging camera = " + Str(c.name) + ", Sensor = " + Str(c.sensorName) +
            ", Sensor type = " + SensorTypeName(c.sensorType) + ", Pixel size = " + pixelSize +
            ", Sensor size = " + sensorSize + ", Binning = " + binning +
            ", Pixel scale = " + Num(PixelScale(s), "%.2f arc-sec/px") + ", Gain = " + Int(c.gain) +
            ", Offset = " + Int(c.offset) + ", Readout mode = " + Str(c.readoutMode) + ", Cooler = " + cooler +
            ", Sensor temp = " + Num(c.sensorTemp, "%.1f C") + "\n";
    }
    else if (s.ambiguousCameras > 0)
        out += Fmt("Imaging camera = ambiguous (%d cameras)\n", s.ambiguousCameras);

    if (s.filterWheel)
        out += "Filter wheel = " + Str(s.filterWheel->name) + ", " + FilterValues(*s.filterWheel) + "\n";

    if (s.rotator)
    {
        const Rotator& r = *s.rotator;
        std::string reverse = r.reverse ? (*r.reverse ? "true" : "false") : UNKNOWN;
        out += "Rotator = " + Str(r.name) + ", Position = " + Num(r.position, "%.1f deg") +
            ", Mechanical position = " + Num(r.mechanicalPosition, "%.1f deg") + ", Reverse = " + reverse + "\n";
    }

    if (s.focuser)
    {
        const Focuser& f = *s.focuser;
        out += "Focuser = " + Str(f.name) + ", Position = " + Int(f.position, "%d steps") +
            ", Temperature = " + Num(f.temperature, "%.1f C") + "\n";
    }

    if (s.conditions)
    {
        // ASCOM defines StarFWHM as the seeing, in arc-sec
        const Conditions& oc = *s.conditions;
        out += "Observing conditions = " + Str(oc.name) + ", Seeing = " + Num(oc.starFwhm, "%.1f arc-sec") +
            ", Star FWHM = " + Num(oc.starFwhm, "%.1f arc-sec") + ", Wind speed = " + Num(oc.windSpeed, "%.1f m/s") +
            ", Wind gust = " + Num(oc.windGust, "%.1f m/s") + ", Temperature = " + Num(oc.temperature, "%.1f C") +
            ", Humidity = " + Num(oc.humidity, "%.0f %%") + ", Dew point = " + Num(oc.dewPoint, "%.1f C") +
            ", Pressure = " + Num(oc.pressure, "%.0f hPa") + ", Cloud cover = " + Num(oc.cloudCover, "%.0f %%") +
            ", Sky quality = " + Num(oc.skyQuality, "%.1f mag/arcsec^2") + "\n";
    }

    if (std::optional<double> ratio = GuideRatio(s, guideFocalLengthMm))
    {
        const char *train = OffAxisGuider(*ratio) ? "Off-axis guider" : "Separate guide scope";
        out += Fmt("Guide train = %s, Guide/imaging focal length ratio = %.2f\n", train, *ratio);
    }
    else
        out += "Guide train = unknown, Guide/imaging focal length ratio = unknown\n";

    return out;
}

std::vector<std::string> EquipmentChangeLines(const Snapshot& prev, const Snapshot& cur)
{
    std::vector<std::string> lines;

    // a different device (set_imaging_gear_devices) is not a move
    if (prev.filterWheel && cur.filterWheel && cur.filterWheel->device == prev.filterWheel->device &&
        cur.filterWheel->position && cur.filterWheel->position != prev.filterWheel->position)
        lines.push_back("INFO: EQUIPMENT " + FilterValues(*cur.filterWheel));

    if (prev.rotator && cur.rotator && cur.rotator->device == prev.rotator->device && Known(cur.rotator->position))
    {
        std::string now = Num(cur.rotator->position, "%.1f deg");
        if (now != Num(prev.rotator->position, "%.1f deg"))
            lines.push_back("INFO: EQUIPMENT Rotator position = " + now);
    }

    if (prev.focuser && cur.focuser && cur.focuser->device == prev.focuser->device && cur.focuser->position &&
        cur.focuser->position != prev.focuser->position)
        lines.push_back("INFO: EQUIPMENT Focuser position = " + Int(cur.focuser->position, "%d steps") +
                        ", Temperature = " + Num(cur.focuser->temperature, "%.1f C"));

    return lines;
}

std::string ConditionsLine(const Snapshot& s)
{
    if (!s.conditions)
        return "";
    const Conditions& oc = *s.conditions;
    return "INFO: CONDITIONS Seeing = " + Num(oc.starFwhm, "%.1f arc-sec") + ", Wind speed = " + Num(oc.windSpeed, "%.1f m/s") +
        ", Wind gust = " + Num(oc.windGust, "%.1f m/s") + ", Temperature = " + Num(oc.temperature, "%.1f C") +
        ", Humidity = " + Num(oc.humidity, "%.0f %%");
}

Selection SelectDevices(const std::vector<ConfiguredDevice>& devices, int guideCamera, int connectedTelescope,
                        int connectedRotator, const Overrides& overrides)
{
    Selection sel;
    sel.telescope = Choose(devices, "telescope", overrides.telescope, connectedTelescope);
    sel.rotator = Choose(devices, "rotator", overrides.rotator, connectedRotator);
    sel.filterWheel = Choose(devices, "filterwheel", overrides.filterWheel);
    sel.focuser = Choose(devices, "focuser", overrides.focuser);
    sel.conditions = Choose(devices, "observingconditions", overrides.conditions);
    // the guide camera is never the imaging camera, not even when named
    if (overrides.camera && *overrides.camera != guideCamera)
    {
        if (Configured(devices, "camera", *overrides.camera))
            sel.cameraCandidates.push_back(*overrides.camera);
        return sel;
    }
    for (const ConfiguredDevice& d : devices)
        if (Lower(d.type) == "camera" && d.number != guideCamera)
            sel.cameraCandidates.push_back(d.number);
    return sel;
}

bool SetOverride(Overrides *o, const std::string& name, int device, int guideCamera, std::string *error)
{
    for (const OverrideField& f : OVERRIDE_FIELDS)
    {
        if (name != f.name)
            continue;
        if (device < -1)
        {
            *error = name + ": expected an Alpaca device number, or -1 to clear";
            return false;
        }
        if (f.field == &Overrides::camera && device >= 0 && device == guideCamera)
        {
            *error = Fmt("camera %d is the guide camera, not the imaging camera", device);
            return false;
        }
        o->*f.field = device >= 0 ? std::optional<int>(device) : std::nullopt;
        return true;
    }
    *error = "unknown device type: " + name;
    return false;
}

std::string SnapshotJson(const Snapshot& s, double guideFocalLengthMm, const Overrides& overrides)
{
    // without the server, nothing but the source is reported (as in the log header)
    const bool alpaca = s.source == Source::Alpaca;
    const char *source = alpaca ? "alpaca" : s.source == Source::NotAlpaca ? "not_alpaca" : "unavailable";
    std::string out = std::string("{\"source\":\"") + source + "\",\"server\":" + (alpaca ? JText(s.server) : JNULL);

    out += ",\"telescope\":";
    if (alpaca && s.telescope)
    {
        const Telescope& t = *s.telescope;
        std::optional<double> focalMm = PositiveMm(t.focalLengthM);
        std::optional<double> apertureMm = PositiveMm(t.apertureM);
        std::optional<double> ratio;
        if (focalMm && apertureMm)
            ratio = *focalMm / *apertureMm;
        out += "{\"device\":" + JDevice(t.device) + ",\"name\":" + JText(t.name) + ",\"focal_length_mm\":" + JNum(focalMm) +
            ",\"aperture_mm\":" + JNum(apertureMm) + ",\"focal_ratio\":" + JNum(ratio) + "}";
    }
    else
        out += JNULL;

    out += ",\"camera\":";
    if (alpaca && s.camera)
    {
        const Camera& c = *s.camera;
        std::string sensorType = SensorTypeName(c.sensorType);
        out += "{\"device\":" + JDevice(c.device) + ",\"name\":" + JText(c.name) + ",\"sensor\":" + JText(c.sensorName) +
            ",\"sensor_type\":" + (sensorType == UNKNOWN ? JNULL : JText(sensorType)) +
            ",\"pixel_size_x_um\":" + JNum(c.pixelSizeX) + ",\"pixel_size_y_um\":" + JNum(c.pixelSizeY) +
            ",\"width_px\":" + JInt(c.sizeX) + ",\"height_px\":" + JInt(c.sizeY) + ",\"bin_x\":" + JInt(c.binX) +
            ",\"bin_y\":" + JInt(c.binY) + ",\"pixel_scale_arcsec\":" + JNum(PixelScale(s)) + ",\"gain\":" + JInt(c.gain) +
            ",\"offset\":" + JInt(c.offset) + ",\"readout_mode\":" + JText(c.readoutMode) +
            ",\"cooler_on\":" + JBool(c.coolerOn) + ",\"sensor_temp_c\":" + JNum(c.sensorTemp) + "}";
    }
    else
        out += JNULL;

    out += ",\"filter_wheel\":";
    if (alpaca && s.filterWheel)
    {
        const FilterWheel& fw = *s.filterWheel;
        bool settled = fw.position && *fw.position >= 0;
        std::optional<std::string> filter;
        std::optional<int> position, count, focusOffset;
        if (settled)
        {
            position = *fw.position + 1; // 1-based, as in the log
            if (*fw.position < (int) fw.names.size())
                filter = fw.names[*fw.position];
            if (*fw.position < (int) fw.focusOffsets.size())
                focusOffset = fw.focusOffsets[*fw.position];
        }
        if (!fw.names.empty())
            count = (int) fw.names.size();
        out += "{\"device\":" + JDevice(fw.device) + ",\"name\":" + JText(fw.name) + ",\"filter\":" + JText(filter) +
            ",\"position\":" + JInt(position) + ",\"count\":" + JInt(count) + ",\"focus_offset\":" + JInt(focusOffset) + "}";
    }
    else
        out += JNULL;

    out += ",\"rotator\":";
    if (alpaca && s.rotator)
    {
        const Rotator& r = *s.rotator;
        out += "{\"device\":" + JDevice(r.device) + ",\"name\":" + JText(r.name) + ",\"position_deg\":" + JNum(r.position) +
            ",\"mechanical_position_deg\":" + JNum(r.mechanicalPosition) + ",\"reverse\":" + JBool(r.reverse) + "}";
    }
    else
        out += JNULL;

    out += ",\"focuser\":";
    if (alpaca && s.focuser)
    {
        const Focuser& f = *s.focuser;
        out += "{\"device\":" + JDevice(f.device) + ",\"name\":" + JText(f.name) + ",\"position\":" + JInt(f.position) +
            ",\"temperature_c\":" + JNum(f.temperature) + "}";
    }
    else
        out += JNULL;

    out += ",\"conditions\":";
    if (alpaca && s.conditions)
    {
        // ASCOM defines StarFWHM as the seeing, in arc-sec
        const Conditions& oc = *s.conditions;
        out += "{\"device\":" + JDevice(oc.device) + ",\"name\":" + JText(oc.name) + ",\"seeing_arcsec\":" + JNum(oc.starFwhm) +
            ",\"wind_speed_m_s\":" + JNum(oc.windSpeed) + ",\"wind_gust_m_s\":" + JNum(oc.windGust) +
            ",\"temperature_c\":" + JNum(oc.temperature) + ",\"humidity_pct\":" + JNum(oc.humidity) +
            ",\"dew_point_c\":" + JNum(oc.dewPoint) + ",\"pressure_hpa\":" + JNum(oc.pressure) +
            ",\"cloud_cover_pct\":" + JNum(oc.cloudCover) + ",\"sky_quality_mag_arcsec2\":" + JNum(oc.skyQuality) + "}";
    }
    else
        out += JNULL;

    std::optional<double> guideRatio = alpaca ? GuideRatio(s, guideFocalLengthMm) : std::nullopt;
    std::string trainType = JNULL;
    if (guideRatio)
        trainType = OffAxisGuider(*guideRatio) ? "\"off_axis_guider\"" : "\"separate_guide_scope\"";
    out += ",\"guide_train\":{\"type\":" + trainType + ",\"ratio\":" + JNum(guideRatio) + "}";
    out += Fmt(",\"ambiguous_cameras\":%d", alpaca ? s.ambiguousCameras : 0);
    return out + ",\"overrides\":" + OverridesJson(overrides) + "}";
}

std::string OverridesJson(const Overrides& overrides)
{
    std::string out = "{";
    for (const OverrideField& f : OVERRIDE_FIELDS)
    {
        if (out.size() > 1)
            out += ",";
        out += JText(std::string(f.name)) + ":" + JInt(overrides.*f.field);
    }
    return out + "}";
}
}
