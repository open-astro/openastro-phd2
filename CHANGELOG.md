# Changelog

All notable changes to OpenAstro PHD2 will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- Alpaca mount connect failures now show the server's own `ErrorMessage` (e.g. the ZWO/ASI hub's "Connection failed" when the mount is powered off or in use) with a hint to check power/exclusive use, instead of the misleading "HTTP 1031"-style dialog that presented an Alpaca error number as an HTTP status.

### Fixed
- `phd2.exe` failed to launch on clean Windows 10 systems ("VCRUNTIME140_1.dll was not found"): the app-local Visual C++ runtime shipped by the installer was missing `vcruntime140_1.dll` (required by every x64 binary built with VS2019+) and the `msvcp140_1/_2/_atomic_wait/_codecvt_ids` satellites. All nine CRT DLLs are now vendored at a matching version (14.51) and installed alongside the exe, so PHD2 runs without the VC++ redistributable.
- Alpaca camera connect no longer stalls for the full 15-second timeout against servers (e.g. the ZWO/ASI hub) that accept the connect request but never flip the `Connected` property to true: the poll loop now also probes `CameraState` after a 1-second grace period and proceeds as soon as the device is demonstrably alive.
- Windows installer refused to run on ARM64 Windows 11 ("This program does not support the version of Windows your computer is running"). Inno Setup 6.3+ redefined `x64` to mean x64-OS-only; the installer now uses `x64compatible`, which admits native x64 and ARM64 Windows running x64 apps under emulation.

## [2.1.0] - 2026-07-15

### Added
- ASCOM camera support on Windows. Ports upstream `cam_ascom.{cpp,h}` and wires the ASCOM Chooser's registered Camera devices into the New Profile Wizard's camera dropdown, mirroring how the existing mount/rotator paths surface ASCOM drivers. The driver itself lives outside PHD2 — the user installs whatever ASCOM camera driver their hardware vendor ships, and PHD2 talks to it via late-bound COM (no vendor SDK is bundled). Gated by `ASCOM_CAMERA` in `cameras.h`, only enabled when `__WINDOWS__` is defined; the source files compile out on macOS/Linux.

### Changed
- `run_exe.bat` now wipes `tmp\` and starts clean on every run, matching `run_deb.sh` and `run_dmg.sh` (which already did this) and `build-exe.ps1`. The `-rebuild` flag is gone since it's no longer meaningful — every run is a rebuild. Removes the in-place CMake reconfigure path that re-triggered vcpkg's `FetchContent` UPDATE step and could fail on the unqualified `bootstrap-vcpkg.bat` lookup. Cost: every Windows dev build is now 10–60 min instead of 1–5 min incremental; benefit: deterministic, matches the other platforms, no more reconfigure-induced FetchContent failures.
- Camera factory routing in `src/camera.cpp` uses explicit anchors (`StartsWith("INDI Camera")` / `StartsWith("Alpaca Camera")` for the built-in transports, `EndsWith(" (ASCOM)") || StartsWith("ASCOM ")` for ASCOM) instead of the previous loose `Contains()` matches, so a vendor camera whose display name happens to contain "INDI"/"Alpaca"/"ASCOM" anywhere can no longer be misrouted to the wrong factory.

### Fixed
- UBSan failure in the Gaussian Process guider tests: `GP::clearData()` now resets its Cholesky cache via `compute()` on an empty matrix instead of assigning a default-constructed `Eigen::LDLT`, whose uninitialized `m_info` the move-assignment read (flagged as an invalid `ComputationInfo` load on every sanitizer CI run).
- Repo-wide engineering audit follow-up (see `AUDIT.md`). Event server: added the missing `return` in `capture_single_frame` (fell through to a null `pCamera` deref), null-guarded `set_algo_param`'s `value`, guarded `capture_master_dark_frame` against a mid-build frame-size change overrunning the accumulator, fixed truncate-before-scale in `set_variable_delay_settings`, and made `url_decode` UTF-8-safe and tolerant of malformed `%` sequences. INDI: hopped the off-thread wx UI mutations and self-joining `Disconnect()` calls in `INDIConfig`/`CameraINDI`/`ScopeINDI`/`RotatorINDI` disconnect paths onto the main thread, fixed the `SetButtonEvent` text-property sizing, closed the video-stacking dangling-pointer race, initialized/atomicized the rotator `modal` flag, de-duplicated the Dual-CCD combo, guarded reentrant `wxYield` loops, null-checked `indi_gui` property lookups, and fixed the BLOB format termination and a lost `sync_cond` wakeup. ASCOM COM: added a VARIANT `vt` check before walking the image SafeArray, null-guarded status getters callable while disconnected, fixed per-frame SAFEARRAY/VARIANT/EXCEPINFO leaks (added `Variant`/EXCEPINFO cleanup), unregistered the rotator driver on disconnect, cleaned up after a failed camera connect, and balanced `CoInitializeEx` in the worker thread. Alpaca: took the shared response buffer read under the mutex in the client accessors and clarified the subframe row index. Fixed the `-DBUILD_TESTING=OFF` documentation flag so it actually disables the suite.
- Second-round review follow-up: INDI camera/mount/rotator `ExecInMainThread` disconnect lambdas now carry a shared alive flag cleared in the destructor, so a lambda dispatched after the device object is destroyed is a no-op instead of dereferencing a dangling `this`. The `SlewToCoordinates`/`OnDiscover` reentrancy flags are now RAII-reset so a future early return can't permanently lock out slewing or discovery. `AlpacaClient::Get()` gained a `rawResponse` out-parameter captured under the client mutex so accessor error logging can't race a concurrent request's buffer reuse. `url_decode` falls back to a lossless 8-bit decode (with a debug log) when a query parameter isn't valid UTF-8 instead of silently returning an empty string.
- Third-round review follow-up: `INDIConfig` now carries the same destructor-cleared alive flag as the INDI device classes, so its `newDevice`/`newProperty` main-thread lambdas can't dereference a destroyed dialog. The ASCOM `ImageArray` guard accepts 16-bit (`I2`/`UI2`) SAFEARRAYs from older non-conformant drivers via a typed read path instead of rejecting them, and logs a specific unsupported-element-type message. `AlpacaClient::Put()` gained the same mutex-held `rawResponse` out-parameter as `Get()`. Pinned the Linux CI runner to `ubuntu-24.04` and documented `url_decode`'s pass-through handling of malformed `%` escapes.
- Event server `get_algo_param`/`set_algo_param` now null-check the guide algorithm before dereferencing it, so an RPC call issued before an algorithm is configured returns a JSON-RPC error instead of crashing.
- `INDIConfig::OnDiscover` re-enables the dialog and Discover button via the same RAII guard that resets the reentrancy flag, so an early return or exception can no longer leave the dialog permanently disabled. The discovery and synchronous-slew reentrancy flags are now per-instance members instead of process-global statics.
- `url_decode` now validates UTF-8 itself (rejecting overlongs, surrogates, and out-of-range code points) instead of probing `wxString::FromUTF8`'s build-dependent failure behavior, so the lossless 8-bit fallback triggers deterministically on every wx build. `set_variable_delay_settings` caps delays at 3600 seconds so the millisecond conversion can't overflow.
- API reference README error-code range corrected to Alpaca `ErrorNumber` 0x400–0xFFF → COM HRESULT 0x80040400–0x80040FFF (was mislabeled as only 0x400–0x4FF).

### Removed
- Dead scope backends left over from upstream (`scope_voyager`, `scope_eqmac`, `scope_equinox`, `scope_GC_USBST4`, `scope_gpusb`, `scope_gpint`) whose feature guards are never defined, their now-empty `#ifdef` blocks in `scope.cpp`, and the dead `V4L_CAMERA` menu/event code. Removed leftover proprietary vendor blobs that contradicted the no-bundled-SDK design (`SBIGUDrv.framework`, `fcCamFw.framework.zip`, `MallincamGuider` source zip, `FindZWO.cmake`, and the `build/unpack_*` vendor-SDK helpers).

### CI / packaging
- macOS builds now install Eigen via Homebrew (`release.yml`, `build-dmg.sh` dependency list, `run_dmg.sh` hint) — required since the audit switched non-Windows builds to the system Eigen3 package; without it the DMG leg failed at CMake configure.
- New fast PR check suite (`checks.yml`): clang-tidy and cppcheck scoped to the lines/files each PR changes, ShellCheck over the repo's shell scripts, a trojan-source Unicode scan, JavaScript syntax checking of inline `<script>` blocks, and a zizmor security audit of the workflows themselves. The clang-format check now also scopes to changed lines on PRs (full repo-wide check remains on pushes to `main`).
- New `codeql.yml`: CodeQL semantic security analysis (security-extended query suite) of the C++ tree — post-merge on `main`, weekly, and on demand; findings surface in the Security tab. Deep path-sensitive static analysis (the Clang Static Analyzer / `clang-analyzer-*`) is CodeQL's job post-merge; the PR clang-tidy job stays fast, running only the cheap `bugprone-*`/`performance-*` lints on changed lines.
- New `sanitizers.yml`: ASan+UBSan and TSan (concurrency stress, `ctest --repeat until-fail:5`) builds on native arm64 runners — post-merge on `main`, weekly on Mondays, and on demand; not on PRs, keeping PR CI compile-free.
- Dependabot now bumps the SHA-pinned GitHub Actions weekly in a single grouped PR. All actions across the workflows are pinned by commit SHA (zizmor's `unpinned-uses` audit enforces this going forward).
- New manual-trigger `release.yml` workflow that builds all four release installers on GitHub-hosted runners — Debian amd64, Debian arm64 (native ARM runner), Windows x64 (Inno Setup, with the static wxWidgets build cached), and macOS Apple Silicon DMG — and attaches them to a draft GitHub Release. Kicked off by hand from the Actions tab or by pushing a `v*` tag; nothing is published until the draft is reviewed and published manually.
- The Linux build + `ctest` workflow no longer runs on pull requests — the full compile slowed PR iteration. PRs get the fast checks (clang-format, Claude review); the compile runs as a post-merge safety net on pushes to `main` and can be run on any branch via `workflow_dispatch`.
- Added a Linux build + `ctest` GitHub Actions workflow (previously nothing compiled or tested on PRs). Pinned `actions/checkout` by SHA and de-duplicated the clang-format trigger. Honored the vcpkg commit pin on Windows (`run_exe.bat` was building vcpkg HEAD), added a verified `URL_HASH` for the googletest download, fixed the `run_exe.bat` delayed-expansion bug, made the macOS DMG Finder-layout step non-fatal with staging cleanup, hardened `build-dmg.sh` dylib bundling, stopped `debian/rules` from shipping host INDI libs, required wxWidgets 3.2 in `debian/control`, taught `build-exe.ps1` to find Inno Setup 6 and validate `WXWIN`, and refreshed stale repo URLs/branding.

### Commit References
- `2091c7b3` - fix: avoid uninitialized LDLT read flagged by UBSan in GP::clearData
- `035e02ce` - Bump the actions group with 5 updates (#18)
- `95b92139` - ci: fast PR checks, sanitizer runs, dependabot, and manual CD for all release targets (#17)
- `56cf69aa` - Merge pull request #16 from open-astro/fix/audit-findings
- `c4093caa` - review: deterministic url_decode UTF-8 fallback; bound delay settings
- `ed2f72ca` - review: RAII dialog re-enable and per-instance reentrancy flags
- `32b485d3` - review: null-check guide algorithm in get/set_algo_param RPCs
- `d2774ab6` - review: address Claude review round-3 findings on PR #16
- `ae869134` - review: address Claude review round-2 findings on PR #16
- `9ce3fe3d` - review: address Claude review feedback on PR #16
- `d11dfc06` - ci: cap build parallelism (OOM) and revert claude-review.yml to main
- `abb95e3c` - ci: fix formatting, restore id-token for review, use INDI PPA
- `5620ee8a` - build(win): call vcpkg bootstrap/install by absolute path
- `65818f42` - Fix audit findings across source, build, packaging, CI, and docs
- `6591b4af` - Merge pull request #15 from open-astro/ci/add-claude-review
- `9eb34ffc` - ci: add Claude Code automated PR review workflow
- `a9d2e7f6` - docs: add headless-port playbook + tracking files (Phase 0) (#1)
- `708fe87d` - Merge pull request #13 from open-astro/add/ascom-camera-win
- `6dde3611` - Address CodeRabbit round 2 on PR #13
- `4c8bf7ca` - Address CodeRabbit review on PR #13
- `7d939d35` - Add Windows ASCOM camera support, make run_exe.bat always wipe tmp

## [2.0.0] - 2026-05-15

### Added
- Unit test suite covering the API contracts and pure math the fork ships, wired into `enable_testing()` so `ctest` runs alongside the existing GP tests. New `option(PHD_BUILD_TESTS ON)` defaults the suite ON so `build-deb.sh` (via `dh_auto_test`), `build-dmg.sh`, and `build-exe.ps1` all pick it up automatically — pass `-DPHD_BUILD_TESTS=OFF` to skip. Six new test executables under `tests/`: `test_json_parser` (the deserializer that backs every Alpaca response and event-server inbound RPC); `test_jsonrpc_schema` (contract test for the documented event-server message shapes — `Version`, `AppState`, `SettleDone`, `Settling`, `StarSelected`, JSON-RPC envelope, `get_profiles`, `get_profile`, `get_lock_shift_params` — so a downstream-visible field rename surfaces at PR time instead of via a Discord report); `test_alpaca_schema` (Alpaca standard envelope, `ErrorNumber`/`ErrorMessage` extraction including the float-coerced lenient path, camera/telescope `Value` shapes, the property-name fallback used when servers omit the standard wrapper, the management API `apiversions`/`configureddevices` shapes, the discovery UDP `{"AlpacaPort": N}` reply); `test_discovery_logic` (host:port parse contract and the `std::set<wxString>` dedupe model shared by `AlpacaDiscovery` and `INDIDiscovery`); `test_indi_discovery` (INDI-only subnet enumeration math — prefix clamping `16..30` with `/24` fallback, mask construction, loopback skip, scan-range network/broadcast skip, plus the "always probe `127.0.0.1`" contract from the 1.3.0 loopback fix that lets headless Pi setups discover a same-box INDI server); `test_guide_algorithm_math` (math-twin pinning of `identity`, `hysteresis`, and `resistswitch` `result()` curves, with the production line numbers referenced in test comments so any formula edit forces a deliberate update on both sides). Build infra uses a `tests/include/phd.h` shadow + `-include` force-include to neutralise `src/phd.h`'s wxWidgets transitive pull on test targets that don't need it. New `tests/README.md` documents the per-suite test style (real / fixture-contract / model / math-twin) and the deferred items: Lowpass/Lowpass2/ZFilter (need `WindowedAxisStats` / `ZFilterFactory`), star detection (needs `usImage`), calibration math (needs `Mount` / `Scope`), and end-to-end testing of `CameraAlpaca::Capture()`'s bounded-retry fix from e7a91ddc.
- `/release` slash command that bumps `version.md`, closes `## [Unreleased]` into a numbered version section, gathers commit references via `git log`, and creates the release commit.
- macOS Apple Silicon (arm64) build support. New `run_dmg.sh` and `build-dmg.sh` at the project root mirror the Linux/Windows pattern (`run_deb.sh` + `build-deb.sh`, `run_exe.bat` + `build-exe.ps1`). `build-dmg.sh` produces a fully self-contained `PHD2-<version>-macOS-arm64.dmg`: 15 Homebrew dylibs (cfitsio, wxWidgets 3.3, image libs, pcre2, sharpyuv via @rpath) are recursively copied into `PHD2.app/Contents/Frameworks/` and their install names rewritten with `install_name_tool` so end users don't need Homebrew installed to run the app.
- macOS DMG "drag to /Applications" install window. `build-dmg.sh` now builds a writable scratch DMG, lays out an icon-view Finder window via AppleScript (700×600 logical, 128px icons), drops in an `/Applications` symlink as the drag target, and uses a custom background image at `packaging/macos/background.png` so users see the OpenAstro branding next to the install layout instead of a bare Finder window.
- macOS dark mode support. `Info.plist.in` opts in via `NSRequiresAquaSystemAppearance=false` so window chrome, dialogs, menus, and controls follow System Settings → Appearance instead of being forced into the legacy light Aqua theme.
- Alpaca rotator support on macOS (`ROTATOR_ALPACA` enabled in `rotators.h`) and Alpaca guide-output support on macOS (`GUIDE_ALPACA` added to `scopes.h`), matching the fork's "Alpaca + INDI only" philosophy.
- `debian/control` ships a `phd2-alpaca` transitional metadata package alongside `openastro-phd2`. It is `Architecture: all`, owns no files, and depends on `openastro-phd2 (= ${binary:Version})`. Existing `phd2-alpaca` users who run an apt-managed upgrade get pulled into `openastro-phd2` automatically without first having to `apt remove phd2-alpaca`; `build-deb.sh` produces a `phd2-alpaca-<version>-all.deb` next to the main `openastro-phd2-<version>-<arch>.deb`.

### Changed
- All three packaging scripts now hard-fail on a single failing test, with no per-script bypass. `build-exe.ps1` previously emitted a warning and continued past `ctest` failures (silently shipping installers built off red tests); it now `exit 1`s, with the same behaviour if `ctest` itself can't be located. `build-dmg.sh` lost its `--skip-tests` flag entirely. `debian/rules` gained an `override_dh_auto_test` that calls `ctest --output-on-failure` directly on the `obj-*-linux-*` build dir instead of routing through `dh_auto_test`, so `DEB_BUILD_OPTIONS=nocheck` no longer skips the suite. The only kill-switch on every platform is now `cmake -DPHD_BUILD_TESTS=OFF`, which has to be passed deliberately.
- `/commit` slash command now appends an entry to `## [Unreleased]` in `CHANGELOG.md` so release notes accumulate per-commit instead of being backfilled at release time.
- macOS minimum deployment target raised `10.14` → `26.0` (Tahoe). Matches the fork's pattern of dropping legacy floors and lets the build delete the now-dead "Sonoma+" conditional code in `run_cmake-osx` / `build/build-mac`. Tahoe also drops the last Intel Macs from Apple's supported list, aligning with the arm64-only target.
- macOS scripts restructured: `build/build-mac` and `run_cmake-osx` removed; `run_dmg.sh` and `build-dmg.sh` at the project root are now the entry points (mirroring `run_deb.sh` + `build-deb.sh`). `MacOSXBundleInfo.plist.in` renamed to `Info.plist.in` to match Apple's canonical bundle filename.
- macOS build wiring: `cfitsio` now links against the Homebrew dylib (the previous static `.a`-only constraint matched no Homebrew bottle); `libnova` lookup drops the hardcoded `/usr/local/lib` path (Intel Homebrew) in favor of `CMAKE_PREFIX_PATH`, so the same code path works on `/opt/homebrew` (arm64) and `/usr/local` (Intel); zlib explicitly added to the macOS link line for INDI 2.x's `basedevice.cpp` (`uncompress`); Eigen3 include path now queried from the `Eigen3::Eigen` target so Eigen 5.x (which dropped `EIGEN3_INCLUDE_DIR`) works.
- "OSX" terminology cleanup in CMake comments / section headers and script names — Apple retired the "OS X" branding in 2016. User-facing wire-protocol identifiers (`PHD_OSNAME`, update-check URL paths) deliberately left unchanged to avoid breaking update notifications for existing installs.
- macOS app bundle rebranded "PHD2" → "OpenAstro PHD2". Bundle name, identifier (`org.openphdguiding.phd2` → `net.openastro.phd2`), icon (`PHD_OSX_icon.icns` → `AppIcon.icns` with new OpenAstro raspberry artwork), and copyright string all updated. `run_phd2_macos` updated to match the new bundle ID when toggling `NSAppSleepDisabled`.
- INDI/Alpaca server discovery moved off the main thread. `INDIConfig::OnDiscover` and `AlpacaConfig::OnDiscover` now run `DiscoverServers()` via `std::async` and pump the UI event loop while waiting, instead of blocking the GUI thread for the 2–4s discovery window. On macOS that block was over the beachball threshold and the dialog visibly froze; the cursor is also forcibly held to the standard pointer each tick so wxOSX/AppKit doesn't swap it for the wait/watch cursor mid-discovery.
- INDI device setup dialogs (`CameraINDI`, `ScopeINDI`, `RotatorINDI`) no longer auto-connect to the configured server when the dialog opens. Auto-connect blocked the main thread for 3–6s waiting for a server that often wasn't running — on macOS surfaced as a beachball that users perceived as a hang. The dialog already has its own Discover Servers and Connect buttons; users now trigger the connection deliberately.
- `README.md` reorganized around a Run / Build Installers / Test structure with the platforms listed Linux → macOS → Windows in each section. The "all three packaging scripts run the full test suite and abort on failure" gate from 94c80630 is now called out explicitly at the top of the Build Installers section instead of buried in a parenthetical, and the lower-priority verbose preamble was trimmed.

- Linux build scripts (`build-deb.sh`, `run_deb.sh`) now target Debian 13 Trixie / Raspberry Pi OS Trixie only, with fail-fast architecture checks rejecting hosts that aren't amd64 or arm64.
- Windows build modernized: C++14 → C++20, and wxWidgets minimum pinned to 3.2 (was unpinned and silently accepted 3.0). `run_win.bat` now configures with `-A x64`.
- C++20 conformance fixes across the source tree: ternary common-type ambiguities resolved in seven files (camera, gear_dialog, image_math, mount, myframe, rotator, guide_algorithm_gaussian_process); `DispatchObj` / `DispatchClass` in `comdispatch.h`/`.cpp` now take `const OLECHAR *` to satisfy `/Zc:strictStrings`; missing `#include "CVTrace.h"` added to `thirdparty/VidCapture/Source/VidCapture/CVImage.h` so the `CVAssert` macro is visible to `/permissive-`'s template-body parsing.
- About dialog refreshed: Joey Troy is the sole Project maintainer; Andy Galasso and Bruce Waddington moved to Past maintainers alongside Craig Stark and Bret McKee; `Copyright 2026 OpenAstro` added to the copyright list.
- Windows build wiring rewired for x64-only: `thirdparty.cmake` replaces the `WINDOWS_ARCH` x86/x64 detection with a hardcoded `x64` plus a configure-time `FATAL_ERROR` if `-A Win32` (or anything else) is passed; the VidCapture compile block, the x86 wxWidgets lib-dir branch, and the x86-only `msvcr120.dll` copy are all gone. `build-installer.ps1` and `build/build-win` updated for x64-only — `build/build-win`'s `-a arch` option removed since there's only one architecture. `phd2-x86.iss.in` renamed to `phd2.iss.in`.
- vcpkg pin bumped `2024.11.16` → `2026.03.18` (18 months of port updates in one shot). Notable bundled-library bumps: cfitsio `3.49` → `4.6.3` (major version), curl `8.18.0` → `8.19.0`, plus refreshed OpenCV 4.x and Eigen3. The 2026.03.18 release also carries a security fix for OpenSSL on Windows (ZDI-CAN-29616).
- libINDI ExternalProject bumped from `v2.1.6` to `v2.2.1.1` (current active 2.2 maintenance line; the 2.1 line dead-ended at v2.1.9). v2.2.1.1 is a same-day hotfix on top of v2.2.1. Verified end-to-end against the INDI-DEV OVA with a Player One camera — server discovery, device connect, and frame capture all clean. This is the line that contains the meaningful Player One driver fixes the 1.3.0 INDI-1.9-removal commit was alluding to.
- GoogleTest FetchContent pin bumped `v1.14.0` → `v1.17.0` (~33 months of test framework updates in one step). Required floor moves to C++17 (we're on C++20, so already covered). All four existing test executables (`GaussianProcessTest`, `MathToolboxTest`, `GPGuiderTest`, `GuidePerformanceTest`) build and run against the new version.
- macOS app bundle file renamed `PHD2.app` → `OpenAstro PHD2.app`. The CMake `OUTPUT_NAME` for the bundle target is now `"OpenAstro PHD2"`, which sets both the `.app` filename (the only string visible to Finder / Spotlight / Launchpad without a `CFBundleDisplayName`) and the binary inside `Contents/MacOS/`. Pre-2.0.0 the bundle was named `PHD2.app` even though `CFBundleName` was `"OpenAstro PHD2"`, so Finder displayed `PHD2` and on-disk it collided with upstream PHD2's bundle file path in `/Applications`. `run_phd2_macos` updated to exec the renamed binary, and `build-dmg.sh` routes every reference through a single `APP_BUNDLE` variable so the spaces stay quoted consistently.
- DMG volume label renamed `PHD2` → `OpenAstro PHD2`. Volume now matches the bundle's display name in Finder / `/Volumes/...` when the DMG is mounted.
- Windows installer is now built as a 64-bit Inno Setup process via `ArchitecturesAllowed=x64` + `ArchitecturesInstallIn64BitMode=x64` in `phd2.iss.in`. Without these directives a 64-bit-only payload (which our `phd2.exe` is) was installing under `C:\Program Files (x86)\OpenAstroPHD2` — the WoW64 redirect target for 32-bit installers — instead of the real `C:\Program Files\OpenAstroPHD2`. The installer also now refuses to launch on 32-bit Windows entirely.
- wxConfig moved out of the upstream PHD2 namespace so OpenAstro PHD2 2.0.0 can be installed side-by-side with the official PHD2 without sharing settings or clobbering the other app's data on uninstall. wxApp `VendorName` `StarkLabs` → `OpenAstro`; wxApp `AppName` `PHD2`/`phd2` → `OpenAstroPHD2`/`openastro-phd2`; wxConfig path `PHDGuidingV2` → `OpenAstroPHD2`. Resulting locations: `HKCU\Software\OpenAstro\OpenAstroPHD2` (Windows), `~/.OpenAstroPHD2` (Linux), `~/.OpenAstroPHD2` / per-bundle prefs (macOS). `phd2.iss.in` registry block updated to target the new path so the Inno Setup uninstaller's `uninsdeletekey` no longer touches `HKCU\Software\StarkLabs\PHDGuidingV2` — uninstalling OpenAstro PHD2 used to wipe upstream PHD2's settings, which it no longer does. New `PhdConfig::PhdConfig()` runs a one-shot copy from the legacy `StarkLabs/PHDGuidingV2` store the first time the new store is empty so OpenAstro PHD2 1.3.0 users' profiles, calibration, and per-equipment settings carry forward automatically. The legacy store is read-only during migration; upstream PHD2 keeps its own copy of the settings intact on machines where both apps are present.
- Unified artifact naming across all three platforms: `openastro-phd2-<version>-<arch>.<ext>` for the `.deb`, `.dmg`, and `.exe`. Previously each platform used its own convention (`PHD2-2.0.0-macOS-arm64.dmg`, `phd2-alpaca_2.0.0_amd64.deb`, `PHD2-2.0.0-x64.exe`); now they share a single recognizable prefix. macOS DMG name updated in `build-dmg.sh`. Windows installer name + Inno Setup `APP_NAME`/`DefaultDirName`/`DefaultGroupName`/`OutputBaseFilename` updated in `build-exe.ps1` and `phd2.iss.in`. Linux `.deb` is renamed post-build to convert dpkg's underscores into hyphens (the package internal name is still underscored per dpkg's filename convention).
- Linux package renamed `phd2-alpaca` → `openastro-phd2`. Debian source/binary package name, system user, install paths (`/usr/lib/openastro-phd2`, `/var/lib/openastro-phd2`), systemd unit (`debian/openastro-phd2.service`), and installed wrapper/binary names all match. `Conflicts: phd2-alpaca` + `Replaces: phd2-alpaca` in `debian/control` blocks side-by-side installs and lets dpkg handle the upgrade cleanly. `phd2.sh.in`'s `LD_LIBRARY_PATH` now prefers `lib/openastro-phd2` (where the bundled INDI 2.x dylibs land). Existing `phd2-alpaca` users need a `sudo apt remove phd2-alpaca` before installing the new package — the package name change crosses dpkg's identity boundary, not a transparent upgrade (the transitional `phd2-alpaca` package above is the apt-managed path).
- `debian/rules` `override_dh_auto_test` now resolves the cmake build dir deterministically: tries `obj-$(DEB_HOST_GNU_TYPE)` first (the canonical path `dh_auto_configure` creates), and only falls back to a **sorted** `obj-*-linux-*` glob if that's missing. Previously used `ls -d obj-*-linux-* | head -1` which would pick an arbitrary directory if cross-build residue from a prior arch was sitting in the tree, silently invalidating the test gate.
- README "Building Installers" Windows entry expanded with output filename, bundled-dependency list (vcpkg DLLs: wxWidgets, OpenCV, curl, libINDI, cfitsio), unsigned/SmartScreen caveat, and Inno Setup install path — matching the level of detail the macOS entry already had.
- `.gitignore` now excludes `debian/openastro-phd2/`, the per-binary-package staging dir `dh_install` creates during `dpkg-buildpackage`. The old `debian/phd2-alpaca/` entry was orphaned by the source-package rename and no longer matched anything the build emits, so a clean build under the new package name was leaving the staging tree visible as untracked noise in `git status`.

### Removed
- Ubuntu PPA guidance, wxWidgets 3.0 fallback paths, and armhf/i386 build support from the Linux build scripts.
- 32-bit Windows build target. `run_win.bat` now passes `-A x64`.
- `WinLibs/x86/` — 9 32-bit redist DLLs and legacy libraries (`msvcr120.dll` VS2013 runtime, `inpout32.dll` legacy port I/O, `wxVidCapLib_wx29`).
- `thirdparty/VidCapture/` — vendored DirectShow video-capture library (~2003), only compiled on Windows x86 and unreferenced from `src/`.
- `upload.cmd` — legacy buildbot upload script targeting openphdguiding.org's `phd2buildbot` putty session; not used by this fork's release flow.
- `upload.sh` and `build/make-release` — Linux/macOS twins of the already-removed `upload.cmd`. Same `phd2buildbot@openphdguiding.org` SFTP/buildbot infrastructure that this fork doesn't use.
- `APPLE32` branch in `cmake_modules/compiler_options.cmake` (forced i386 architecture; macOS dropped i386 in 10.15) and the QuickTime framework reference in `thirdparty/thirdparty.cmake` (32-bit-only framework removed in macOS 10.15).
- Legacy guide-output drivers on macOS: `GUIDE_GPUSB`, `GUIDE_GCUSBST4`, `GUIDE_EQUINOX`, `GUIDE_EQMAC` defines dropped from `scopes.h`. USB-direct outputs that haven't worked on modern macOS in years and aren't reachable from this fork's Alpaca + INDI guide-output story.
- `IOMainPort` / `IOMasterPort` compatibility shim in `serialport_mac.cpp`. `IOMainPort` was introduced in macOS 12 Monterey; the fork's minimum is now 26 Tahoe.
- `build/build.cfg.sample` — orphaned template referencing the long-removed `build/build-{linux,win,mac}` driver scripts.
- `thirdparty/include/libdc1394-2.2.2/config.h` — autoconf-generated header for the vendored libdc1394 build; unreachable since vendored libdc1394 was retired.
- Intel Mac support. The macOS build is now arm64-only — there is no universal binary, no Rosetta path. Intel users on this fork's Mac path are no longer supported.
- libusb dependency and the vendored `openssag` StarShoot AutoGuider driver. Neither is reachable from `src/` after the 1.2.0 native-camera-SDK cleanup; this fork's cameras come over Alpaca / INDI / ASCOM, none of which need direct USB. Drops `libusb-1.0-0-dev` and `libudev-dev` from Debian build deps, drops `USE_SYSTEM_LIBUSB` from CMake options and `run_deb.sh`, drops the macOS `libusb_openphd.dylib` framework-copy step, and removes 27 vendored files (`libusb-1.0.21.tar.bz2`, `thirdparty/include/libusb-1.0.21/`, entire `thirdparty/openssag/` tree) plus ~140 lines of CMake wiring.
- `phd2-x64.iss.in` — orphan upstream-era Inno Setup template at the project root (still branded `PHD 2`, install dir `PHDGuiding2`, no OpenAstro fixes). Nothing referenced it; `build-exe.ps1` only ever read `phd2.iss.in`. Leftover from before `phd2-x86.iss.in` was renamed to `phd2.iss.in` in the 2.0.0 modernization.

### Fixed
- Dialogs and tool windows opened in random places (often the primary display) instead of next to the main PHD2 window — frustrating on multi-monitor setups. Every `wxDialog` / `wxFrame` subclass in `src/` now centers on its parent: About, Advanced/Preferences, Manual Guide, Calibration Assistant, Build Dark Library, Refine Bad-pixel Map, Connect Equipment, profile wizard + its `ConnectDialog` sub-dialog, `INDIConfig`, `AlpacaConfig`, `IndiGui`, calibration-assistant inner dialogs (Custom / Sanity / Explanation), slit-properties dialog, backlash graph, calstep dialog, calreview dialog, camera-cal import dialog, confirm dialog, manual cal dialog, star-cross test dialog, Gaussian-process expert dialog, log uploader, auto-updater, pier-flip cal tool, scope-pointing dialog, new-profile dialog. `MyFrame::PlaceWindowOnScreen()` (the helper that restores cached tool-window positions) also falls back to `CentreOnParent()` instead of primary-display center when the saved coords are off-screen.
- Profile wizard's Camera and Mount selectors fell through to whatever sorted first alphabetically (Alpaca) when no choice had been made, making the wizard look pre-selected and forcing users to click Alpaca a second time to actually open the configure flow. Both now default to `None`, matching the AuxMount / AO / Rotator / Switch defaults.
- INDI Options dialog (`IndiGui`) wedged the app when closed via the window's X button while running modal — `Show(false)` hid the dialog without ending the modal loop, leaving the app in a state where every click beeped because an invisible modal dialog was "active." Now always `EndModal(wxID_CANCEL)` in the modal case.
- INDI server discovery missed servers bound only to loopback. `INDIDiscovery::DiscoverServers()` now always probes `127.0.0.1:7624` as a unicast target. `EnumerateLocalSubnets()` skips the loopback interface by design (IFF_LOOPBACK on Linux/macOS, 127.0.0.0/8 check on Windows), so when PHD2 and the INDI server both run on the same Linux box — the typical Raspberry Pi setup — the local server was invisible to the Discover Servers button. Same shape of bug as the 1.3.0 `AlpacaDiscovery::BuildBroadcastTargets()` loopback fix.
- Stale `https://github.com/open-astro/phd2-alpaca` Homepage URLs in `debian/control` and `debian/phd2-alpaca.service` corrected to `https://github.com/open-astro/phd2-alpaca-indi` so apt's package metadata and the systemd unit's Documentation field point at the right repo.
- Visual Leak Detector link-directories path in `CMakeLists.txt` updated from `lib/win32` to `lib/Win64` so VLD actually links when present on an x64 build.
- Debian builds against the new INDI 2.2.1.1 failed at configure with `Could NOT find GSL`. INDI 2.2 introduced `INDI_BUILD_COMMON` (defaults `ON`) which pulls driver-development deps (GSL, USB1, JPEG, Nova, Iconv) that this fork's client-only build doesn't need. Pass `-DINDI_BUILD_COMMON=OFF` in the INDI `ExternalProject_Add` so the dep block is skipped.
- Stale "INDI 2.1.6" references in `build-deb.sh`, `run_deb.sh`, `debian/rules`, and `README.md` updated to "INDI 2.2.1.1" to match the actual pinned source build.
- `build-deb.sh` install instructions and `install_deps` apt-get command still listed `libusb-1.0-0-dev` and `libudev-dev` after those were removed from `debian/control`. Now match the actual build requirements.
- `debian/rules` no longer passes the obsolete `-DUSE_SYSTEM_LIBUSB=1` to `dh_auto_configure`; the option was removed from the CMake project alongside libusb itself.
- x64 architecture guard in `thirdparty/thirdparty.cmake` now also checks `CMAKE_SIZEOF_VOID_P` in addition to `CMAKE_GENERATOR_PLATFORM`, so 32-bit toolchains can't slip through when using Ninja or Unix Makefiles (where `CMAKE_GENERATOR_PLATFORM` is empty).
- Fenced code blocks in `.claude/commands/release.md` now carry language tags (`text` / `markdown`) to satisfy markdownlint MD040.
- Alpaca camera hung PHD2 when Stop was pressed during Looping against servers that ack `AbortExposure` but never flip `imageready` to true (e.g. ASCOM Remote Server forwarding a `StopExposure`-only driver). `CameraAlpaca::Capture()` now sends the abort exactly once per exposure and caps the post-abort wait at 3 s before bailing out, instead of hammering the server every poll iteration until the user force-quits. Fixes #7.
- `GPTest.drawSamples_prior_covariance_test` was flaky — the test draws 20 000 samples and compares empirical to analytical covariance within a 2e-1 tolerance, but Eigen's `setRandom()` (used by `math_tools::generate_normal_random_matrix`) reads from `std::rand()`, so run-to-run variation occasionally pushed the empirical covariance outside the band. The `GPTest` fixture constructor now seeds `std::srand(1)`, making every statistical-expectation test in the fixture reproducible across runs. Test-only change. Fixes #8.
- `GPTest.drawSamples_prior_covariance_test` then failed deterministically on the Debian build (passed on Windows/macOS) after the srand seed went in. `std::rand()`'s sequence is platform-specific (glibc vs MSVCRT vs BSD libc), so seed 1 yields a different Monte Carlo realization on each platform; with N=20000 samples the empirical-covariance SE is ~0.15 and the 2e-1 tolerance was only ~1.35σ wide, which glibc's draw happened to land just outside (15.004 vs 14.778, diff 0.226). Loosened the tolerance to 5e-1 (~3.4σ) so the test stays meaningful while tolerating cross-platform `std::rand()` differences under the fixed seed. Test-only change.
- `build-exe.ps1` failed to parse on Windows PowerShell 5.1 after the test-gate commit (94c80630). Two em-dashes (U+2014) were added to a comment and a `Write-Error` string; PS 5.1 reads UTF-8-without-BOM `.ps1` files as CP1252, where the em-dash's UTF-8 bytes (`E2 80 94`) decode to a trailing right-curly-quote that the parser treats as a premature string terminator, producing misleading "missing closing '}'" errors pointing at unrelated lines downstream. Replaced both em-dashes with ASCII hyphens. PowerShell 7+ wouldn't have hit this because it defaults to UTF-8.
- `build-exe.ps1` aborted with `CTest not found at 'ctest.exe'` even when `ctest` was on `PATH`. `Get-Command ctest` returns a `CommandInfo` object whose default string form is just the command's `.Name`; the script then called `Test-Path` on that, which looked for a literal `ctest.exe` in the current directory. Now extracts `.Source` when `Get-Command` succeeds so `Test-Path` and the invocation see the resolved full path.
- `build-exe.ps1` packaging step aborted with `Source file ... msvcr120.dll does not exist`. `phd2.iss.in` still listed `msvcr120.dll` — the **Visual C++ 2013** C runtime — but the fork builds with VS 2022, which produces the VC 14.x runtime (`vcruntime140.dll` / `msvcp140.dll`) instead. Removed the dead entry and added a comment noting the absence is intentional so a future maintainer doesn't add it back.
- Test binaries on Windows failed to compile with `Cannot open include file: 'phd_version.h'`. `tests/CMakeLists.txt` set `target_compile_options(... -include <shadow phd.h>)`, which is GCC/Clang force-include syntax; MSVC silently emits warning D9002 and ignores it, so the shadow header was never active and the real `src/phd.h` won normal lookup (and pulled in `phd_version.h`, generated only into the main build target's include path). Branched on `MSVC` to emit `/FI<file>` instead, which is MSVC's force-include flag.
- Linux desktop launcher did nothing on a fresh install. `phd2.desktop` referenced the pre-rename binary name (`Exec=phd2`, `Icon=phd2`, `StartupWMClass=phd2.bin`), all of which became invalid when `debian/rules` renamed the installed binary to `openastro-phd2` / `openastro-phd2.bin`. Renamed the file to `openastro-phd2.desktop`, updated all three fields, and renamed `phd2.appdata.xml` → `openastro-phd2.appdata.xml` (plus its internal `<id>` and `<launchable>` AppStream refs) so GNOME Software / KDE Discover index us under the new app id. `cmake_modules/PHD2Packaging.cmake` install paths now drop `/usr/share/applications/openastro-phd2.desktop`, `/usr/share/pixmaps/openastro-phd2.png`, and `/usr/share/metainfo/openastro-phd2.appdata.xml` so the desktop file's `Icon=openastro-phd2` resolves via XDG icon theme lookup.
- Could not run OpenAstro PHD2 and upstream PHD2 simultaneously on Windows. The second app to launch hit `wxLogError("PHD2 instance 1 is already running")` even though their wxApp `AppName` values are distinct (`OpenAstroPHD2` vs `PHD2`). Root cause: `src/phd.cpp` constructed the `wxSingleInstanceChecker` lock *before* calling `SetAppName()`, so `GetAppName()` fell back to the executable basename — and on Windows the .exe is still `phd2.exe` in both apps (we relocate it under `OpenAstroPHD2\` at install time, the file itself keeps its short name). Both apps therefore took a lock named `"phd2.1"` and collided. Reordered so `SetVendorName` / `SetAppName` run first; the lock is now named `"OpenAstroPHD2.1"` on Windows/macOS and `"openastro-phd2.1"` on Linux — distinct from upstream's `"phd2.1"` / `"PHD2.1"` on every platform.
- `build-dmg.sh` failed on macOS Tahoe with `text: command not found` followed by `Finder got an error: Can't get disk "PHD2" (-1728)`. The first error was bash interpreting backticks in an AppleScript comment (`` `text color` ``) as command substitution inside the unquoted heredoc, replacing them with empty strings — noisy but non-fatal. The second was the real failure: `hdiutil attach` returns once the kernel mount lands, but Finder only learns about the volume asynchronously via diskarbitrationd, and on Tahoe the gap between mount and Finder-readiness is long enough that the immediately-following `tell disk "PHD2"` raised -1728. Both fixed: the comment now uses quoted text instead of backticks, and the script polls `/Volumes/PHD2` for up to 5 s before talking to Finder (plus a 1 s `delay` at the top of the AppleScript for belt-and-suspenders).
- `debian/openastro-phd2.postrm` removes `/var/lib/openastro-phd2` on package purge. The postinst creates this directory for runtime state (calibration, profiles, logs); Debian policy requires purge to delete all package state. Without this, `apt purge openastro-phd2` would leave the state dir orphaned on disk.
- `test_jsonrpc_schema.cpp` now type-guards every `json_value` union access. Added `ASSERT_EQ(units->type, JSON_STRING)` and `ASSERT_EQ(axes->type, JSON_STRING)` before the `string_value` reads in the `GetLockShiftParams` enabled branch, and upgraded the `expect_string_field` / `expect_numeric_field` / `expect_bool_field` helpers from `EXPECT_EQ` to `ASSERT_EQ` on the type check. Wrong-type accesses now hard-stop the test instead of progressing to UB on a mismatched union member.
- `test_jsonrpc_schema::JsonRpcSchema.SettleDoneEvent` now asserts `Error` is absent on success (Status==0) responses, not just on the failure branch. Schema spec says Error exists iff Status≠0; downstream consumers (NINA, KStars) read `Error` as a truthy "did the settle fail" indicator, so a spurious empty-string Error on success would look like a settle failure to them.
- `test_jsonrpc_schema::JsonRpcSchema.ResponseEnvelopeSuccess` and `ResponseEnvelopeError` now assert envelope exclusivity (`result` xor `error`). Previously each branch only asserted the required key was present; a mixed envelope with both keys would pass both tests silently. Per JSON-RPC 2.0 the response MUST have exactly one of `result` or `error`.
- `test_jsonrpc_schema::JsonRpcSchema.GetLockShiftParams` disabled-branch now asserts `rate`, `units`, and `axes` are absent. Comment said "only enabled is present" but the test didn't enforce it; a server that started emitting `rate`/`units`/`axes` alongside `enabled=false` would pass the test.
- `build-dmg.sh` shipped a DMG that crashed on launch with `Library not loaded: /opt/homebrew/...libcfitsio.10.dylib` on any Mac without Homebrew installed at the same prefix as the build host. `bundle_app_dylibs` and `check_library_dependencies` both hardcoded the binary path as `Contents/MacOS/PHD2`, but the same release renamed CMake's `OUTPUT_NAME` to `"OpenAstro PHD2"` so the binary actually lives at `Contents/MacOS/OpenAstro PHD2`. `otool -L` against the missing PHD2 file produced empty output, which silently skipped both the dylib bundling step AND the safety-net check that exists specifically to fail-loud on unbundled `/opt/homebrew/...` loads. End result: the DMG appeared to build cleanly, ran fine on the developer's Homebrew-equipped Mac, and crashed at launch on the user's. Both helpers now use a new `APP_EXE` variable defined alongside `APP_BUNDLE` (so they move in lockstep with CMake's OUTPUT_NAME), and added an `[[ -f "$exe" ]] || err …` guard so any future drift fails the build instead of shipping a Homebrew-coupled binary.

### Commit References
- `1f15cc8b` - Set AppName before wxSingleInstanceChecker so both PHD2 forks can run
- `ac5a8c39` - Ignore debian/openastro-phd2/ build staging dir
- `abb0b8cc` - Align per-platform install defaults with the OpenAstro brand
- `719f8313` - Fix build-dmg.sh failure on macOS Tahoe (-1728 + bash backtick noise)
- `b3406d56` - Remove RELEASE-NOTES-2.0.0.md from the repository
- `47431ecf` - Ship a phd2-alpaca transitional package alongside openastro-phd2
- `a037c209` - debian/openastro-phd2.postrm: remove /var/lib/openastro-phd2 on purge
- `6fb40e7d` - Move wxConfig out of upstream PHD2's namespace for safe coexistence
- `6dcdb3c2` - Unify packaging artifact names as openastro-phd2-<ver>-<arch>.<ext>
- `62a81250` - Address CodeRabbit round 3 on PR #12
- `77ef0cef` - Type-guard json_value union accesses in jsonrpc_schema tests
- `c7cecf41` - Address CodeRabbit review on PR #12
- `6ee83d07` - Widen GPTest covariance tolerance so Debian build stops failing
- `81588a67` - Repair Windows packaging build broken by the test-gate commit
- `94c80630` - Add unit test suite and gate every packaging build on it passing
- `26923689` - Seed std::srand in GPTest fixture so covariance test is deterministic
- `e7a91ddc` - Bound Alpaca AbortExposure retry so Stop during Looping no longer hangs PHD2
- `b0eb5f0e` - Drop redundant per-window SetCursor in discovery polling loops
- `2e611ba9` - Probe loopback in INDI discovery so same-box servers are found
- `5a22621a` - Finish OpenAstro rebrand, fix INDI/Alpaca dialog UX on macOS
- `cfad5f23` - Center every dialog on the main window, ship drag-to-Applications DMG
- `88578c56` - Add native Apple Silicon (arm64) macOS build, ship redistributable DMG
- `8035cb8b` - Address CodeRabbit review on PR #9
- `f953b40a` - Disable INDI_BUILD_COMMON to skip driver-side deps
- `2cf0a5ac` - Remove libusb and vendored openssag (dead code)
- `b844cf80` - Bump GoogleTest from v1.14.0 to v1.17.0
- `03f62cbd` - Bump libINDI from v2.1.6 to v2.2.1.1
- `08f754d4` - Bump vcpkg pin to 2026.03.18
- `9cdf439c` - Drop x86 wiring from CMake and Windows build scripts
- `bf83f239` - Remove dead 32-bit Windows binary assets
- `3f4860af` - Refresh About dialog maintainers and fix stale Homepage URLs
- `f6a5a8ef` - Backfill CHANGELOG Unreleased for the C++20 modernization commit
- `9a297da2` - Narrow Linux build scripts to Debian 13 Trixie, amd64/arm64
- `4126947c` - Track CHANGELOG Unreleased in /commit and add /release skill
- `eddc100d` - Modernize Windows build: C++20, wxWidgets 3.2 floor, x64-only
- `9c6ae54b` - Address CodeRabbit review on PR #6
- `0d25be58` - Update .gitignore
- `61be36b0` - Fix .deb post-build glob to match phd2-alpaca filename
- `539f6cdb` - Write phd2-alpaca (not phd2) as source name in synced debian/changelog
- `8d83023e` - Extend libindi auto-detect to .deb build path
- `36753c43` - Address CodeRabbit review on PR #5

## [1.3.0] - 2026-05-12

### Fixed
- **Alpaca discovery misses loopback-bound servers**
  - `AlpacaDiscovery::BuildBroadcastTargets()` now always probes `127.0.0.1:32227` as a unicast target so Alpaca servers bound only to loopback (e.g. ASCOM Remote Server's default "Loopback" IP setting) are discovered. Previously these were invisible because subnet broadcasts (255.255.255.255 and per-interface broadcasts) never reach loopback-only listeners — the workaround was to manually edit Remote Server to bind to the LAN IP.
  - Added Linux/macOS network-interface enumeration via `getifaddrs`. Previously the non-Windows path sent only to `255.255.255.255`, so multi-NIC Linux/Pi setups (VPN, docker, multiple LANs) missed servers on subnets the default route did not cover. Mirrors the Windows `GetAdaptersAddresses` path and the pattern in `indi_discovery.cpp`.

### Removed
- **`PHD2_ALLOW_INDI_1_9` escape hatch dropped**
  - INDI 2.0+ is now a hard requirement. Removed the option from `thirdparty/thirdparty.cmake`, the env-var plumbing in `run_deb.sh` / `build-deb.sh`, and the `PHD2_ALLOW_INDI_1_9` make variable in `debian/rules`. The Player One driver and several other INDI drivers have meaningful fixes after 1.9.x; keeping the toggle invited subtly broken builds.
  - For Debian/Pi systems that only ship INDI 1.9.x, `run_deb.sh` now auto-detects this and falls back to building INDI 2.1.6 from source — no manual PPA setup required.

### Changed
- **Auto-detect libindi version in all build paths**
  - `run_deb.sh`, `debian/rules`, and `build-deb.sh` now all probe `pkg-config --atleast-version=2.0.0 libindi`. When the system libindi is missing or < 2.0.0, the build sets `USE_SYSTEM_LIBINDI=0` so CMake fetches and builds INDI 2.1.6 from source as a static client library (no shared libs to bundle). The from-source path was already wired into `thirdparty/thirdparty.cmake`; it just wasn't reachable from any of the user-facing scripts. Override with `USE_SYSTEM_LIBINDI=0` or `=1` explicitly.
  - `build-deb.sh` no longer hard-fails on stale libindi-dev: it now prints which path will be taken (system or from-source) and proceeds. Older libindi-dev on Debian trixie / Pi OS no longer blocks `.deb` builds.
- **Alpaca port default is now `0` (unconfigured) instead of `6800`**
  - `pConfig->Profile.GetLong("/alpaca/port", ...)` now defaults to `0` in `cam_alpaca.cpp`, `scope_alpaca.cpp`, `rotator_alpaca.cpp`, `camera.cpp`, `scope.cpp`, `rotator.cpp`, and `event_server.cpp`. The previous `6800` was AlpacaBridge-specific and misleading for users running ASCOM Remote Server (default `11111`) or anything else.
  - `Connect()` "not yet configured" sentinel in the Alpaca camera/mount/rotator drivers simplified from `host == "localhost" && port == 6800 && device == 0` to just `port == 0`. The old triple-check could spuriously re-open the setup dialog for users genuinely running AlpacaBridge at `localhost:6800` with device 0.
  - `AlpacaConfig::SetSettings()` renders an unconfigured port as an empty field instead of literal `"0"`. The dialog's existing auto-discover path fires when the server list is empty, so new profiles open with empty fields and discovery immediately populates them — matching NINA's flow.

### Added
- **INDI support restored on all platforms**
  - Re-added INDI camera, mount, and rotator drivers (`cam_indi`, `scope_indi`, `rotator_indi`, `config_indi`, `indi_gui`). Available on Windows (via vcpkg-built `indiclient.lib`), macOS, and Linux. AO/stepguider INDI drivers remain removed.
  - Restored `USE_SYSTEM_LIBINDI=1` for Debian builds; INDI 2.x runtime libs bundled into `/usr/lib/phd2-alpaca`.
  - Added `libindi-dev` build dependency to `debian/control`.
- **INDI server discovery**
  - New `INDIDiscovery` class performs parallel non-blocking TCP probes on port 7624 across local /24 subnets. Total scan ~2s.
  - Added Discover Servers button, status label, and discovered-servers combobox to `INDIConfig` dialog. Auto-fires on dialog open when host is empty; picking from the list auto-fills host/port.
- **OpenAstro favicon for the embedded web portal.**
- **Developer tooling**
  - Added `.claude/commands/commit.md` slash command that runs the project's clang-format pass before staging and writes a structured commit message.

### Changed
- **Windows build**
  - Bumped pinned vcpkg to release tag `2024.11.16` (SHA-pinned). Parallelized the Windows build invocation in `run_win.bat` (renamed from `run_cmake.bat`) so multi-core machines actually use their cores. Added flags: `-rebuild`, `-config`, `-launch`. Added Linux counterpart `run_deb.sh`.
  - Enforced CRLF for `.bat` files via `.gitattributes` to keep the Windows build script intact across platforms.
- **Wrapper script and systemd service**
  - Restored `LD_LIBRARY_PATH=/usr/lib/phd2-alpaca` in `debian/phd2-alpaca.service` and `phd2.sh.in` so bundled INDI 2.x libs are discoverable at runtime.

### Fixed
- **INDI camera first-exposure disconnect**
  - `CameraINDI::CheckState()` now requires `CCD_INFO` before marking the camera ready, so `Connect()` blocks until `m_maxSize` is populated. Previously the first `Capture()` could send `FRAME` with width=0/height=0, causing the driver to disconnect mid-exposure.
- **Wizard pixel-size auto-fill for INDI cameras**
  - `CameraINDI::GetDevicePixelSize()` now waits briefly for `CCD_INFO` arrival when called immediately after connect, so the profile wizard auto-populates the pixel-size field from the camera (matching Alpaca behavior).
- **Aux-mount cleanup**
  - Collapsed `ScopeINDI`'s aux-mount conditional branches; the gear dialog no longer exposes `AuxScope()` post-slim, so the dead branch is gone.

### Commit References
- `9648537a` - Re-add INDI camera, mount, and rotator support on all platforms
- `b63a1358` - Fix clang-format wrap in scope_indi.cpp Debug.Write
- `a33ccf1c` - INDI server discovery + pixel size + first-exposure fix
- `f2bc9b8f` - Apply clang-format fixes and add /commit slash command
- `5263b10c` - Bump vcpkg to 2024.11.16 and parallelize Windows build
- `c9d57c8c` - Address CodeRabbit review: SHA pin, preserve CL flags, enforce CRLF
- `e399535d` - Add favicon using OpenAstro logo

## [1.2.0] - 2026-03-22

### Removed
- **Legacy camera drivers and SDKs**
  - Removed all non-Alpaca camera drivers and bundled third-party SDK libraries (ZWO, QHY, SBIG, Altair, ToupTek, SVBony, PlayerOne, Moravian, INDI, ASCOM, OpenCV, and remaining legacy backends).
  - Removed associated sources, headers, and SDK references from `CMakeLists.txt` and the `cameras/` tree.
- **Adaptive optics**
  - Removed adaptive optics from the gear dialog, profile wizard, and event server API (StepGuider/SXAO, `stepguider_sbigao_indi`). Stubs retained for backward compatibility.
- **Auxiliary mount and on-camera ST4 guiding**
  - Removed auxiliary mount support and on-camera ST4 guiding (`OnboardST4`, `ScopeOnCamera`, `ScopeOnStepGuider`). Guiding in this fork uses Alpaca only.
- **INDI**
  - Removed INDI support entirely (`scope_indi`, `rotator_indi`, `indi_gui`, `config_indi`).
- **Platform build wiring**
  - Removed macOS framework copy and `install_name_tool` handling for dropped SDKs.
  - Removed `/DELAYLOAD:sbigudrv.dll` from Windows link flags.

### Changed
- **Gear and UI**
  - Simplified gear dialog to camera, mount, and rotator only.
- **Formatting**
  - Applied clang-format across affected files (`event_server.cpp`, `gear_dialog.cpp`, `gear_simulator.cpp`, `profile_wizard.cpp`, `stepguiders.h`, `camera.cpp`).

## [1.1.0] - 2026-03-07

### Added
- **Headless Runtime + API Expansion**
  - Added CLI flags `--headless` and `--headless-auto-connect`.
  - Added auto-connect support for selected equipment at startup.
  - Added Linux systemd unit example: `packaging/systemd/phd2-headless.service`.
  - Added JSON-RPC methods for equipment/config selection, including INDI and Alpaca server/discovery/device-selection methods.
  - Added `get_equipment_choices`.
  - Added JSON-RPC smoke tooling and equipment choice listing scripts.
  - Added `doc/jsonrpc_api.md` API reference.
- **Embedded Web Portal (single-app mode)**
  - Added an in-process HTTP server integrated into `EventServer` lifecycle (start/stop with Server Mode).
  - Added local web access endpoint: `http://127.0.0.1:8080/` for instance 1.
  - Added multi-instance port pattern matching existing server behavior: `8080 + (instanceId - 1)`.
  - Added HTTP routing for:
    - `GET /` and `GET /index.html`
    - `GET /assets/*`
    - `GET /api/setup`
    - `GET /api/discover/alpaca`
    - `POST /api/rpc`
  - Added Tools menu entry to launch the web portal in the default browser.
  - Added platform packaging/install handling for web UI assets on Linux, Windows, and macOS.
- **Web Setup and Profile Flow**
  - Added profile-first wizard flow with create/select behavior before equipment setup.
  - Added profile management actions (select/rename/delete) and dark-file options tied to profile deletion.
  - Added end-of-wizard dark library build flow with progress reporting and completion feedback.
- **API and Discovery Support**
  - Added setup payload endpoint to return combined wizard/profile/equipment/alpaca/dark state needed by web UI.
  - Added Alpaca discovery endpoint integration for server discovery and device listing from the web portal.
  - Added HTTP-to-RPC bridge endpoint using existing JSON-RPC method handling.
- **Version Source of Truth**
  - Added root `version.md`.
  - Added generated compile-time version header (`cmake_modules/phd_version.h.in` -> generated `phd_version.h`).
  - Switched CMake/runtime and build scripts to read version from `version.md`.

### Changed
- **JSON-RPC Contract Hardening**
  - Rejected selection/config changes while equipment is connected.
  - Standardized invalid parameter handling.
  - Expanded `get_connected` coverage for aux mount/AO/rotator.
- **Web Wizard UX**
  - Updated profile-first flow, device discovery, dark-library controls, status messaging, and profile summaries to align more closely with native PHD2 behavior.
  - Updated Alpaca discovery inputs/behavior so host/port are not prefilled before discovery.
  - Updated profile and wizard cards/details styling to match AlpacaHTTP visual behavior.
  - Updated dark library exposure controls and labels to match native wizard defaults and wording.
- **Build and Packaging**
  - Updated Debian build flow to consume version from `version.md` and sync top `debian/changelog` version during configure.
  - Updated `build-deb.sh` to always sync `debian/changelog` from `version.md` + root `CHANGELOG.md` on each run, while preserving older Debian changelog entries.
  - Updated Windows installer build script to consume version from `version.md`.
- **Documentation**
  - Updated JSON-RPC + Web API reference: [doc/jsonrpc_api.md](doc/jsonrpc_api.md).
  - Updated `scripts/README_web_ui.md` to document embedded web mode and current behavior.

### API Links
- JSON-RPC and Web API reference: [doc/jsonrpc_api.md](doc/jsonrpc_api.md)
- Web portal usage/behavior notes: [scripts/README_web_ui.md](scripts/README_web_ui.md)

### Commit References
- `bf0d0349` - Add headless runtime and JSON-RPC equipment selection API
- `9789d2ec` - Expand headless JSON-RPC equipment/config API and docs

## [1.0.0] - 2026-03-04

### Added
- **Initial OpenAstro PHD2 Release**
  - First OpenAstro-branded release line.
  - Linux packaging and Alpaca Linux enablement included.

### Changed
- **Packaging/Build**
  - Updated `phd2.desktop` branding/comment for OpenAstro PHD2 with Alpaca support.
  - Fixed Alpaca build issues around JsonParser usage and include dependencies.
  - Extended `.gitignore` for Debian/CMake build artifacts.

### Commit References
- `e9125e1b` - OpenAstro branding: APPNAME, version 1.0.0, About dialog and status bar
- `4b5ab276` - Linux .deb build, Alpaca on Linux, and packaging fixes

## [2026-01-23]

### Changed
- **OpenAstro Branding and UI**
  - Applied OpenAstro Alpaca-focused branding updates, including icons, app presentation text, and related UI polish.
  - Updated main frame/about presentation and profile/wizard-facing UI elements for OpenAstro identity.

### Commit References
- `33475ebf` - OpenAstro Alpaca-only branding and UI changes
- `8474f1be` - Use OpenAstro icon for main window and disclaimer

## [2026-01-20]

### Changed
- **Alpaca Camera + Profile Flow**
  - Improved Alpaca camera handling and profile-related setup flows.
  - Updated profile wizard and gear/config dialogs for smoother Alpaca setup behavior.
  - Improved Alpaca discovery + camera selection behavior in wizard-related dialogs.

### Commit References
- `77d42cdc` - Enhance Alpaca camera handling and profile flows

## [2026-01-13]

### Added
- **Alpaca Rotator Support**
  - Added Alpaca rotator driver support and integration.

### Changed
- **Alpaca Client Reliability**
  - Improved Alpaca client/discovery behavior and related camera/mount integration reliability.
  - Expanded rotator integration path in core device selection/build registration.

### Commit References
- `a3dee229` - Add Alpaca rotator support and improve Alpaca client reliability

## [2025-11-18]

### Added
- **ASCOM Local Alpaca Support**
  - Added local Alpaca support path for cameras and mounts.
  - Added Alpaca discovery module integration (`alpaca_discovery`) used by selection/configuration flow.

### Commit References
- `254c95fa` - Add ALPACA (ASCOM Local Alpaca) support for cameras and mounts

## [2025-11-16]

### Added
- **Initial ASCOM Alpaca Driver Support**
  - Added initial Alpaca camera and mount driver integration for OpenAstro fork work.
  - Added initial Alpaca client/configuration plumbing and platform build wiring.

### Commit References
- `a0900cd6` - Add Alpaca (ASCOM Alpaca) driver support for cameras and mounts
