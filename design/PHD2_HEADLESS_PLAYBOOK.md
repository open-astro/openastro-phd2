# PHD2 Headless Playbook — `openastro-guider`

> **Note:** This document is the plan for the **separate**
> `open-astro/openastro-guider` project (a Linux-only, headless, Alpaca-only
> downstream fork). It is kept here as reference/tracking material only and is
> **not** the roadmap for this `openastro-phd2` repo.

Authoritative plan for turning the inherited PHD2 codebase (`openastro-phd2` fork) into
**`openastro-guider`**: a Linux-only, headless, Alpaca-only guiding daemon that OpenAstro
ARA drives entirely over PHD2's API. This document is to `openastro-guider` what
`design/PORT_PLAYBOOK.md` is to the ARA port — read it before touching the tree.

Lineage: PHD2 (Open PHD Guiding) → `open-astro/openastro-phd2` (added Alpaca + INDI) →
`open-astro/openastro-guider` (this repo; headless, Alpaca-only). `openastro-phd2` remains
as the GUI/INDI reference and is configured as the `upstream` git remote.

---

## 0. Operating rules

1. **No scope creep.** This is a *strip + headless-enable*, not a rewrite. Keep the guiding
   engine, calibration math, guide algorithms, dark/defect-map logic, and the event-server
   protocol intact. Remove platforms and backends; do not "improve" working guiding logic.
2. **Keep wxWidgets.** wxBase (threads, config, strings, sockets) and the existing GUI code
   stay. We add a **headless run mode** (no window shown / daemonized); we do **not** rip out
   wx. Full wx removal is explicitly out of scope.
3. **No half-finished states.** Each commit leaves the tree buildable for the headless target.
   Work on a per-PR feature branch cut from `master`; merge back to `master` via PR (same
   GitHub Flow + merge-gate as the ARA port — see §6).
4. **Strip before refactor.** Delete platform/backends first (smaller surface), then add the
   headless mode and fill API gaps. Mirrors ARA Phase 0.5 (delete-before-rename).
5. **Verify continuously.** After each phase, the §5 build + GTest gate must be green for
   everything done so far before the next phase starts.
6. **Commit cadence.** One logical unit per commit. Messages: `strip(<area>): <what>` /
   `feat(headless): <what>` / `feat(api): <what>`. Never `--no-verify`, never force-push.
7. **Cite when stuck.** Leave a `// TODO(guider): <one sentence>` + a compiling placeholder,
   log it in `design/GUIDER_TODO.md`, and move on.

---

## 1. Goal & architecture

`openastro-guider` is a **guiding engine, not an app**. It runs headless on the Pi (Linux
arm64, the same target as AlpacaBridge) and is controlled remotely:

```
  ARA (Flutter UI: guiding graph, dark library, algo picker, calibration, "the fancy stuff")
        │
        │  PHD2 event-server API  (JSON-RPC over TCP :4400)  — control + event stream
        ▼
  openastro-guider  (headless wx daemon: capture loop, calibration, guide algorithms)
        │
        │  ASCOM Alpaca  (HTTP)
        ▼
  AlpacaBridge  (guide camera + mount)
```

- **ARA owns the UI.** No guiding UI is reimplemented in the daemon — ARA renders everything
  from API state + the event stream and issues commands via RPC.
- **Equipment is Alpaca-only.** The guide camera and mount are reached through AlpacaBridge.
  INDI and all vendor/ASCOM-on-Windows backends are removed.
- **The daemon is "headless PHD2."** Same proven split used by KStars/Ekos and ASIAIR
  (run the guider headless, drive it over the event server).

---

## 2. What's dropped vs kept

**Dropped**
- **macOS** — `build-dmg.sh`, `run_dmg.sh`, `Info.plist.in`, `run_phd2_macos`, mac serial /
  framework backends, `extra_frameworks/`, Cocoa-specific code paths.
- **Windows** — `build-exe.ps1`, `run_exe.bat`, `phd2.iss.in`, `phd.rc`, `WinLibs/`,
  ASCOM/win32 camera + mount + serial backends, `*_win32.cpp`.
- **INDI backend** — `*_indi.cpp` (camera/mount/rotator/focuser/config), `indi_gui.cpp`,
  `indi_discovery.cpp`, libindi build dep, the `USE_SYSTEM_LIBINDI` / thirdparty-INDI
  ExternalProject. (Equipment comes via Alpaca instead.)
- **Locale GUI bundling** that blocks a headless build (the `messages.mo` copy step that
  broke the first CI run) — pruned or made non-fatal under the headless build.

**Kept**
- wxWidgets (wxBase + GUI code, just not shown), the capture/guide loop, calibration,
  **all guide algorithms**, dark-frame + bad-pixel-map logic, the **event server (:4400)**,
  the Alpaca client equipment path, the GTest suite under `tests/`.

---

## 3. Phase plan

| Phase | Scope | Gate |
|---|---|---|
| **0** | Repo + tracking files (`design/GUIDER_TODO.md`, `design/GUIDER_DECISIONS.md`, `design/API_CONTRACT.md`) + **headless-build CI baseline** (build current tree + run GTest suite on Linux). | CI green |
| **1** | **Drop Windows** — delete win build scripts, WinLibs/, `.iss`, `phd.rc`, win32 backends; strip `#ifdef __WINDOWS__` paths; remove win bits from CMake. | builds Linux |
| **2** | **Drop macOS** — delete dmg/mac scripts, Info.plist, mac frameworks/backends; strip `#ifdef __APPLE__` paths. | builds Linux |
| **3** | **Drop INDI** — delete `*_indi.cpp`, indi_gui/discovery, libindi dep + thirdparty INDI; remove INDI from camera/mount factories; Alpaca becomes the only equipment backend. | builds + tests |
| **4** | **Headless run mode** — a `--headless`/daemon entry that runs the guide engine + event server without showing a window (Xvfb-free where feasible; document the display strategy). systemd unit + `debian/` updated for the daemon. | daemon starts, event server reachable |
| **5** | **API gap-fill** — expose GUI-only features over the event server so ARA can drive them. Known gaps: **dark-frame / bad-pixel-map library management** (create/select/delete), guide-algorithm selection + param get/set, calibration management, exposure/ROI. Each new method logged in `design/API_CONTRACT.md`. | new RPC methods covered by tests |
| **6** | **ARA integration validation** — ARA client ↔ headless guider end-to-end: connect, calibrate, guide, dither/settle, dark library, algorithm change, star-lost recovery — against Alpaca simulators. | integration smoke green |

**Phase boundaries** get a `phase-N-complete` tag (per §6). A phase may sub-split (e.g. Ph 3
INDI removal by device type) when a single PR gets too large.

---

## 4. API contract (`design/API_CONTRACT.md`)

PHD2's event server already covers much of what ARA needs — `guide`, `dither`,
`set_exposure`, `get/set_algo_param`, `set_lock_position`, `flip_calibration`, connect state,
and the event stream (`GuideStep`, `SettleDone`, `StarLost`, `CalibrationComplete`, …). Phase 5
fills the gaps. Treat `API_CONTRACT.md` as append-only: one entry per method ARA depends on or
that we add, with request/response shape + the ARA call site.

The biggest known gap is **dark/defect-map library management**, which today lives in GUI
dialogs with no RPC equivalent — it must become API-driven for a headless daemon.

---

## 5. CI / test gate

- **Runner:** `ubuntu-24.04` (x86_64 is fine for the logic; the *deployment* target is arm64,
  validated separately once the build is slimmed). Add an `ubuntu-24.04-arm` job once INDI/GUI
  weight is gone and the build is fast.
- **Build:** CMake configure + build of the headless target with `OPENSOURCE_ONLY=1`
  (no proprietary SDKs) and `USE_SYSTEM_GTEST=1`.
- **Tests:** the existing **GTest/CTest suite** is the gate — `alpaca`, `discovery`, `json`,
  `guide-algorithm` targets are exactly the headless-relevant logic. `ctest --output-on-failure`.
- **Lint:** keep the inherited `clang-format-check`. Consider `cppcheck` once the tree is slimmed.
- As each phase removes weight (INDI, GUI bundling), CI gets faster and more reliable.

---

## 6. Branch / PR rhythm + safety

Same model as the rest of the org (see ARA `COMMIT-PR-RULES.md`), already settled 2026-06-02:

- **Direct-to-master GitHub Flow.** Branch from `master` → PR → merge back to `master` →
  delete branch. No integration branch.
- **Branch naming:** `phase/<N>[-<letter>]-<short-name>` (e.g. `phase/3-drop-indi-camera`).
- **Merge-gate:** required CI green + review pass + clean self-review against the phase scope.
- **Branch protection:** a ruleset on `master` (require PR + the CI checks, block deletion +
  force-push, admin bypass) — applied once the headless-build CI reports its first check.
- `upstream` remote = `openastro-phd2` (pull future fixes selectively); `origin` = this repo.

---

## 7. Tracking files

- `design/GUIDER_DECISIONS.md` — append-only log of every non-obvious strip/headless decision.
- `design/GUIDER_TODO.md` — every `TODO(guider)` / placeholder left in code, grouped by phase.
- `design/API_CONTRACT.md` — append-only RPC contract (see §4).
- `design/GUIDER_PROGRESS.md` — single-page status (current phase, last merged PR, next step),
  read first on resume.
