# OpenAstro PHD2

<img src="icons/oa512.png" alt="OpenAstro Logo" width="125">

## Overview

This repository contains an OpenAstro-maintained build derived from PHD2 that supports **ASCOM Alpaca**, **INDI**, and (on Windows) **ASCOM COM** equipment drivers. It is intended for use with [**AlpacaBridge**](https://github.com/open-astro/AlpacaBridge) and related Alpaca-based workflows, with [**INDI**](https://indilib.org/) as the alternative path for Linux/Pi rigs and remote INDI servers, and native ASCOM as a Windows fallback for hardware whose vendor only ships an ASCOM driver.

## Important Notice

- This is **not** an official PHD2 release.
- It is a **private build** supported by OpenAstro, not by the PHD2 developers or community.
- Support requests should be directed to OpenAstro.

## Scope

This fork supports three driver transports for cameras, mounts, and rotators, all of which rely on **user-installed** drivers — no vendor SDKs are bundled:

- **ASCOM Alpaca** - cameras, mounts, rotators (network-based; works on Windows, Linux, macOS, Raspberry Pi).
- **INDI** - cameras, mounts, rotators (native INDI client; ideal for Linux/Pi rigs or remote INDI servers).
- **ASCOM COM** (Windows only) - cameras, mounts, rotators (late-bound to ASCOM Platform; the user installs whichever ASCOM driver their hardware vendor ships).
- Designed to work alongside AlpacaBridge.

**Removed compared to upstream PHD2:** all vendor SDK camera backends (ZWO, QHY, SBIG, Altair, ToupTek, SVBony, PlayerOne, Moravian, etc.), adaptive optics / step-guiders, on-camera ST4, and auxiliary mounts. The intentional shape of this fork is that every supported camera/mount/rotator is a thin protocol shim over a driver the user installs and the vendor maintains — we don't take on the burden of tracking vendor SDKs. If you need a bundled-SDK build, use upstream OpenPHDGuiding/phd2 instead.

## Running

Each platform has a `run_*` script that configures and (with `--build`) compiles for fast local iteration. The resulting binary stays inside `tmp/` and is not redistributable - see [Building Installers](#building-installers) for that.

### Linux / Debian / Raspberry Pi

Supported: **Debian 13 Trixie** and **Raspberry Pi OS Trixie**, on amd64 or arm64. 32-bit ARM (armhf) and i386 are not supported; on a 64-bit-capable Pi (3/4/5) install the 64-bit Raspberry Pi OS.

```bash
./run_deb.sh --build           # configure + parallel build
```

Run the binary at `tmp/phd2.bin` (or via the `tmp/phd2` wrapper). INDI 2.0+ is required; if your system `libindi` is missing or older (e.g. Pi OS stock 1.9.x), the script auto-fetches and statically compiles INDI 2.2.1.1 (adds ~3-5 min on first build). Force a specific path with `USE_SYSTEM_LIBINDI=0` (always from-source) or `USE_SYSTEM_LIBINDI=1` (always system, fails if < 2.0).

### macOS

Supported: **macOS 26 Tahoe or newer** on **Apple Silicon (arm64)**. Intel Macs and pre-Tahoe macOS are not supported.

Install build dependencies via [Homebrew](https://brew.sh) (or `./build-dmg.sh --install-deps`):

```bash
brew install cmake wxwidgets cfitsio libnova gettext
./run_dmg.sh --build           # configure + parallel build
```

The bundle is at `tmp/OpenAstro PHD2.app`.

### Windows

Requires Visual Studio 2022 and `git` on `PATH`. x64 only.

```cmd
run_exe.bat                    REM clean configure + build (default)
run_exe.bat -config            REM clean configure only, no build
run_exe.bat -launch            REM build then start phd2.exe
run_exe.bat -help              REM all options
```

Every run wipes `tmp\` and starts clean - matches `run_deb.sh` and `run_dmg.sh` on the other platforms (and the installer script `build-exe.ps1` on this one). Each build takes 10-60 minutes depending on hardware, dominated by vcpkg compiling OpenCV from source.

## Building Installers

Each platform has a packaging script that produces a redistributable artifact:

All three packaging scripts produce a consistently-named artifact: `openastro-phd2-<version>-<arch>.<ext>`.

- **Linux:** `./build-deb.sh` -> `../openastro-phd2-<version>-<amd64|arm64>.deb` plus a tiny `../phd2-alpaca-<version>-all.deb` transitional metadata package. The internal package name is `openastro-phd2` (renamed from `phd2-alpaca` in 2.0.0; `Conflicts`/`Replaces` metadata lets dpkg handle the transition cleanly). For a fresh install just use the main `.deb`; for an apt-managed upgrade from an existing `phd2-alpaca` host, `sudo apt install ./openastro-phd2-<version>-<arch>.deb ./phd2-alpaca-<version>-all.deb` pulls in the new package and lets apt retire the old name automatically.
- **macOS:** `./build-dmg.sh` -> `tmp/openastro-phd2-<version>-arm64.dmg` (bundles every Homebrew dylib into the `.app` so end users don't need Homebrew; unsigned, so first launch requires right-click -> Open).
- **Windows:** `.\build-exe.ps1` -> `tmp\openastro-phd2-<version>-x64.exe` (Inno Setup-driven installer that bundles every vcpkg-built dependency DLL — wxWidgets, OpenCV, curl, libINDI, cfitsio, etc. — so end users don't need vcpkg or VS runtime; unsigned, so Windows SmartScreen will warn on first launch, click "More info" -> "Run anyway"). Requires [Inno Setup 5](https://jrsoftware.org/isinfo.php) at `C:\Program Files\Inno Setup 5\ISCC.exe` (or the `(x86)` path).

**All three build scripts run the full test suite before packaging. If any test fails, no installer is produced.** Fix the failing tests, or pass `-DBUILD_TESTING=OFF` (or the fork's own `-DPHD_BUILD_TESTS=OFF`) at configure time to drop the test build entirely.

## Testing

The fork ships a unit-test suite under `tests/` that runs alongside the upstream Gaussian-process tests under `contributions/MPI_IS_gaussian_process/tests/`. Tests are wired into CMake's `enable_testing()`, so once you've configured a build (any of the three `run_*` scripts) you can run them via `ctest`:

```bash
# from the build directory (tmp/ for the run_*.sh / run_exe.bat scripts)
cd tmp
ctest --output-on-failure                  # all tests
ctest -R test_alpaca_schema -V             # one suite, verbose
ctest -L "Unit tests"                      # by label

# or build & run individual suites directly
cmake --build . --target test_json_parser
./tests/test_json_parser
```

Currently 10 test executables (4 from upstream GP + 6 added by this fork). The fork's suites are read-only and need no devices, network, or wxWidgets - they cover the JSON parser, the event-server JSON-RPC schema downstream consumers depend on (NINA / KStars / web UI), the Alpaca client/discovery JSON contracts, the INDI subnet/scan math, the shared host:port parsing + dedupe model, and math-twin pinning of the simple guide algorithms. See `tests/README.md` for architectural notes and what's deferred.

## License

This project remains under the original PHD2 licensing terms. See [LICENSE.txt](LICENSE.txt) for details.
