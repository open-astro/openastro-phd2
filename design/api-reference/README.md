# ASCOM / Alpaca API Reference

Authoritative specs for the device APIs used by this project's ASCOM (COM) and
Alpaca drivers. Downloaded 2026-07-12 from the ASCOM Initiative (MIT licensed).

## Files in this folder

- `AlpacaDeviceAPI_v1.yaml` — OpenAPI 3.1.1 spec for the Alpaca Device API.
  Covers all device types; the ones this codebase uses are **camera**,
  **telescope**, and **rotator**. This is the contract for
  `src/alpaca_client.*`, `src/cam_alpaca.*`, `src/scope_alpaca.*`,
  `src/rotator_alpaca.*`.
- `AlpacaManagementAPI_v1.yaml` — OpenAPI spec for the Alpaca Management API
  (`/management/apiversions`, `/management/v1/configureddevices`, etc.).
  Relevant to `src/alpaca_discovery.*`. Alpaca UDP discovery uses broadcast
  port **32227**, message `alpacadiscovery1`.

## Online sources (canonical, always current)

- Master interface specification (covers BOTH the COM and Alpaca forms of
  every member, side by side): https://ascom-standards.org/newdocs/
- Live YAML downloads / Swagger UI: https://ascom-standards.org/api/
  - https://ascom-standards.org/api/AlpacaDeviceAPI_v1.yaml
  - https://ascom-standards.org/api/AlpacaManagementAPI_v1.yaml

## ASCOM COM (classic Windows driver model)

There is no YAML for COM — the interfaces are defined as .NET/COM types:

- Interface source with full doc comments:
  https://github.com/ASCOMInitiative/ASCOMPlatform (`ASCOM.DeviceInterface/`)
- NuGet `ASCOM.DeviceInterfaces` contains the same definitions.
- This codebase talks to COM drivers via IDispatch late binding
  (`src/comdispatch.cpp`), using driver ProgIDs — no type library import.

Key fact: COM and Alpaca are member-for-member mirrors. COM
`Telescope.PulseGuide(Direction, Duration)` ≡ Alpaca
`PUT /api/v1/telescope/{device_number}/pulseguide`. So the Alpaca YAML doubles
as a machine-readable model of the COM surface: same members, same semantics,
same error codes (Alpaca `ErrorNumber` 0x400–0xFFF maps to COM HRESULTs
0x80040400–0x80040FFF, i.e. OR with 0x80040000; within that, 0x400–0x4FF is
reserved for ASCOM-defined errors and 0x500–0xFFF is the driver-specific range).

## Interface versions current as of 2026-07-12

- Camera: ICameraV4, Telescope: ITelescopeV4, Rotator: IRotatorV4
  (V4 adds async `Connect()`/`Disconnect()`/`Connecting` and `DeviceState`;
  older drivers expose V3 or earlier — feature-detect via `InterfaceVersion`).
