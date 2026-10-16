/*
 *  imaging_gear.h
 *  PHD Guiding
 *
 *  The imaging train (telescope, camera, filter wheel, rotator, focuser,
 *  observing conditions) read from the Alpaca server the guider already uses,
 *  and the guide log lines that describe it. Line format: design/IMAGING_GEAR_LOG.md.
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
    int number;
    std::string name;
};

struct Telescope
{
    std::string name;
    std::optional<double> focalLengthM; // ASCOM FocalLength, metres
    std::optional<double> apertureM; // ASCOM ApertureDiameter, metres
};

struct Camera
{
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
    std::string name;
    std::optional<int> position; // ASCOM, 0-based; unset while moving
    std::vector<std::string> names;
    std::vector<int> focusOffsets;
};

struct Rotator
{
    std::string name;
    std::optional<double> position, mechanicalPosition;
    std::optional<bool> reverse;
};

struct Focuser
{
    std::string name;
    std::optional<int> position;
    std::optional<double> temperature;
};

struct Conditions
{
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

// arc-sec per imaging pixel: 206.265 x pixel size (um) x binning / focal length (mm)
std::optional<double> PixelScale(const Snapshot& s);

// header block written at the start of each guiding and calibration section;
// guideFocalLengthMm <= 0 means unknown
std::string HeaderLines(const Snapshot& s, double guideFocalLengthMm);

// "INFO: EQUIPMENT ..." lines for what changed from prev to cur
std::vector<std::string> EquipmentChangeLines(const Snapshot& prev, const Snapshot& cur);

// "INFO: CONDITIONS ..." line; empty when there is no conditions device
std::string ConditionsLine(const Snapshot& s);

// Which devices to read, chosen from management/v1/configureddevices.
struct Selection
{
    std::optional<int> telescope, rotator, filterWheel, focuser, conditions;
    std::vector<int> cameraCandidates; // every camera except the guide camera
};

// connected* are the device numbers the guider is connected to, or -1
Selection SelectDevices(const std::vector<ConfiguredDevice>& devices, int guideCamera, int connectedTelescope,
                        int connectedRotator);
}

#endif
