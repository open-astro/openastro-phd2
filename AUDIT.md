# openastro-phd2 — Engineering Audit

**Date:** 2026-07-12
**Scope:** Full-repository senior engineering audit of the OpenAstro fork of PHD2 (branch `main`, at `6591b4af`). The fork supports only ASCOM Alpaca, INDI, and ASCOM COM device transports; all vendor SDK camera backends were removed upstream.
**Method:** Parallel review of four areas — C++ source (`src/`), build system & packaging, tests/CI/docs, and the INDI transport. Line numbers are anchors at time of audit and may drift.

---

## Executive summary

The fork is, on the whole, thoughtfully engineered: careful CHANGELOG discipline, single-sourced versioning, defensive Alpaca binary-image parsing, SHA-pinned review CI, and a real test suite. The risk is concentrated in the surfaces the fork *added or heavily touched*, and it clusters into a few themes:

1. **An unauthenticated, network-exposed RPC server** (`event_server.cpp`) bound to `0.0.0.0` with destructive methods, remote arbitrary file write, and several network-reachable null-dereferences. This is the single most serious area.
2. **INDI thread-discipline gaps** — disconnect/unplug paths touch wxWidgets UI or join the INDI client thread from the wrong thread, making device-loss scenarios the most crash-prone.
3. **ASCOM COM resource leaks and disconnect-race null-derefs** — classic uncleared VARIANT / SAFEARRAY / EXCEPINFO leaks plus always-callable status getters that crash after disconnect.
4. **Build/packaging inconsistencies** — a bypassable Windows dependency pin, an unhashed download, a contradictory Debian INDI story, and leftover proprietary vendor blobs that contradict the "no bundled SDKs" claim and the blanket BSD copyright.
5. **CI and docs drift** — no compile/test CI at all, a documented test kill-switch that does nothing, and design/help docs describing a different project or removed hardware.

Recommended priority order: **(1) lock down the RPC endpoint, (2) fix the reachable null-derefs and path confinement in `event_server.cpp`, (3) fix the INDI disconnect thread-hops, (4) add a build+ctest CI job, (5) clean up ASCOM leaks and remove dead backends.**

---

## Resolution status

Most findings below were remediated in the `fix/audit-findings` branch (see the CHANGELOG `[Unreleased]` section for the itemized list). **Intentionally not changed:** the RPC server's `0.0.0.0` bind and lack of authentication (P1), and the `capture_single_frame` path confinement (P2) — the event-server RPC is treated as a trusted local/LAN control surface for this application's normal usage, so these are accepted by design rather than defects to fix. The reachable null-dereference and correctness bugs in the same file (P3, P4, and the medium/low items) were still fixed, since those are crashes independent of the trust model.

## Priority fixes (do these first)

| # | Severity | Area | Issue |
|---|----------|------|-------|
| P1 | HIGH | Security | RPC server binds `0.0.0.0` with no auth; destructive methods exposed to the LAN |
| P2 | HIGH | Security | `capture_single_frame` path param → remote arbitrary file write |
| P3 | HIGH | Correctness | Missing `return` → null-deref of `pCamera` in `capture_single_frame` |
| P4 | HIGH | Correctness | `set_algo_param` null `value` → network-triggerable null deref |
| P5 | HIGH | Concurrency | INDI `removeDevice`/`serverDisconnected` touch UI / self-join client thread |
| P6 | HIGH | Correctness | ASCOM status getters null-deref after disconnect |
| P7 | MEDIUM | Supply chain | Windows vcpkg pin bypassed; googletest download unhashed |
| P8 | HIGH | CI | No build or test CI — the test suite never runs on PRs |

---

## 1. Event server / RPC (`src/event_server.cpp`)

The fork's headless RPC surface is the highest-risk component.

- **HIGH — `event_server.cpp:5276` — RPC server binds to `0.0.0.0` with no authentication.** The full method surface (`shutdown`, `set_connected`, `capture_single_frame`, config writes) is reachable by anyone on the LAN. Port is `8080 + instanceId - 1`; `/api/rpc` dispatches any method by name. *Fix: bind loopback by default, require an opt-in for LAN exposure, and add a token/auth check.*
- **HIGH — `event_server.cpp:3522` — `capture_single_frame` accepts any absolute `path` with no directory confinement.** Only `fn.IsAbsolute()` and an "already exists" check; combined with the `0.0.0.0` bind this is remote arbitrary file write. *Fix: confine writes to a configured capture directory; reject traversal.*
- **HIGH — `event_server.cpp:3465` — `capture_single_frame` lacks a `return` after the "camera not connected" error**, so it falls through to `pCamera->GetBinning()` and dereferences a null `pCamera`. Network-reachable crash.
- **HIGH — `event_server.cpp:3994` — `set_algo_param` passes a possibly-null `value` into `float_param`**, which reads `->type` without a null check (~`event_server.cpp:2522`). Omitting the key triggers a null deref over the network.
- **MEDIUM — `event_server.cpp:3099` — `capture_master_dark_frame` buffer overrun.** `avgimg` is sized from the first frame's `NPixels` and never re-checked; a later larger frame (ROI/binning change mid-build) overruns the accumulator.
- **MEDIUM — `event_server.cpp:471 / 997` — partial socket writes silently truncate JSON.** Writes only check `LastWriteCount()` and log short writes with no retry/requeue of the unwritten tail, desyncing that client's line-delimited stream.
- **LOW — `event_server.cpp:2093` — `set_variable_delay_settings`** computes `(int) shortDelaySec * 1000`, truncating before scaling, with no range validation.
- **LOW — `event_server.cpp:4856` — `url_decode`** casts `%XX` bytes via `(wxChar) v` without UTF-8 decoding and tolerates a trailing `%`; bytes > 0x7F are mis-decoded.
- **LOW — `event_server.cpp:3628` — `get_star_image`** reads `reqsize` with only a lower-bound check; currently harmless because `halfw` is clamped to 31.

---

## 2. INDI transport (`cam_indi` / `scope_indi` / `rotator_indi` / `indi_gui` / `config_indi`)

Dominant weakness: **inconsistent thread discipline**. Some paths correctly hop INDI-client-thread callbacks onto the wx main thread (`ExecInMainThread`, `wxQueueEvent`); the disconnect/unplug paths do not.

- **HIGH — `config_indi.cpp:463-475, 513-554` — `INDIConfig::newDevice`/`newProperty` mutate wx controls directly from the INDI client thread** (`Append`/`Delete`/`SetSelection`/`Enable`), while `serverConnected`/`serverDisconnected` in the same class correctly use `wxQueueEvent`. Off-main-thread wx calls are undefined behavior.
- **HIGH — `cam_indi.cpp:761-765` — `CameraINDI::removeDevice` calls `DisconnectWithAlert` synchronously on the INDI client thread.** `updateProperty` (line 346) deliberately routes the same call through `ExecInMainThread` to avoid self-joining the INDI worker thread; `removeDevice` and `serverDisconnected(exit_code==-1)` skip that hop → self-join deadlock risk + off-thread UI.
- **HIGH — `scope_indi.cpp:374-378` and `rotator_indi.cpp:200-224` — same defect.** `ScopeINDI::removeDevice` calls `Disconnect()` (which `disconnectServer()` joins the current thread) from the callback; `RotatorINDI` additionally calls `pFrame->Alert` / `UpdateStatsWindowScopePointing()` on the client thread.
- **HIGH — `indi_gui.cpp:638-640` — `SetButtonEvent` sizes the write from the *old* text length** (`snprintf(tp[i].text, strlen(tp[i].text)+1, ...)`), so new text is truncated to the previous value's length and an initially-empty text property can never be set (buffer size 1). Also writes into a libindi-owned buffer of unknown capacity.
- **HIGH — `cam_indi.cpp:1207-1243` — video-stacking dangling-pointer race.** The client thread checks `if (modal && !stacking)` then calls `StackStream` while the worker clears `modal`/returns; a callback that passed the check before `stacking = true` writes through a dangling `StackImg` pointer into the caller's stack `usImage`. `has_blob`, `expose_prop->s`, `Connected`, `ready` are similarly polled cross-thread without atomics.
- **MEDIUM — `rotator_indi.cpp:61, 123-130` — `volatile bool modal` never initialized**; `CheckState()` reads indeterminate memory (UB). (`cam_indi.cpp` already moved this pattern to `std::atomic`.)
- **MEDIUM — `config_indi.cpp:216-217` — Dual-CCD combo grows duplicates.** `UpdateControlStates()` appends "Main"/"Secondary" without `Clear()`, so every reconnect / queued update event duplicates entries.
- **MEDIUM — `config_indi.cpp:352-358` and `scope_indi.cpp:862-867` — reentrant `wxYield`/`wxSafeYield` on the main thread.** `OnDiscover` yields while discovery runs (user can re-enter Connect/Cancel/close → use-after-free of the dialog); `SlewToCoordinates` can spin up to 90 s yielding.
- **MEDIUM — `indi_gui.cpp:530-531, 547-548, 565-566` — unchecked hash lookups.** `devlist[devname]`/`properties[propname]` are dereferenced without null checks; an update for a device whose creation event was skipped crashes (and `wxStringHash[key]` inserts null entries as a side effect).
- **LOW — `cam_indi.cpp:77-85, 240-243` — `strncpy(m_format, ..., MAXINDIBLOBFMT)`** leaves `m_format` unterminated at exactly-max length (later `strcmp` over-reads); `ClearStatus()` signals `sync_cond` without holding `sync_lock`, so a `Guide()` waiter can miss the wakeup until its 100 ms timeout.

---

## 3. ASCOM COM layer (`cam_ascom` / `scope_ascom` / `rotator_ascom` / `comdispatch`)

Structurally sound (GIT marshaling, per-call exception objects, new SAFEARRAY bounds checks) but carries classic slow COM leaks and disconnect-race crashes.

- **HIGH — `cam_ascom.cpp:318` — `ASCOM_Image` uses `vRes.parray` without checking `vRes.vt`.** A driver returning a non-`VT_ARRAY|VT_I4` variant makes the copy operate on a garbage pointer or mis-sized elements; the `(unsigned short) rawdata[i]` loops (lines 411, 427) cast to `long *` assuming 4-byte elements.
- **HIGH — `comdispatch.h:117` + call sites — `GITEntry::Get()` returns null when unregistered; `DispatchObj` dereferences `m_idisp` unchecked.** Status getters callable while disconnected crash: `RotatorAscom::Position()` (`rotator_ascom.cpp:238`), `CameraASCOM::GetSensorTemperature()` (`cam_ascom.cpp:931`, races with Disconnect's Unregister at `:815`), `ScopeASCOM::AbortSlew()` (`scope_ascom.cpp:1119`).
- **MEDIUM — `cam_ascom.cpp:325/342/431` — SAFEARRAY descriptor leak per frame.** Each frame calls `SafeArrayDestroyData` (frees data, not descriptor) and never `VariantClear`s `vRes` — a descriptor leaked per captured frame, at guide cadence, for hours.
- **MEDIUM — `comdispatch.h:44` — `struct Variant` has no destructor calling `VariantClear`.** Gear-dialog enumeration loops (`cam_ascom.cpp:493`, `scope_ascom.cpp:120`, `rotator_ascom.cpp:103`) leak IDispatch + BSTRs per device on each open.
- **MEDIUM — `comdispatch.cpp:183` — reused `m_excep` EXCEPINFO** overwrites its BSTR fields on each `DISP_E_EXCEPTION` without freeing the previous ones; leaks BSTRs on repeated failed calls.
- **MEDIUM — `rotator_ascom.cpp:200` — `Disconnect()` never calls `m_gitEntry.Unregister()`** (camera and scope both do), pinning the driver IDispatch in the GIT after a driver restart.
- **LOW — `cam_ascom.cpp:594-793` — `CamConnectFailed` after `Connected = true`** leaves the driver connected/registered with no compensating `PutProp(Connected,false)`/Unregister; live driver connection leaks until exit.
- **LOW — `worker_thread.cpp:391` — `CoInitializeEx(COINIT_MULTITHREADED)` has no matching `CoUninitialize`** before return (`:469`); apartment left unbalanced.

---

## 4. Alpaca transport (`alpaca_client` / `cam_alpaca` / `alpaca_discovery`)

Mostly clean; the binary parser and UDP discovery are well-bounded.

- **MEDIUM — `alpaca_client.cpp:654, 667, 793, 819` — unlocked shared-buffer read.** `GetDouble`/`GetInt`/`GetBool`/`GetString` re-read `m_response.str()` after `Get()`/`Put()` released `m_mutex`; a concurrent request on the same client races on it.
- **LOW — `cam_alpaca.cpp:1184` — confused subframe index** `(y - roi.y + roi.y)` (i.e. just `y`), correct only because the terms cancel; a latent bug if edited.
- **Positive:** ImageBytes decode (`cam_alpaca.cpp:888-1046`) validates metadata size, payload truncation, and rank/dimension/type before any pointer walk; UDP receive (`alpaca_discovery.cpp:307-341`) is correctly bounded; curl handles and `curl_slist` headers are freed on all paths.

---

## 5. Dead code from removed SDK backends

The camera-layer SDK removal is clean (no ZWO/QHY/SBIG/Atik/SX/etc. references remain), but scope/webcam backends were left behind.

- **MEDIUM — six dead scope backends still in the build.** `scope_voyager`/`eqmac`/`equinox`/`GC_USBST4`/`gpusb`/`gpint` (`.cpp`/`.h`) are listed in `CMakeLists.txt:230-243` and `scopes.h:62-67`, but their guards (`GUIDE_VOYAGER`, `GUIDE_EQMAC`, `GUIDE_EQUINOX`, `GUIDE_GCUSBST4`, `HAVE_SHOESTRING`) are defined nowhere — empty translation units. *Fix: remove from build and delete.*
- **LOW — `scope.cpp:434`** contains literal prose inside `#ifdef GUIDE_VOYAGER`, proving the block never compiles (also dead blocks at `:358`, `:419`).
- **LOW — V4L remnants** in `myframe.h:334` / `myframe.cpp:129/546/577` (guard `V4L_CAMERA` never defined); stale `about_dialog.cpp:183` ToupTek credit; stale comments in `scope_eqmac.h:57`, `cameras.h:37`.

---

## 6. Build system & packaging

Scripts are well-commented and defensive, but pinning and the Debian INDI story are inconsistent, and proprietary blobs remain.

- **HIGH — `run_exe.bat:108` — the vcpkg commit pin is bypassed.** The script pre-clones vcpkg at HEAD; because `thirdparty.cmake:88` overrides `UPDATE_COMMAND` with `bootstrap-vcpkg.bat`, FetchContent never checks out the pinned `GIT_TAG` (`thirdparty.cmake:87`). Windows builds use whatever vcpkg HEAD (and dependency versions) ship that day.
- **HIGH — `debian/control:11` vs `debian/rules:19` / `build-deb.sh:133` — the libindi-dev fallback is dead on the packaging path.** `libindi-dev` is a hard `Build-Depends`, so `dpkg-buildpackage` fails the dep check before the "auto-fetch INDI 2.2.1.1" fallback can run. Comments promising automatic fallback are wrong for the `.deb` path (works only with `--force`).
- **HIGH — `build-dmg.sh:485` — `osascript` failure aborts the whole DMG build under `set -e`.** The comment claims a denied Finder-automation prompt falls back to a default layout, but the nonzero exit kills the script (no trap/cleanup; staging DMG left mounted).
- **MEDIUM — `thirdparty.cmake:220-223` — googletest tarball has no `URL_HASH`.** Downloaded over HTTPS but unverified on non-system-gtest builds (Windows/macOS).
- **MEDIUM — `build-dmg.sh:199-206` — empty `dep_load` corrupts install names.** If `bundled_name_for` fails it echoes empty, `install_name_tool -change` runs with an empty basename, and all stderr is discarded via `2>/dev/null`; nested-dylib `@rpath/` deps are whitelisted (`:255`) and never validated.
- **MEDIUM — `debian/rules:87-91` — packages host `/usr/local/lib/libindi*.so*`.** Non-hermetic: stray host INDI libs get shipped; the "trixie ships 1.9.9" comment is stale and contradicts every other file.
- **MEDIUM — `run_exe.bat:75-90` — delayed-expansion bug.** `%STAMP%` expands at parse time inside the parenthesized block (empty), so the locked-tmp rename target is always `tmp_locked_`; a second locked run collides. Needs `enabledelayedexpansion` + `!STAMP!`.
- **MEDIUM — `build-exe.ps1:65-68` — hardcoded Inno Setup 5 paths only.** Inno 5 is EOL; the script errors on machines with the current Inno 6.
- **MEDIUM — leftover proprietary vendor blobs contradict the "no bundled SDK" claim.** `thirdparty/frameworks/SBIGUDrv.framework/`, `extra_frameworks/SBIGUDrv.framework.zip`, `fcCamFw.framework.zip`, `thirdparty/MallincamGuider-OSX-dylib-source.zip` remain although `CMakeLists.txt:560` says frameworks were removed; `debian/copyright` (`Files: *` → BSD-3-clause) misdeclares their license in any source package. `cmake_modules/FindZWO.cmake` and `build/unpack_*_sdk*` are dead too.
- **MEDIUM — `WinLibs/x64/*.dll` — vendored MSVC CRT DLLs** installed by `phd2.iss.in:60-63` can lag the CRT the VS 2022 toolchain links against (missing-export crashes); no refresh process.
- **LOW — stale Build-Depends / URLs.** `debian/control:6` allows wx 3.0 though CMake requires wx 3.2 (`thirdparty.cmake:287`); `debian/control:15` Homepage, the systemd unit Documentation, and `PHD2Packaging.cmake:94` maintainer still point at upstream / the old repo name; two divergent systemd unit definitions.
- **LOW — `thirdparty.cmake:272` — typo `${wxRwxRequiredLibs}`** (undefined) breaks the FreeBSD branch.
- **LOW — `build-exe.ps1` never validates `WXWIN`** though `thirdparty.cmake:236-247` hard-fails without a static `vc_x64_lib` wx install; failure surfaces deep in CMake output.

---

## 7. Tests, CI & documentation

- **HIGH — no build/test CI at all.** `.github/workflows/` has only `clang-format-check.yml` and `claude-review.yml`; nothing compiles the project or runs `ctest`. Tests gate only local packaging scripts, so a PR can break the build/tests and CI stays green. (`design/GUIDER_PROGRESS.md:14` lists "CI baseline" as a to-do.)
- **HIGH — documented test kill-switch does nothing.** `README.md:76` and `tests/README.md:24` tell users to pass `-DBUILD_TESTING=OFF`, but `CMakeLists.txt:166-168` gates the suite on `PHD_BUILD_TESTS`; `BUILD_TESTING` is never consulted and `enable_testing()` (`CMakeLists.txt:129`) is unconditional. The CHANGELOG's `-DPHD_BUILD_TESTS=OFF` is the flag that actually works.
- **HIGH — zero coverage for the ASCOM transport,** including newly added `cam_ascom.cpp` and the changed camera-factory routing in `camera.cpp`. `tests/` covers only Alpaca/INDI/JSON/guide-math, and mostly as "model"/"twin" reimplementations rather than production functions (acknowledged in `tests/README.md`).
- **MEDIUM — `claude-review.yml` reads untrusted PR content with comment-posting enabled.** PR diff/contents are prompt-injection surface that can steer the posted review. Mitigations are otherwise strong (SHA-pinned actions, `persist-credentials: false`, `contents: read`, allowlisted Bash); `id-token: write` is broader than needed and fork PRs fail noisily on the empty secret.
- **MEDIUM — `clang-format-check.yml:9` uses mutable `actions/checkout@v6`** (inconsistent with the SHA-pinning elsewhere) and triggers `on: [push, pull_request]`, double-running for in-repo PRs.
- **MEDIUM — `design/` docs describe a different project.** `GUIDER_*.md` / `PHD2_HEADLESS_PLAYBOOK.md` document "openastro-guider," a hard-fork whose Phase 1 is "drop Windows," while this repo actively ships Windows — committed to the wrong repo or stale.
- **MEDIUM — bundled help is stale.** `help/Advanced_settings.htm` / `Basic_use.htm` still document ZWO/QHY/SBIG cameras and AO/ST4 features the README says were removed.
- **LOW — misc hygiene.** `debian/changelog:1` marks `2.0.0 UNRELEASED` while `CHANGELOG.md` shows it released; orphaned `pre-commit.py` (not wired as a hook); test fixtures `savetest.fit`/`savetest2.fit`/`simimage.fit` committed at repo root; over-broad `.gitignore` `/tmp*`; `scripts/*.py` untested and unmentioned in the README.

---

## Suggested remediation roadmap

1. **Security hardening of `event_server.cpp`** (P1–P4): default to loopback bind + auth token, confine capture paths, add the missing `return` and null checks. Add regression tests driving the RPC layer directly.
2. **INDI disconnect thread-safety** (P5): route `removeDevice`/`serverDisconnected` through `ExecInMainThread`, and move `INDIConfig` device callbacks onto the main thread; fix the `SetButtonEvent` sizing and the video-stacking race.
3. **CI** (P8): add a matrix job that configures + builds + runs `ctest` on Linux (and ideally macOS/Windows); pin `actions/checkout` by SHA; fix the `BUILD_TESTING` docs.
4. **ASCOM cleanup** (P6): add the `vt` check, null-guard the always-callable getters, and give `Variant`/EXCEPINFO proper RAII cleanup.
5. **Repo/packaging cleanup**: remove the six dead scope backends and stale V4L code; fix the vcpkg pin and add `URL_HASH` to googletest; reconcile the Debian INDI Build-Depends; remove or relicense the proprietary framework blobs and update `debian/copyright`.

---

*This audit is a static review; findings marked network-reachable or crash-prone were traced to their call sites but not exploited or reproduced at runtime. Line numbers reflect the audited commit and should be re-confirmed before fixing.*
