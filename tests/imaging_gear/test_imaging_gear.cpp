// Imaging-gear guide log lines (open-astro/openastro-phd2#33,
// open-astro/openastro-guider#82). The line format is shared with
// OpenAstro Guide Insight and the headless guider:
// design/IMAGING_GEAR_LOG.md. These tests pin it, and the device overrides
// and JSON behind set_imaging_gear_devices / get_imaging_gear
// (open-astro/openastro-guider#89).

#include <gtest/gtest.h>

#include "imaging_gear.h"

#include <cmath>

using namespace ImagingGear;

namespace
{
Snapshot FullRig()
{
    Snapshot s;
    s.source = Source::Alpaca;
    s.server = "AlpacaBridge 192.168.1.20:11111";

    Telescope t;
    t.name = "Sky-Watcher EQ6-R Pro";
    t.focalLengthM = 0.250;
    t.apertureM = 0.051;
    s.telescope = t;

    Camera c;
    c.name = "ZWO ASI2600MM Pro";
    c.sensorName = "IMX571";
    c.sensorType = 0;
    c.pixelSizeX = 3.76;
    c.pixelSizeY = 3.76;
    c.sizeX = 6248;
    c.sizeY = 4176;
    c.binX = 1;
    c.binY = 1;
    c.gain = 100;
    c.offset = 50;
    c.readoutMode = "High Gain";
    c.coolerOn = true;
    c.sensorTemp = -10.0;
    s.camera = c;

    FilterWheel fw;
    fw.name = "ZWO EFW 7x36";
    fw.names = { "L", "R", "Ha", "OIII", "SII", "G", "B" };
    fw.focusOffsets = { 0, 5, 0, 12, 8, 5, 5 };
    fw.position = 2;
    s.filterWheel = fw;

    Rotator r;
    r.name = "ZWO CAA";
    r.position = 92.3;
    r.mechanicalPosition = 90.0;
    r.reverse = false;
    s.rotator = r;

    Focuser f;
    f.name = "ZWO EAF";
    f.position = 12345;
    f.temperature = 8.4;
    s.focuser = f;

    Conditions oc;
    oc.name = "WeeWX";
    oc.starFwhm = 2.1;
    oc.windSpeed = 3.2;
    oc.windGust = 5.0;
    oc.temperature = 8.9;
    oc.humidity = 71;
    oc.dewPoint = 3.9;
    oc.pressure = 1012;
    oc.cloudCover = 0;
    oc.skyQuality = 21.2;
    s.conditions = oc;
    return s;
}
}

TEST(ImagingGear, HeaderForAFullRig)
{
    EXPECT_EQ(HeaderLines(FullRig(), 120.0),
              "Equipment source = AlpacaBridge 192.168.1.20:11111\n"
              "Imaging telescope = Sky-Watcher EQ6-R Pro, Focal length = 250 mm, Aperture = 51 mm, Focal ratio = f/4.9\n"
              "Imaging camera = ZWO ASI2600MM Pro, Sensor = IMX571, Sensor type = Monochrome, Pixel size = 3.76 x 3.76 um, "
              "Sensor size = 6248 x 4176 px, Binning = 1x1, Pixel scale = 3.10 arc-sec/px, Gain = 100, Offset = 50, "
              "Readout mode = High Gain, Cooler = On, Sensor temp = -10.0 C\n"
              "Filter wheel = ZWO EFW 7x36, Filter = Ha, Position = 3 of 7, Focus offset = 0\n"
              "Rotator = ZWO CAA, Position = 92.3 deg, Mechanical position = 90.0 deg, Reverse = false\n"
              "Focuser = ZWO EAF, Position = 12345 steps, Temperature = 8.4 C\n"
              "Observing conditions = WeeWX, Seeing = 2.1 arc-sec, Star FWHM = 2.1 arc-sec, Wind speed = 3.2 m/s, "
              "Wind gust = 5.0 m/s, Temperature = 8.9 C, Humidity = 71 %, Dew point = 3.9 C, Pressure = 1012 hPa, "
              "Cloud cover = 0 %, Sky quality = 21.2 mag/arcsec^2\n"
              "Guide train = Separate guide scope, Guide/imaging focal length ratio = 0.48\n");
}

TEST(ImagingGear, NotAlpacaAndUnavailableWriteOnlyTheSourceLine)
{
    Snapshot s = FullRig();
    s.source = Source::NotAlpaca;
    EXPECT_EQ(HeaderLines(s, 120.0), "Equipment source = not Alpaca\n");
    s.source = Source::Unavailable;
    EXPECT_EQ(HeaderLines(s, 120.0), "Equipment source = unavailable\n");
}

TEST(ImagingGear, MissingDevicesAreOmitted)
{
    Snapshot s;
    s.source = Source::Alpaca;
    s.server = "AlpacaBridge 10.0.0.2:11111";
    EXPECT_EQ(HeaderLines(s, 0.0),
              "Equipment source = AlpacaBridge 10.0.0.2:11111\n"
              "Guide train = unknown, Guide/imaging focal length ratio = unknown\n");
}

TEST(ImagingGear, UnreportedValuesAreUnknown)
{
    Snapshot s;
    s.source = Source::Alpaca;
    s.server = "AlpacaBridge 10.0.0.2:11111";
    Telescope t;
    t.name = "Mount";
    t.focalLengthM = 0.0; // left blank in AlpacaBridge
    s.telescope = t;
    Camera c;
    c.name = "Cam";
    s.camera = c;
    FilterWheel fw;
    fw.name = "EFW";
    fw.names = { "L", "R" };
    s.filterWheel = fw; // position unset: moving
    Rotator r;
    r.name = "Rot";
    s.rotator = r;
    Focuser f;
    f.name = "Foc";
    s.focuser = f;
    Conditions oc;
    oc.name = "OC";
    s.conditions = oc;

    EXPECT_EQ(HeaderLines(s, 120.0),
              "Equipment source = AlpacaBridge 10.0.0.2:11111\n"
              "Imaging telescope = Mount, Focal length = unknown, Aperture = unknown, Focal ratio = unknown\n"
              "Imaging camera = Cam, Sensor = unknown, Sensor type = unknown, Pixel size = unknown, Sensor size = unknown, "
              "Binning = unknown, Pixel scale = unknown, Gain = unknown, Offset = unknown, Readout mode = unknown, "
              "Cooler = unknown, Sensor temp = unknown\n"
              "Filter wheel = EFW, Filter = unknown, Position = unknown, Focus offset = unknown\n"
              "Rotator = Rot, Position = unknown, Mechanical position = unknown, Reverse = unknown\n"
              "Focuser = Foc, Position = unknown, Temperature = unknown\n"
              "Observing conditions = OC, Seeing = unknown, Star FWHM = unknown, Wind speed = unknown, Wind gust = unknown, "
              "Temperature = unknown, Humidity = unknown, Dew point = unknown, Pressure = unknown, Cloud cover = unknown, "
              "Sky quality = unknown\n"
              "Guide train = unknown, Guide/imaging focal length ratio = unknown\n");
}

TEST(ImagingGear, AmbiguousCameraSkipsTheCameraValues)
{
    Snapshot s;
    s.source = Source::Alpaca;
    s.server = "AlpacaBridge 10.0.0.2:11111";
    s.ambiguousCameras = 2;
    EXPECT_EQ(HeaderLines(s, 0.0),
              "Equipment source = AlpacaBridge 10.0.0.2:11111\n"
              "Imaging camera = ambiguous (2 cameras)\n"
              "Guide train = unknown, Guide/imaging focal length ratio = unknown\n");
}

TEST(ImagingGear, PixelScaleUsesBinningAndFocalLength)
{
    Snapshot s = FullRig();
    ASSERT_TRUE(PixelScale(s).has_value());
    EXPECT_NEAR(*PixelScale(s), 206.265 * 3.76 / 250.0, 1e-9);
    s.camera->binX = 2;
    EXPECT_NEAR(*PixelScale(s), 206.265 * 3.76 * 2 / 250.0, 1e-9);
    s.telescope->focalLengthM = 0.0;
    EXPECT_FALSE(PixelScale(s).has_value());
    s = FullRig();
    s.camera->pixelSizeX.reset();
    EXPECT_FALSE(PixelScale(s).has_value());
}

TEST(ImagingGear, GuideTrainNearOneIsAnOffAxisGuider)
{
    Snapshot s = FullRig();
    std::string h = HeaderLines(s, 240.0);
    EXPECT_NE(h.find("Guide train = Off-axis guider, Guide/imaging focal length ratio = 0.96\n"), std::string::npos) << h;
}

TEST(ImagingGear, ChangeLinesForFilterRotatorAndFocuser)
{
    Snapshot a = FullRig();
    Snapshot b = a;
    EXPECT_TRUE(EquipmentChangeLines(a, b).empty());

    b.filterWheel->position = 3;
    b.rotator->position = 180.0;
    b.focuser->position = 12410;
    b.focuser->temperature = 6.1;
    std::vector<std::string> lines = EquipmentChangeLines(a, b);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "INFO: EQUIPMENT Filter = OIII, Position = 4 of 7, Focus offset = 12");
    EXPECT_EQ(lines[1], "INFO: EQUIPMENT Rotator position = 180.0 deg");
    EXPECT_EQ(lines[2], "INFO: EQUIPMENT Focuser position = 12410 steps, Temperature = 6.1 C");
}

TEST(ImagingGear, NoChangeLineWhileTheWheelMovesOrForTemperatureAlone)
{
    Snapshot a = FullRig();
    Snapshot b = a;
    b.filterWheel->position.reset(); // moving
    b.focuser->temperature = 7.0; // drift only
    EXPECT_TRUE(EquipmentChangeLines(a, b).empty());
}

TEST(ImagingGear, ConditionsLine)
{
    Snapshot s = FullRig();
    s.conditions->starFwhm = 2.4;
    s.conditions->windSpeed = 5.1;
    s.conditions->windGust = 8.0;
    s.conditions->temperature = 7.2;
    s.conditions->humidity = 78;
    EXPECT_EQ(ConditionsLine(s),
              "INFO: CONDITIONS Seeing = 2.4 arc-sec, Wind speed = 5.1 m/s, Wind gust = 8.0 m/s, "
              "Temperature = 7.2 C, Humidity = 78 %");
    s.conditions.reset();
    EXPECT_EQ(ConditionsLine(s), "");
}

TEST(ImagingGear, SelectDevices)
{
    std::vector<ConfiguredDevice> devs = {
        { "Telescope", 0, "EQ6" },
        { "Camera", 0, "ASI120MM" },
        { "Camera", 1, "ASI2600MM" },
        { "FilterWheel", 0, "EFW" },
        { "Focuser", 0, "EAF" },
        { "Rotator", 0, "CAA" },
        { "ObservingConditions", 0, "WeeWX" },
    };
    Selection sel = SelectDevices(devs, 0, 0, 0);
    EXPECT_EQ(sel.telescope, 0);
    EXPECT_EQ(sel.rotator, 0);
    EXPECT_EQ(sel.filterWheel, 0);
    EXPECT_EQ(sel.focuser, 0);
    EXPECT_EQ(sel.conditions, 0);
    EXPECT_EQ(sel.cameraCandidates, std::vector<int>({ 1 }));
}

TEST(ImagingGear, SelectDevicesNeedsExactlyOneOfAType)
{
    std::vector<ConfiguredDevice> devs = {
        { "Camera", 0, "a" },   { "Camera", 1, "b" },     { "Camera", 2, "c" },     { "Focuser", 0, "f1" },
        { "Focuser", 1, "f2" }, { "Telescope", 0, "t1" }, { "Telescope", 1, "t2" }, { "filterwheel", 3, "lower-case type" },
    };
    // not connected to a telescope through Alpaca: two telescopes, so none
    Selection sel = SelectDevices(devs, -1, -1, -1);
    EXPECT_FALSE(sel.telescope.has_value());
    EXPECT_FALSE(sel.focuser.has_value());
    EXPECT_EQ(sel.filterWheel, 3);
    EXPECT_EQ(sel.cameraCandidates, std::vector<int>({ 0, 1, 2 }));
    // the connected telescope wins over the exactly-one rule
    EXPECT_EQ(SelectDevices(devs, -1, 1, -1).telescope, 1);
}

TEST(ImagingGear, NamesCannotBreakTheLineFormat)
{
    Snapshot s;
    s.source = Source::Alpaca;
    s.server = "Bridge\n 10.0.0.2:11111";
    Focuser f;
    f.name = "EAF, v2\r\nINFO: fake";
    s.focuser = f;
    std::string h = HeaderLines(s, 0.0);
    EXPECT_NE(h.find("Equipment source = Bridge  10.0.0.2:11111\n"), std::string::npos) << h;
    EXPECT_NE(h.find("Focuser = EAF; v2  INFO: fake, Position = unknown"), std::string::npos) << h;
}

TEST(ImagingGear, ADifferentDeviceIsNotAMove)
{
    // set_imaging_gear_devices switched to another wheel, rotator or focuser
    Snapshot a = FullRig();
    Snapshot b = a;
    b.filterWheel->device = 1;
    b.filterWheel->position = 3;
    b.rotator->device = 1;
    b.rotator->position = 180.0;
    b.focuser->device = 1;
    b.focuser->position = 500;
    EXPECT_TRUE(EquipmentChangeLines(a, b).empty());
}

// --- set_imaging_gear_devices: devices named by the imaging app (Ara) ---

namespace
{
std::vector<ConfiguredDevice> TwoOfEach()
{
    return {
        { "Telescope", 0, "t0" },
        { "Telescope", 1, "t1" },
        { "Camera", 0, "guide" },
        { "Camera", 1, "c1" },
        { "Camera", 2, "c2" },
        { "FilterWheel", 0, "fw0" },
        { "FilterWheel", 1, "fw1" },
        { "Focuser", 0, "f0" },
        { "Focuser", 1, "f1" },
        { "Rotator", 0, "r0" },
        { "Rotator", 1, "r1" },
        { "ObservingConditions", 0, "oc0" },
        { "ObservingConditions", 1, "oc1" },
    };
}
}

TEST(ImagingGear, OverridesWinOverTheSelectionRules)
{
    Overrides o;
    o.telescope = 1;
    o.camera = 2;
    o.filterWheel = 1;
    o.focuser = 0;
    o.rotator = 1;
    o.conditions = 1;
    // the guider is connected to telescope 0 and rotator 0: the overrides still win
    Selection sel = SelectDevices(TwoOfEach(), 0, 0, 0, o);
    EXPECT_EQ(sel.telescope, 1);
    EXPECT_EQ(sel.rotator, 1);
    EXPECT_EQ(sel.filterWheel, 1);
    EXPECT_EQ(sel.focuser, 0);
    EXPECT_EQ(sel.conditions, 1);
    EXPECT_EQ(sel.cameraCandidates, std::vector<int>({ 2 })); // a single candidate is never ambiguous

    // without overrides: the connected telescope / rotator, nothing for two of a kind
    sel = SelectDevices(TwoOfEach(), 0, 0, 0, Overrides());
    EXPECT_EQ(sel.telescope, 0);
    EXPECT_EQ(sel.rotator, 0);
    EXPECT_FALSE(sel.filterWheel.has_value());
    EXPECT_EQ(sel.cameraCandidates, std::vector<int>({ 1, 2 }));
}

TEST(ImagingGear, AnOverrideOnlyAffectsItsOwnType)
{
    Overrides o;
    o.focuser = 1;
    Selection sel = SelectDevices(TwoOfEach(), 0, 0, -1, o);
    EXPECT_EQ(sel.focuser, 1);
    EXPECT_EQ(sel.telescope, 0);
    EXPECT_FALSE(sel.rotator.has_value());
    EXPECT_EQ(sel.cameraCandidates, std::vector<int>({ 1, 2 }));
}

TEST(ImagingGear, AnOverrideForAnUnconfiguredDeviceSelectsNothing)
{
    Overrides o;
    o.focuser = 5;
    o.camera = 7;
    o.telescope = 3;
    Selection sel = SelectDevices(TwoOfEach(), 0, 0, 0, o);
    EXPECT_FALSE(sel.focuser.has_value());
    EXPECT_FALSE(sel.telescope.has_value());
    EXPECT_TRUE(sel.cameraCandidates.empty());
}

TEST(ImagingGear, ACameraOverrideNeverPicksTheGuideCamera)
{
    // the guide camera changed to the overridden one after the override was set
    Overrides o;
    o.camera = 0;
    EXPECT_EQ(SelectDevices(TwoOfEach(), 0, -1, -1, o).cameraCandidates, std::vector<int>({ 1, 2 }));
    // not connected through Alpaca: no guide camera to exclude
    EXPECT_EQ(SelectDevices(TwoOfEach(), -1, -1, -1, o).cameraCandidates, std::vector<int>({ 0 }));
}

TEST(ImagingGear, SetOverride)
{
    Overrides o;
    std::string err;
    EXPECT_TRUE(SetOverride(&o, "camera", 2, 0, &err));
    EXPECT_TRUE(SetOverride(&o, "filterwheel", 1, 0, &err));
    EXPECT_TRUE(SetOverride(&o, "focuser", 0, 0, &err));
    EXPECT_TRUE(SetOverride(&o, "rotator", 3, 0, &err));
    EXPECT_TRUE(SetOverride(&o, "telescope", 4, 0, &err));
    EXPECT_TRUE(SetOverride(&o, "observingconditions", 5, 0, &err));
    EXPECT_EQ(o.camera, 2);
    EXPECT_EQ(o.filterWheel, 1);
    EXPECT_EQ(o.focuser, 0);
    EXPECT_EQ(o.rotator, 3);
    EXPECT_EQ(o.telescope, 4);
    EXPECT_EQ(o.conditions, 5);

    EXPECT_TRUE(SetOverride(&o, "focuser", -1, 0, &err)); // -1 clears
    EXPECT_FALSE(o.focuser.has_value());
    EXPECT_EQ(o.rotator, 3);
}

TEST(ImagingGear, SetOverrideRefusesTheGuideCameraAndBadInput)
{
    Overrides o;
    o.camera = 2;
    const Overrides before = o;
    std::string err;
    EXPECT_FALSE(SetOverride(&o, "camera", 0, 0, &err));
    EXPECT_EQ(err, "camera 0 is the guide camera, not the imaging camera");
    EXPECT_FALSE(SetOverride(&o, "filter_wheel", 1, 0, &err));
    EXPECT_EQ(err, "unknown device type: filter_wheel");
    EXPECT_FALSE(SetOverride(&o, "focuser", -2, 0, &err));
    EXPECT_EQ(err, "focuser: expected an Alpaca device number, or -1 to clear");
    EXPECT_TRUE(o == before);

    // a guide camera that isn't Alpaca (-1) doesn't block camera 0
    EXPECT_TRUE(SetOverride(&o, "camera", 0, -1, &err));
    EXPECT_EQ(o.camera, 0);
}

// --- get_imaging_gear: the cached snapshot as JSON ---

namespace
{
const char *NO_OVERRIDES = "\"overrides\":{\"camera\":null,\"filterwheel\":null,\"focuser\":null,\"rotator\":null,"
                           "\"telescope\":null,\"observingconditions\":null}";
}

TEST(ImagingGear, JsonForAFullRig)
{
    Snapshot s = FullRig();
    s.telescope->device = 0;
    s.camera->device = 1;
    s.filterWheel->device = 0;
    s.rotator->device = 0;
    s.focuser->device = 0;
    s.conditions->device = 0;
    Overrides o;
    o.camera = 1;
    EXPECT_EQ(SnapshotJson(s, 120.0, o),
              "{\"source\":\"alpaca\",\"server\":\"AlpacaBridge 192.168.1.20:11111\","
              "\"telescope\":{\"device\":0,\"name\":\"Sky-Watcher EQ6-R Pro\",\"focal_length_mm\":250,"
              "\"aperture_mm\":51,\"focal_ratio\":4.90196},"
              "\"camera\":{\"device\":1,\"name\":\"ZWO ASI2600MM Pro\",\"sensor\":\"IMX571\",\"sensor_type\":\"Monochrome\","
              "\"pixel_size_x_um\":3.76,\"pixel_size_y_um\":3.76,\"width_px\":6248,\"height_px\":4176,\"bin_x\":1,"
              "\"bin_y\":1,\"pixel_scale_arcsec\":3.10223,\"gain\":100,\"offset\":50,\"readout_mode\":\"High Gain\","
              "\"cooler_on\":true,\"sensor_temp_c\":-10},"
              "\"filter_wheel\":{\"device\":0,\"name\":\"ZWO EFW 7x36\",\"filter\":\"Ha\",\"position\":3,\"count\":7,"
              "\"focus_offset\":0},"
              "\"rotator\":{\"device\":0,\"name\":\"ZWO CAA\",\"position_deg\":92.3,\"mechanical_position_deg\":90,"
              "\"reverse\":false},"
              "\"focuser\":{\"device\":0,\"name\":\"ZWO EAF\",\"position\":12345,\"temperature_c\":8.4},"
              "\"conditions\":{\"device\":0,\"name\":\"WeeWX\",\"seeing_arcsec\":2.1,\"wind_speed_m_s\":3.2,"
              "\"wind_gust_m_s\":5,\"temperature_c\":8.9,\"humidity_pct\":71,\"dew_point_c\":3.9,\"pressure_hpa\":1012,"
              "\"cloud_cover_pct\":0,\"sky_quality_mag_arcsec2\":21.2},"
              "\"guide_train\":{\"type\":\"separate_guide_scope\",\"ratio\":0.48},"
              "\"ambiguous_cameras\":0,"
              "\"overrides\":{\"camera\":1,\"filterwheel\":null,\"focuser\":null,\"rotator\":null,\"telescope\":null,"
              "\"observingconditions\":null}}");
}

TEST(ImagingGear, JsonUnknownValuesAreNull)
{
    Snapshot s;
    s.source = Source::Alpaca;
    s.server = "Alpaca 10.0.0.2:11111";
    Telescope t;
    t.name = "Mount";
    t.focalLengthM = 0.0; // left blank in AlpacaBridge
    t.apertureM = std::nan("");
    s.telescope = t;
    Camera c;
    c.name = "Cam";
    s.camera = c;
    FilterWheel fw;
    fw.name = "EFW";
    s.filterWheel = fw; // position unset: moving; no names
    Rotator r;
    s.rotator = r; // no name
    Focuser f;
    f.name = "Foc";
    s.focuser = f;
    Conditions oc;
    oc.name = "OC";
    s.conditions = oc;

    EXPECT_EQ(SnapshotJson(s, 120.0, Overrides()),
              "{\"source\":\"alpaca\",\"server\":\"Alpaca 10.0.0.2:11111\","
              "\"telescope\":{\"device\":null,\"name\":\"Mount\",\"focal_length_mm\":null,\"aperture_mm\":null,"
              "\"focal_ratio\":null},"
              "\"camera\":{\"device\":null,\"name\":\"Cam\",\"sensor\":null,\"sensor_type\":null,\"pixel_size_x_um\":null,"
              "\"pixel_size_y_um\":null,\"width_px\":null,\"height_px\":null,\"bin_x\":null,\"bin_y\":null,"
              "\"pixel_scale_arcsec\":null,\"gain\":null,\"offset\":null,\"readout_mode\":null,\"cooler_on\":null,"
              "\"sensor_temp_c\":null},"
              "\"filter_wheel\":{\"device\":null,\"name\":\"EFW\",\"filter\":null,\"position\":null,\"count\":null,"
              "\"focus_offset\":null},"
              "\"rotator\":{\"device\":null,\"name\":null,\"position_deg\":null,\"mechanical_position_deg\":null,"
              "\"reverse\":null},"
              "\"focuser\":{\"device\":null,\"name\":\"Foc\",\"position\":null,\"temperature_c\":null},"
              "\"conditions\":{\"device\":null,\"name\":\"OC\",\"seeing_arcsec\":null,\"wind_speed_m_s\":null,"
              "\"wind_gust_m_s\":null,\"temperature_c\":null,\"humidity_pct\":null,\"dew_point_c\":null,"
              "\"pressure_hpa\":null,\"cloud_cover_pct\":null,\"sky_quality_mag_arcsec2\":null},"
              "\"guide_train\":{\"type\":null,\"ratio\":null},"
              "\"ambiguous_cameras\":0," +
                  std::string(NO_OVERRIDES) + "}");
}

TEST(ImagingGear, JsonWithoutTheServerHasOnlyTheSource)
{
    Snapshot s = FullRig(); // stale values are never reported
    const std::string rest = "\"server\":null,\"telescope\":null,\"camera\":null,\"filter_wheel\":null,\"rotator\":null,"
                             "\"focuser\":null,\"conditions\":null,\"guide_train\":{\"type\":null,\"ratio\":null},"
                             "\"ambiguous_cameras\":0," +
        std::string(NO_OVERRIDES) + "}";
    s.source = Source::NotAlpaca;
    EXPECT_EQ(SnapshotJson(s, 120.0, Overrides()), "{\"source\":\"not_alpaca\"," + rest);
    s.source = Source::Unavailable;
    EXPECT_EQ(SnapshotJson(s, 120.0, Overrides()), "{\"source\":\"unavailable\"," + rest);
}

TEST(ImagingGear, JsonAmbiguousCameraAndOffAxisGuider)
{
    Snapshot s = FullRig();
    s.camera.reset();
    s.ambiguousCameras = 3;
    std::string j = SnapshotJson(s, 240.0, Overrides());
    EXPECT_NE(j.find("\"camera\":null,"), std::string::npos) << j;
    EXPECT_NE(j.find("\"guide_train\":{\"type\":\"off_axis_guider\",\"ratio\":0.96},\"ambiguous_cameras\":3,"),
              std::string::npos)
        << j;
    // the guide focal length is unknown
    j = SnapshotJson(s, 0.0, Overrides());
    EXPECT_NE(j.find("\"guide_train\":{\"type\":null,\"ratio\":null}"), std::string::npos) << j;
}

TEST(ImagingGear, JsonEscapesText)
{
    Snapshot s;
    s.source = Source::Alpaca;
    s.server = "Bridge \"A\" 10.0.0.2:11111";
    Focuser f;
    f.name = "EAF\\v2\r\n\t\x01 \xc2\xb5m"; // UTF-8 passes through
    s.focuser = f;
    std::string j = SnapshotJson(s, 0.0, Overrides());
    EXPECT_NE(j.find("\"server\":\"Bridge \\\"A\\\" 10.0.0.2:11111\""), std::string::npos) << j;
    EXPECT_NE(j.find("\"name\":\"EAF\\\\v2\\r\\n\\t\\u0001 \xc2\xb5m\""), std::string::npos) << j;
}

TEST(ImagingGear, OverridesJson)
{
    // set_imaging_gear_devices returns the overrides now in effect
    Overrides o;
    EXPECT_EQ(OverridesJson(o),
              "{\"camera\":null,\"filterwheel\":null,\"focuser\":null,\"rotator\":null,"
              "\"telescope\":null,\"observingconditions\":null}");
    o.camera = 1;
    o.filterWheel = 0;
    o.focuser = 2;
    o.rotator = 3;
    o.telescope = 4;
    o.conditions = 5;
    EXPECT_EQ(OverridesJson(o),
              "{\"camera\":1,\"filterwheel\":0,\"focuser\":2,\"rotator\":3,\"telescope\":4,"
              "\"observingconditions\":5}");
}

TEST(ImagingGear, AnUnreportedNameIsUnknown)
{
    // the snapshot keeps an unreported name empty: "unknown" in the log, null in JSON
    Snapshot s = FullRig();
    s.telescope->name.clear();
    s.camera->name.clear();
    s.filterWheel->name.clear();
    s.rotator->name.clear();
    s.focuser->name.clear();
    s.conditions->name.clear();
    std::string h = HeaderLines(s, 120.0);
    for (const char *line : { "Imaging telescope = unknown, ", "Imaging camera = unknown, ", "Filter wheel = unknown, ",
                              "Rotator = unknown, ", "Focuser = unknown, ", "Observing conditions = unknown, " })
        EXPECT_NE(h.find(line), std::string::npos) << line << "\n" << h;
    std::string j = SnapshotJson(s, 120.0, Overrides());
    EXPECT_EQ(j.find("\"name\":\""), std::string::npos) << j;
}
