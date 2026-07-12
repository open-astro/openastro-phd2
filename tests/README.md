# Tests

Unit tests for the OpenAstro PHD2 fork. Wired into CMake's `enable_testing()`
so they run via `ctest` from the build directory alongside the upstream
Gaussian-process tests under `contributions/MPI_IS_gaussian_process/tests/`.

## Running

```bash
# configure (any of the platform run_* scripts works); from the build dir:
cd tmp
ctest --output-on-failure                       # all tests
ctest -R test_alpaca_schema -V                  # single suite, verbose
ctest -E "GP|GuidePerformance"                  # exclude the slow GP tests

# build a single test binary and run it directly:
cmake --build . --target test_json_parser
./tests/test_json_parser
./tests/test_json_parser --gtest_filter='JsonParser.Parses*'
./tests/test_json_parser --gtest_list_tests     # list cases
```

To turn the test build off entirely (for example, for a packaging build that
doesn't need them), configure with either `-DBUILD_TESTING=OFF` or the fork's
own `-DPHD_BUILD_TESTS=OFF` — both disable the test build. `PHD_BUILD_TESTS`
(referenced in the CHANGELOG) is honoured even when `BUILD_TESTING` is
unavailable for some reason.

GTest is fetched automatically via `FetchContent` (pinned to v1.17.0 in
`thirdparty/thirdparty.cmake`). No system gtest install is needed.

## What's covered

| Executable | Target surface | Style |
|---|---|---|
| `test_json_parser` | `src/json_parser.cpp` — the deserializer behind every Alpaca response and event-server inbound RPC | Direct: compiles the production source into the test binary |
| `test_jsonrpc_schema` | `src/event_server.cpp`'s outgoing message shapes — `Version`, `AppState`, `SettleDone`, `Settling`, `StarSelected`, JSON-RPC envelope, `get_profiles`, `get_profile`, `get_lock_shift_params` | Fixture-based contract: pins the documented field names so a rename in either the production formatter or a downstream consumer (NINA, KStars, web UI) becomes a visible PR diff |
| `test_alpaca_schema` | `src/alpaca_client.cpp` and `src/alpaca_discovery.cpp` JSON shapes — Alpaca standard envelope, `ErrorNumber`/`ErrorMessage` extraction (incl. lenient float→int coerce), camera/telescope `Value` shapes, the property-name fallback for non-standard servers, management API responses, the discovery UDP `{"AlpacaPort": N}` reply | Fixture-based contract |
| `test_discovery_logic` | `host:port` parsing and the `std::set` dedupe model shared by `AlpacaDiscovery::DiscoverServers` and `INDIDiscovery::DiscoverServers` | Model: reproduces the dedupe / parse semantics in `std::string` form, so the actual sockets-bound functions don't have to be runnable in a unit-test process |
| `test_indi_discovery` | `indi_discovery.cpp` INDI-specific math — subnet enumeration prefix clamping (16..30, default /24), mask construction, loopback skip, scan-range network/broadcast skip, the always-add-`127.0.0.1` contract from the 1.3.0 loopback fix | Model |
| `test_guide_algorithm_math` | `result()` / `reset()` for `GuideAlgorithmIdentity`, `GuideAlgorithmHysteresis`, `GuideAlgorithmResistSwitch` | Math-twin: each algorithm's formula is reimplemented in the test alongside the production line numbers; tests pin the input/output curve on a fixed scenario |

## How the build infra works (and why)

Almost every production `.cpp` opens with `#include "phd.h"`, which transitively
pulls in ~30 wxWidgets headers plus `mount.h`, `myframe.h`, `image_math.h`,
`guide_algorithms.h`, etc. — basically the whole project header surface.
Linking that whole graph for a unit test would require a wxApp, a stub Mount
hierarchy, a stub MyFrame, and several hundred lines of supporting scaffold.

For this fork's tests we picked a lighter approach: most production sources
either don't actually USE anything from `phd.h` (they include it
defensively, by convention, often originally for PCH benefit), or what they
DO use is fixture-shaped — JSON in, JSON out — and can be exercised at the
byte level without instantiating the surrounding class.

So `tests/CMakeLists.txt`:

1. Sits under an `add_subdirectory(tests)` placed BEFORE the root
   `include_directories(${phd_src_dir})`, so test targets DON'T inherit
   `src/` on their include path.
2. Provides a stripped-down `tests/include/phd.h` that defines
   `PHD_H_INCLUDED` and pulls in only the std/C headers production code
   expects (`<string>`, `<algorithm>`, `<math.h>`, etc.) plus the
   `POSSIBLY_UNUSED` / `ROUND` macros that show up outside GUI code.
3. Force-includes that shadow on every test target via the
   `-include ${PHD_TESTS_INCLUDE}/phd.h` compile option. This works around
   the C/C++ standard rule that `#include "phd.h"` searches the source
   file's own directory FIRST — so `-iquote` and even `target_include_
   directories(... BEFORE ...)` lose to `src/phd.h` for files compiled out
   of `src/`. The force-include trips the header guard before the source's
   own `#include` line runs.

Net effect: `json_parser.cpp` and the test fixture compile into a tiny
binary with no wx, no globals, no Mount.

For things that genuinely need the wider surface — guide-algorithm
`result()` instantiated against a real Mount, full `event_server.cpp`
formatter exercise, calibration math against a real Scope — the math-twin
or fixture-contract style is what's in tree today; see "Deferred", below.

## Deferred / follow-up

Filed as future work, with notes in the relevant test files explaining the
specific blocker.

- **Lowpass / Lowpass2 / ZFilter algorithms.** The `result()` math reads
  from `WindowedAxisStats` (median + linear fit) or a `ZFilterFactory`
  coefficient set. Mirroring those in a math-twin would mean
  reimplementing a few hundred lines of stats / filter design code.
  Better path: extract the stats class into a header-only target the tests
  can link directly. See the header comment in
  `tests/algorithms/test_guide_algorithm_math.cpp`.
- **Star detection (`star.cpp`, `star_profile.cpp`).** Uses `usImage`
  which is wx + cfitsio coupled. The repo already ships `savetest.fit`
  and `simimage.fit` as fixtures — the right shape for these is a
  golden-file harness that drives a built `phd2.bin`, probably as a small
  Python script invoked from `ctest`.
- **Calibration math (`calibration_assistant.cpp`, `backlash_comp.cpp`).**
  Sign conventions, RA/Dec angle, and step-size selection — high-value
  surface but the math is wired through `Mount` and `Scope` virtuals.
  Either revisit the full stub-layer approach or extract the pure math
  into a header in a follow-up PR.
- **End-to-end test of `CameraAlpaca::Capture()`'s bounded-retry fix
  (e7a91ddc).** Lives inside the `WorkerThread` polling loop; can't be
  unit-tested without an integration harness against a fake Alpaca
  server. The constant is currently asserted to remain at 3000 ms in
  `test_alpaca_schema.cpp::AlpacaCameraAbortContract.DocumentedBoundedRetryBehavior`
  as a deliberate placeholder.
