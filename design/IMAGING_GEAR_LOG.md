# Imaging gear in the guide log

The guide log describes the imaging train (telescope, camera, filter wheel, rotator,
focuser, observing conditions), read from the Alpaca server the guider already uses, so
[OpenAstro Guide Insight](https://github.com/open-astro/openastro-guide-insight) can judge
guiding against the imaging pixel scale. Nothing is entered in the guider.

This format is shared by **openastro-phd2** and **openastro-guider** (same code in
`src/imaging_gear*.cpp`) and is read by Guide Insight. Change all three together.
Tracked in open-astro/openastro-phd2#33 and open-astro/openastro-guider#82.

## Header lines

Written after `Equipment Profile =` at the start of every guiding and calibration section:

```
Equipment source = AlpacaBridge 192.168.1.20:11111
Imaging telescope = Sky-Watcher EQ6-R Pro, Focal length = 250 mm, Aperture = 51 mm, Focal ratio = f/4.9
Imaging camera = ZWO ASI2600MM Pro, Sensor = IMX571, Sensor type = Monochrome, Pixel size = 3.76 x 3.76 um, Sensor size = 6248 x 4176 px, Binning = 1x1, Pixel scale = 3.10 arc-sec/px, Gain = 100, Offset = 50, Readout mode = High Gain, Cooler = On, Sensor temp = -10.0 C
Filter wheel = ZWO EFW 7x36, Filter = Ha, Position = 3 of 7, Focus offset = 0
Rotator = ZWO CAA, Position = 92.3 deg, Mechanical position = 90.0 deg, Reverse = false
Focuser = ZWO EAF, Position = 12345 steps, Temperature = 8.4 C
Observing conditions = WeeWX, Seeing = 2.1 arc-sec, Star FWHM = 2.1 arc-sec, Wind speed = 3.2 m/s, Wind gust = 5.0 m/s, Temperature = 8.9 C, Humidity = 71 %, Dew point = 3.9 C, Pressure = 1012 hPa, Cloud cover = 0 %, Sky quality = 21.2 mag/arcsec^2
Guide train = Separate guide scope, Guide/imaging focal length ratio = 0.48
```

- `Equipment source` is `<ServerName> host:port` (`ServerName` from `management/v1/description`,
  `Alpaca` if not reported), `not Alpaca` when neither the guide camera nor the mount is
  connected through Alpaca (openastro-phd2 with INDI/ASCOM), or `unavailable` when the server
  could not be read. With `not Alpaca` or `unavailable` no other line follows. openastro-guider
  is Alpaca-only and never writes `not Alpaca`.
- A device line is omitted when there is no such device. `Imaging camera = ambiguous (n cameras)`
  is written, with no values, when the camera can't be chosen (below).
- Names and other text from the server have line breaks replaced by spaces and commas by
  semicolons, so they can't break a record or a value list.
- Every value the driver doesn't report (an error, `NotImplemented`, NaN, or 0 for focal length
  or aperture) is written as `unknown`; the key is always written.
- `Guide train` is always written: `Off-axis guider` when the guide/imaging focal length ratio is
  within 0.15 of 1, otherwise `Separate guide scope`; `unknown` when either focal length is unknown.

## INFO lines while guiding or calibrating

Written after the frame line during which the poller saw the change:

```
INFO: EQUIPMENT Filter = OIII, Position = 4 of 7, Focus offset = 12
INFO: EQUIPMENT Rotator position = 180.0 deg
INFO: EQUIPMENT Focuser position = 12410 steps, Temperature = 6.1 C
INFO: IMAGING EXPOSURE start, Duration = unknown, Filter = Ha
INFO: IMAGING EXPOSURE end, Duration = 300.0 s
INFO: CONDITIONS Seeing = 2.4 arc-sec, Wind speed = 5.1 m/s, Wind gust = 8.0 m/s, Temperature = 7.2 C, Humidity = 78 %
```

- `EQUIPMENT Filter` when the wheel settles at a new position (nothing while it moves, nothing when
  it moves back to the filter it left). A position read that fails keeps the last value, so a
  network blip is not taken for a move.
- `EQUIPMENT Rotator position` when the position changes at 0.1 deg resolution.
- `EQUIPMENT Focuser position` when the focuser position changes (not for temperature drift alone).
- `IMAGING EXPOSURE start` when the imaging camera's `CameraState` becomes waiting/exposing, and
  `end` when it leaves that state. Alpaca reports the requested duration of a running exposure
  nowhere, so `start` always has `Duration = unknown`; `end` has `LastExposureDuration`.
  `, Filter = ...` is present only when there is a filter wheel. Exposures shorter than the
  poll interval (2 s) can be missed.
- `CONDITIONS` once a minute, only when there is an ObservingConditions device.
- Lines queued between sections are dropped when the next header is written.

## Values

| Log value | Alpaca property |
|---|---|
| Telescope name, focal length, aperture | Telescope `Name`, `FocalLength`, `ApertureDiameter` (metres, written as mm) |
| Focal ratio | focal length ÷ aperture |
| Camera name, sensor, type | Camera `Name`, `SensorName`, `SensorType` (0 Monochrome, 1 Color, 2 RGGB, 3 CMYG, 4 CMYG2, 5 LRGB) |
| Pixel size, sensor size, binning | Camera `PixelSizeX/Y`, `CameraXSize/YSize`, `BinX/BinY` |
| Gain, offset, readout mode | Camera `Gain`, `Offset`, `ReadoutModes[ReadoutMode]` |
| Cooler, sensor temp | Camera `CoolerOn`, `CCDTemperature` |
| Imaging exposures | Camera `CameraState`, `LastExposureDuration` |
| Filter | FilterWheel `Name`, `Names[Position]`, `FocusOffsets[Position]`; `Position` is written 1-based ("3 of 7") |
| Rotator | Rotator `Name`, `Position`, `MechanicalPosition`, `Reverse` |
| Focuser | Focuser `Name`, `Position`, `Temperature` |
| Conditions | ObservingConditions `StarFWHM`, `WindSpeed`, `WindGust`, `Temperature`, `Humidity`, `DewPoint`, `Pressure`, `CloudCover`, `SkyQuality`. ASCOM defines `StarFWHM` as the seeing in arc-sec, so `Seeing` and `Star FWHM` both carry it. |
| Pixel scale | 206.265 × pixel size X (µm) × BinX ÷ focal length (mm) |
| Guide/imaging focal length ratio | guide focal length (guider profile) ÷ imaging focal length |

Number formats: focal length, aperture, humidity, pressure, cloud cover `%.0f`; focal ratio,
temperatures, wind, seeing, sky quality, rotator angles `%.1f`; pixel size, pixel scale and the
focal length ratio `%.2f`; exposure duration `%.1f`.

## Choosing the devices

From `GET management/v1/configureddevices` on the same server:

- **Telescope, rotator**: the one the guider is connected to through Alpaca, else the only one configured.
- **Filter wheel, focuser, observing conditions**: the only one of that type.
- **Imaging camera**: every camera except the guide camera. With one, that one. With several,
  the one that is connected and exposing, else the only connected one; otherwise ambiguous.

## Rules

- **Read only.** Only `GET`s; nothing is ever written to these devices, in particular not
  `Connected`. The imaging app owns them.
- **Never slows guiding.** A background thread reads with a 2 s timeout (no retries) and keeps the latest
  values in a cache; the log writer only reads the cache. Fast values (camera state, filter,
  rotator, focuser) every 2 s, everything else and device discovery every 30 s, a retry every
  10 s when the server can't be read. One failed discovery keeps the last known gear; after
  three in a row the header says `unavailable`. Successful reads aren't written to the debug log.
- A header written in the first seconds after the guider connects, before the first read
  completes, says `unavailable`.
- **No settings.**
