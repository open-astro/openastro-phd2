# Unit tests for PHD2 fork
#
# Strategy: most production .cpp files include "phd.h", which transitively
# pulls in wxFrame/Mount/MyFrame and most of the project. To keep test
# binaries small and the test surface focused, we use header substitution:
# tests/include/ comes first on the include path, so production .cpp files
# doing `#include "phd.h"` land on tests/include/phd.h — a minimal header
# that brings in the std/C headers production code expects but skips the
# wx/Mount/MyFrame mountain. The real production .cpp files compile
# unmodified.
#
# Pure-data files (json_parser.cpp) just need the std headers and that's
# the end of the story. Heavier tests (guide algorithms) include
# test_phd_full.h on top, which layers on stub Mount/MyFrame/Debug.

set(PHD_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PHD_TESTS_INCLUDE ${PHD_TESTS_DIR}/include)
set(PHD_TESTS_STUBS ${PHD_TESTS_DIR}/stubs)

# Common interface for every test target: gtest + std headers.
add_library(phd_test_common INTERFACE)
target_link_libraries(phd_test_common INTERFACE GTest::gtest GTest::gtest_main)
target_compile_features(phd_test_common INTERFACE cxx_std_20)

# C/C++ standard says #include "phd.h" finds the source file's own
# directory first - so for json_parser.cpp in src/, the real src/phd.h
# always wins over our shadow no matter what -iquote does. Force-include
# the shadow first so PHD_H_INCLUDED is already set when the production
# source's #include "phd.h" line runs, making it a no-op.
#
# Force-include flag is compiler-specific: GCC/Clang use `-include FILE`,
# MSVC uses `/FI FILE`. MSVC silently ignores `-include` with warning
# D9002, so passing the wrong flag produces a confusing missing-header
# error rather than a clear failure.
if(MSVC)
  target_compile_options(phd_test_common INTERFACE /FI${PHD_TESTS_INCLUDE}/phd.h)
else()
  target_compile_options(phd_test_common INTERFACE -include ${PHD_TESTS_INCLUDE}/phd.h)
endif()
target_include_directories(phd_test_common INTERFACE
  ${PHD_TESTS_INCLUDE}
  ${phd_src_dir}
)

############################################################################
# test_json_parser  --  json_parser.cpp is std-only, so it just needs the
# minimal phd.h substitute.
############################################################################
add_executable(test_json_parser
  ${PHD_TESTS_DIR}/json/test_json_parser.cpp
  ${phd_src_dir}/json_parser.cpp
)
target_link_libraries(test_json_parser PRIVATE phd_test_common)
set_property(TARGET test_json_parser PROPERTY FOLDER "Unit tests")
add_test(NAME test_json_parser COMMAND test_json_parser)

############################################################################
# test_jsonrpc_schema  --  pins the canonical event-server JSON-RPC
# message shapes from the consumer's POV. event_server.cpp has too much
# Mount/Camera coupling to call its formatters directly; this fixture-
# based contract test keeps documented field names from silently rotting.
############################################################################
add_executable(test_jsonrpc_schema
  ${PHD_TESTS_DIR}/event_server/test_jsonrpc_schema.cpp
  ${phd_src_dir}/json_parser.cpp
)
target_link_libraries(test_jsonrpc_schema PRIVATE phd_test_common)
set_property(TARGET test_jsonrpc_schema PROPERTY FOLDER "Unit tests")
add_test(NAME test_jsonrpc_schema COMMAND test_jsonrpc_schema)

############################################################################
# test_alpaca_schema  --  pins the Alpaca request/response shapes the
# alpaca_client / alpaca_discovery modules read and write. Same pattern as
# test_jsonrpc_schema: full round-trip would need wxApp + curl + a mock
# server; here we contract-test the documented JSON shapes via JsonParser.
############################################################################
add_executable(test_alpaca_schema
  ${PHD_TESTS_DIR}/alpaca/test_alpaca_schema.cpp
  ${phd_src_dir}/json_parser.cpp
)
target_link_libraries(test_alpaca_schema PRIVATE phd_test_common)
set_property(TARGET test_alpaca_schema PROPERTY FOLDER "Unit tests")
add_test(NAME test_alpaca_schema COMMAND test_alpaca_schema)

############################################################################
# test_discovery_logic  --  models the host:port parse and dedupe-set
# semantics shared by AlpacaDiscovery and INDIDiscovery, without needing
# real sockets.
############################################################################
add_executable(test_discovery_logic
  ${PHD_TESTS_DIR}/discovery/test_discovery_logic.cpp
)
target_link_libraries(test_discovery_logic PRIVATE phd_test_common)
set_property(TARGET test_discovery_logic PROPERTY FOLDER "Unit tests")
add_test(NAME test_discovery_logic COMMAND test_discovery_logic)

############################################################################
# test_guide_algorithm_math  --  math-twin tests for the simple guide
# algorithms (identity, hysteresis, resistswitch). Each algorithm's
# formula is reimplemented in the test file alongside the production line
# numbers; the test pins the input/output curve. Lowpass / Lowpass2 /
# ZFilter need WindowedAxisStats / ZFilterFactory and are deferred.
############################################################################
add_executable(test_guide_algorithm_math
  ${PHD_TESTS_DIR}/algorithms/test_guide_algorithm_math.cpp
)
target_link_libraries(test_guide_algorithm_math PRIVATE phd_test_common)
set_property(TARGET test_guide_algorithm_math PROPERTY FOLDER "Unit tests")
add_test(NAME test_guide_algorithm_math COMMAND test_guide_algorithm_math)

############################################################################
# test_indi_discovery  --  pins indi_discovery.cpp's pure integer math:
# subnet enumeration prefix clamping, mask construction, loopback skip,
# scan-range network/broadcast skip, and the always-add-loopback contract
# (1.3.0 fix mirroring the Alpaca-side loopback fix). Cross-platform
# socket calls are not exercised; the math is.
############################################################################
add_executable(test_indi_discovery
  ${PHD_TESTS_DIR}/indi/test_indi_discovery.cpp
)
target_link_libraries(test_indi_discovery PRIVATE phd_test_common)
set_property(TARGET test_indi_discovery PROPERTY FOLDER "Unit tests")
add_test(NAME test_indi_discovery COMMAND test_indi_discovery)

############################################################################
# test_imaging_gear  --  imaging_gear.cpp: the imaging-train guide log
# lines (design/IMAGING_GEAR_LOG.md), pixel scale, unknown values and
# Alpaca device selection. std-only.
############################################################################
add_executable(test_imaging_gear
  ${PHD_TESTS_DIR}/imaging_gear/test_imaging_gear.cpp
  ${PHD_TESTS_DIR}/imaging_gear/test_imaging_gear_poller.cpp
  ${phd_src_dir}/imaging_gear.cpp
  ${phd_src_dir}/imaging_gear_poller.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(test_imaging_gear PRIVATE phd_test_common Threads::Threads)
set_property(TARGET test_imaging_gear PROPERTY FOLDER "Unit tests")
add_test(NAME test_imaging_gear COMMAND test_imaging_gear)

############################################################################
# test_alpaca_gain  --  alpaca_gain.cpp: the GainMin/GainMax support
# decision and the 0-100 % -> raw gain mapping CameraAlpaca uses. std-only.
############################################################################
add_executable(test_alpaca_gain
  ${PHD_TESTS_DIR}/camera/test_alpaca_gain.cpp
  ${phd_src_dir}/alpaca_gain.cpp
)
target_link_libraries(test_alpaca_gain PRIVATE phd_test_common)
set_property(TARGET test_alpaca_gain PROPERTY FOLDER "Unit tests")
add_test(NAME test_alpaca_gain COMMAND test_alpaca_gain)
