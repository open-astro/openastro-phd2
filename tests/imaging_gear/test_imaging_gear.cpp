// Imaging-gear guide log lines (open-astro/openastro-phd2#33,
// open-astro/openastro-guider#82). The line format is shared with
// OpenAstro Guide Insight and the headless guider:
// design/IMAGING_GEAR_LOG.md. These tests pin it.

#include <gtest/gtest.h>

#include "imaging_gear.h"

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
