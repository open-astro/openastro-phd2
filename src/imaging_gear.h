/*
 *  imaging_gear.h
 *  PHD Guiding
 *
 *  The imaging train (telescope, camera, filter wheel, rotator, focuser,
 *  observing conditions) read from the Alpaca server the guider already uses,
 *  the guide log lines that describe it, and the get_imaging_gear JSON. Line
 *  format: design/IMAGING_GEAR_LOG.md.
 *  Kept free of wx so it can be unit tested (tests/imaging_gear/).
 */

#ifndef IMAGING_GEAR_H_INCLUDED
#define IMAGING_GEAR_H_INCLUDED

#include <optional>
#include <string>
#include <vector>

namespace ImagingGear
{
struct ConfiguredDevice
{
    std::string type; // as reported, e.g. "Camera", "FilterWheel"
    int number = -1;
    std::string name;
};

struct Telescope
{
    int device = -1; // Alpaca device number, -1 when unknown
    std::string name;
    std::optional<double> focalLengthM; // ASCOM FocalLength, metres
    std::optional<double> apertureM; // ASCOM ApertureDiameter, metres
};

struct Camera
{
    int device = -1; // Alpaca device number, -1 when unknown
    std::string name;
    std::optional<std::string> sensorName;
    std::optional<int> sensorType; // ASCOM SensorType
    std::optional<double> pixelSizeX, pixelSizeY;
    std::optional<int> sizeX, sizeY, binX, binY, gain, offset;
    std::optional<std::string> readoutMode;
    std::optional<bool> coolerOn;
    std::optional<double> sensorTemp;
};

struct FilterWheel
{
    int device = -1; // Alpaca device number, -1 when unknown
    std::string name;
    std::optional<int> position; // ASCOM, 0-based; unset while moving
    std::vector<std::string> names;
    std::vector<int> focusOffsets;
};

struct Rotator
{
    int device = -1; // Alpaca device number, -1 when unknown
    std::string name;
    std::optional<double> position, mechanicalPosition;
    std::optional<bool> reverse;
};

struct Focuser
{
    int device = -1; // Alpaca device number, -1 when unknown
    std::string name;
    std::optional<int> position;
    std::optional<double> temperature;
};

struct Conditions
{
    int device = -1; // Alpaca device number, -1 when unknown
    std::string name;
    std::optional<double> starFwhm, windSpeed, windGust, temperature, humidity, dewPoint, pressure, cloudCover, skyQuality;
};

enum class Source
{
    NotAlpaca, // the guider is not connected through Alpaca
    Unavailable, // Alpaca, but the server could not be read
    Alpaca,
};

struct Snapshot
{
    Source source = Source::Unavailable;
    std::string server; // "<ServerName> host:port"
    std::optional<Telescope> telescope;
    std::optional<Camera> camera;
    int ambiguousCameras = 0; // > 0: this many candidates, none picked
    std::optional<FilterWheel> filterWheel;
    std::optional<Rotator> rotator;
    std::optional<Focuser> focuser;
    std::optional<Conditions> conditions;
};

// text from the server with line breaks and commas (the log's delimiters) replaced
std::string CleanText(std::string s);

// arc-sec per imaging pixel: 206.265 x pixel size (um) x binning / focal length (mm)
std::optional<double> PixelScale(const Snapshot& s);

// header block written at the start of each guiding and calibration section;
// guideFocalLengthMm <= 0 means unknown
std::string HeaderLines(const Snapshot& s, double guideFocalLengthMm);

// "INFO: EQUIPMENT ..." lines for what changed from prev to cur
std::vector<std::string> EquipmentChangeLines(const Snapshot& prev, const Snapshot& cur);

// "INFO: CONDITIONS ..." line; empty when there is no conditions device
std::string ConditionsLine(const Snapshot& s);

// Alpaca device numbers the imaging app named with set_imaging_gear_devices;
// each one wins over the selection rules for its type.
struct Overrides
{
    std::optional<int> camera, filterWheel, focuser, rotator, telescope, conditions;

    bool operator==(const Overrides& o) const
    {
        return camera == o.camera && filterWheel == o.filterWheel && focuser == o.focuser && rotator == o.rotator &&
            telescope == o.telescope && conditions == o.conditions;
    }
    bool operator!=(const Overrides& o) const { return !(*this == o); }
};

// Sets one override; name is the set_imaging_gear_devices parameter ("camera",
// "filterwheel", "focuser", "rotator", "telescope", "observingconditions"),
// device an Alpaca device number or -1 to clear it. guideCamera is the guide
// camera's device number (-1 if not Alpaca), which is refused as the imaging
// camera. On error o is unchanged.
bool SetOverride(Overrides *o, const std::string& name, int device, int guideCamera, std::string *error);

// {"camera": n|null, "filterwheel": ..., "observingconditions": ...}
std::string OverridesJson(const Overrides& overrides);

// get_imaging_gear: the snapshot as a JSON object, null for every value the
// log writes as "unknown"; guideFocalLengthMm <= 0 means unknown
std::string SnapshotJson(const Snapshot& s, double guideFocalLengthMm, const Overrides& overrides);

// Which devices to read, chosen from management/v1/configureddevices.
struct Selection
{
    std::optional<int> telescope, rotator, filterWheel, focuser, conditions;
    std::vector<int> cameraCandidates; // every camera except the guide camera
};

// connected* are the device numbers the guider is connected to, or -1. An
// override wins when that device is configured (none of its type is read when
// it isn't); a camera override that is the guide camera is ignored.
Selection SelectDevices(const std::vector<ConfiguredDevice>& devices, int guideCamera, int connectedTelescope,
                        int connectedRotator, const Overrides& overrides = Overrides());
}

#endif
