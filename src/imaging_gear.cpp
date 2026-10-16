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

    std::optional<double> imagingFocalMm;
    if (s.telescope)
    {
        const Telescope& t = *s.telescope;
        imagingFocalMm = PositiveMm(t.focalLengthM);
        std::optional<double> apertureMm = PositiveMm(t.apertureM);
        std::optional<double> ratio;
        if (imagingFocalMm && apertureMm)
            ratio = *imagingFocalMm / *apertureMm;
        out += "Imaging telescope = " + Clean(t.name) + ", Focal length = " + Num(imagingFocalMm, "%.0f mm") +
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
        out += "Imaging camera = " + Clean(c.name) + ", Sensor = " + Str(c.sensorName) +
            ", Sensor type = " + SensorTypeName(c.sensorType) + ", Pixel size = " + pixelSize +
            ", Sensor size = " + sensorSize + ", Binning = " + binning +
            ", Pixel scale = " + Num(PixelScale(s), "%.2f arc-sec/px") + ", Gain = " + Int(c.gain) +
            ", Offset = " + Int(c.offset) + ", Readout mode = " + Str(c.readoutMode) + ", Cooler = " + cooler +
            ", Sensor temp = " + Num(c.sensorTemp, "%.1f C") + "\n";
    }
    else if (s.ambiguousCameras > 0)
        out += Fmt("Imaging camera = ambiguous (%d cameras)\n", s.ambiguousCameras);

    if (s.filterWheel)
        out += "Filter wheel = " + Clean(s.filterWheel->name) + ", " + FilterValues(*s.filterWheel) + "\n";

    if (s.rotator)
    {
        const Rotator& r = *s.rotator;
        std::string reverse = r.reverse ? (*r.reverse ? "true" : "false") : UNKNOWN;
        out += "Rotator = " + Clean(r.name) + ", Position = " + Num(r.position, "%.1f deg") +
            ", Mechanical position = " + Num(r.mechanicalPosition, "%.1f deg") + ", Reverse = " + reverse + "\n";
    }

    if (s.focuser)
    {
        const Focuser& f = *s.focuser;
        out += "Focuser = " + Clean(f.name) + ", Position = " + Int(f.position, "%d steps") +
            ", Temperature = " + Num(f.temperature, "%.1f C") + "\n";
    }

    if (s.conditions)
    {
        // ASCOM defines StarFWHM as the seeing, in arc-sec
        const Conditions& oc = *s.conditions;
        out += "Observing conditions = " + Clean(oc.name) + ", Seeing = " + Num(oc.starFwhm, "%.1f arc-sec") +
            ", Star FWHM = " + Num(oc.starFwhm, "%.1f arc-sec") + ", Wind speed = " + Num(oc.windSpeed, "%.1f m/s") +
            ", Wind gust = " + Num(oc.windGust, "%.1f m/s") + ", Temperature = " + Num(oc.temperature, "%.1f C") +
            ", Humidity = " + Num(oc.humidity, "%.0f %%") + ", Dew point = " + Num(oc.dewPoint, "%.1f C") +
            ", Pressure = " + Num(oc.pressure, "%.0f hPa") + ", Cloud cover = " + Num(oc.cloudCover, "%.0f %%") +
            ", Sky quality = " + Num(oc.skyQuality, "%.1f mag/arcsec^2") + "\n";
    }

    if (imagingFocalMm && guideFocalLengthMm > 0.0)
    {
        double ratio = guideFocalLengthMm / *imagingFocalMm;
        const char *train = std::fabs(ratio - 1.0) <= 0.15 ? "Off-axis guider" : "Separate guide scope";
        out += Fmt("Guide train = %s, Guide/imaging focal length ratio = %.2f\n", train, ratio);
    }
    else
        out += "Guide train = unknown, Guide/imaging focal length ratio = unknown\n";

    return out;
}

std::vector<std::string> EquipmentChangeLines(const Snapshot& prev, const Snapshot& cur)
{
    std::vector<std::string> lines;

    if (prev.filterWheel && cur.filterWheel && cur.filterWheel->position &&
        cur.filterWheel->position != prev.filterWheel->position)
        lines.push_back("INFO: EQUIPMENT " + FilterValues(*cur.filterWheel));

    if (prev.rotator && cur.rotator && Known(cur.rotator->position))
    {
        std::string now = Num(cur.rotator->position, "%.1f deg");
        if (now != Num(prev.rotator->position, "%.1f deg"))
            lines.push_back("INFO: EQUIPMENT Rotator position = " + now);
    }

    if (prev.focuser && cur.focuser && cur.focuser->position && cur.focuser->position != prev.focuser->position)
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
                        int connectedRotator)
{
    Selection sel;
    sel.telescope = Pick(devices, "telescope", connectedTelescope);
    sel.rotator = Pick(devices, "rotator", connectedRotator);
    sel.filterWheel = Pick(devices, "filterwheel");
    sel.focuser = Pick(devices, "focuser");
    sel.conditions = Pick(devices, "observingconditions");
    for (const ConfiguredDevice& d : devices)
        if (Lower(d.type) == "camera" && d.number != guideCamera)
            sel.cameraCandidates.push_back(d.number);
    return sel;
}
}
