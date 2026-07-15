#!/usr/bin/env bash
#
# macOS counterpart to run_deb.sh / run_exe.bat — wipes the tmp/ build dir,
# configures CMake for an arm64 build with macOS 26 Tahoe deployment target,
# and points find_package/find_library at the Homebrew prefix. Run
# `make -j$(sysctl -n hw.logicalcpu)` afterward, or pass --build to do it here.
#
# Supported targets: Apple Silicon (arm64) on macOS 26 Tahoe or newer.
# Intel Macs and pre-Tahoe macOS are not supported.
#
# For producing an installable .dmg, see build-dmg.sh (the packaging script).
#
# Optional flags:
#   -g, --debug              Configure a Debug build into tmp_debug/ instead of
#                            a Release build into tmp/.
#   --build, -b              Configure, then build with all logical cores.
#
# Optional env vars:
#   JOBS=N                   Build parallelism (default: detected cores).
#   APPLE_ARCH=arm64         Override architecture (default: arm64). The build
#                            has only been verified for arm64; x86_64/universal
#                            builds need additional Homebrew x86_64 deps.
#
# Usage:
#   ./run_dmg.sh             # configure only (Release into tmp/)
#   ./run_dmg.sh --build     # configure + parallel build
#   ./run_dmg.sh -g --build  # configure + build a Debug variant in tmp_debug/
#

set -e

cd "$(dirname "$0")"

# ---------------------------------------------------------------------------
# Argparse
# ---------------------------------------------------------------------------
TMPDIR_NAME=tmp
BUILDTYPE=Release
DO_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -g|--debug)
            TMPDIR_NAME=tmp_debug
            BUILDTYPE=Debug
            shift
            ;;
        --build|-b)
            DO_BUILD=1
            shift
            ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Usage: $0 [-g|--debug] [--build]" >&2
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Host check: must be macOS arm64.
# ---------------------------------------------------------------------------
if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This script targets macOS only. Use run_deb.sh or run_exe.bat instead." >&2
    exit 1
fi

HOST_ARCH=$(uname -m)
APPLE_ARCH=${APPLE_ARCH:-arm64}
if [[ "$APPLE_ARCH" != "arm64" && "$APPLE_ARCH" != "x86_64" ]]; then
    echo "Unsupported APPLE_ARCH=$APPLE_ARCH; expected arm64 or x86_64." >&2
    exit 1
fi
if [[ "$APPLE_ARCH" == "arm64" && "$HOST_ARCH" != "arm64" ]]; then
    echo "Cross-compiling arm64 from $HOST_ARCH is not supported by this script." >&2
    echo "Run on Apple Silicon, or set APPLE_ARCH=$HOST_ARCH." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Toolchain check.
# ---------------------------------------------------------------------------
for tool in cmake make brew wx-config install_name_tool hdiutil; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        cat >&2 <<EOF
Missing build tool: $tool

Install Homebrew (https://brew.sh) and the required formulae:
  brew install cmake wxwidgets cfitsio libnova gettext eigen

Xcode Command Line Tools provide make, install_name_tool, and hdiutil:
  xcode-select --install
EOF
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# CMAKE_PREFIX_PATH points find_package/find_library at /opt/homebrew on
# Apple Silicon (or /usr/local on Intel) so cfitsio, libnova, gettext, etc.
# resolve without per-module patches.
# ---------------------------------------------------------------------------
BREW_PREFIX=$(brew --prefix)

WXWIN=$(wx-config --prefix)
if [[ ! -d "$WXWIN" ]]; then
    echo "wx-config --prefix returned '$WXWIN' which does not exist." >&2
    echo "Install wxWidgets via Homebrew: brew install wxwidgets" >&2
    exit 1
fi

# Use all cores by default. Override with JOBS=N if you need to throttle.
JOBS=${JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}
export CMAKE_BUILD_PARALLEL_LEVEL=$JOBS

CMAKE_FLAGS=(
    -G "Unix Makefiles"
    "-DCMAKE_OSX_ARCHITECTURES=$APPLE_ARCH"
    "-DCMAKE_BUILD_TYPE=$BUILDTYPE"
    "-DCMAKE_PREFIX_PATH=$BREW_PREFIX"
)

rm -rf "$TMPDIR_NAME"
mkdir "$TMPDIR_NAME"
cd "$TMPDIR_NAME"

echo "Configuring with: cmake ${CMAKE_FLAGS[*]} .."
cmake "${CMAKE_FLAGS[@]}" ..

cd ..

if (( DO_BUILD )); then
    echo "Building with $JOBS parallel jobs..."
    cmake --build "$TMPDIR_NAME" --parallel "$JOBS"
    echo
    echo "Done. Bundle: $TMPDIR_NAME/OpenAstro PHD2.app"
else
    echo
    echo "Configure complete. To build:"
    echo "  cd $TMPDIR_NAME && make -j$JOBS"
    echo "Or rerun with --build to build now."
fi
