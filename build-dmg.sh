#!/usr/bin/env bash
# PHD2 .dmg Build Script for macOS 26 Tahoe (Apple Silicon)
#
# Supported host: macOS 26 Tahoe or newer on Apple Silicon (arm64). Intel Macs
# and pre-Tahoe macOS are not supported by this fork.
#
# Builds PHD2 and produces:
#   openastro-phd2-<version>-arm64.dmg
#
# All Homebrew dylibs that PHD2 links against (wxWidgets, cfitsio, libnova
# transitive image libs, etc.) are copied into "OpenAstro PHD2.app"/Contents/Frameworks/
# and their install names rewritten via install_name_tool, so the DMG runs on
# end-user machines without Homebrew installed.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }
step()  { echo -e "${CYAN}[STEP]${NC} $*"; }

# Project root (directory containing this script)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
cd "$ROOT_DIR"

# ---------------------------------------------------------------------------
# Host check
# ---------------------------------------------------------------------------
if [[ "$(uname -s)" != "Darwin" ]]; then
    err "build-dmg.sh runs on macOS only. Use build-deb.sh or build-exe.ps1 for other platforms."
fi

HOST_ARCH=$(uname -m)
if [[ "$HOST_ARCH" != "arm64" ]]; then
    err "Unsupported host architecture: $HOST_ARCH. This fork builds Apple Silicon (arm64) only."
fi

# Tahoe-or-newer check (deployment target is 26.0). Allow override via env for CI.
MAJOR_VER=$(sw_vers -productVersion | cut -d. -f1)
if [[ -z "${ALLOW_OLD_MACOS:-}" ]] && (( MAJOR_VER < 26 )); then
    err "macOS $(sw_vers -productVersion) detected; this fork requires macOS 26 Tahoe or newer. Override with ALLOW_OLD_MACOS=1."
fi
info "Building for macOS $(sw_vers -productVersion) arm64"

# ---------------------------------------------------------------------------
# Build dependencies (Homebrew). Mirrors what run_dmg.sh / CMakeLists need.
# ---------------------------------------------------------------------------
BUILD_DEPS=(cmake wxwidgets cfitsio libnova gettext)

check_deps() {
    if ! command -v brew >/dev/null 2>&1; then
        warn "Homebrew not found. Install from https://brew.sh first."
        return 1
    fi
    local missing=()
    for pkg in "${BUILD_DEPS[@]}"; do
        brew list --formula "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    if (( ${#missing[@]} > 0 )); then
        warn "Missing Homebrew formulae: ${missing[*]}"
        echo ""
        echo "Install build deps:"
        echo "  brew install ${BUILD_DEPS[*]}"
        echo ""
        echo "Or run: $0 --install-deps"
        return 1
    fi
    return 0
}

install_deps() {
    step "Installing Homebrew build dependencies..."
    if ! command -v brew >/dev/null 2>&1; then
        err "Homebrew is not installed. Install from https://brew.sh and rerun."
    fi
    brew install "${BUILD_DEPS[@]}"
    info "Build dependencies installed."
}

# ---------------------------------------------------------------------------
# Extract version from version.md (single source of truth)
# ---------------------------------------------------------------------------
read_version() {
    local version_md="${ROOT_DIR}/version.md"
    if [[ ! -f "$version_md" ]]; then
        err "Cannot find version.md at: $version_md"
    fi
    FULL_VERSION=$(grep -E '^[[:space:]]*[0-9]+\.[0-9]+\.[0-9]+([A-Za-z0-9._-]*)[[:space:]]*$' "$version_md" | head -1 | sed -E 's/^[[:space:]]*//; s/[[:space:]]*$//')
    if [[ -z "$FULL_VERSION" ]]; then
        err "Could not extract version from version.md (expected line like 1.2.3 or 1.2.3rc1)"
    fi
}

# ---------------------------------------------------------------------------
# is_external_dylib path
#   Returns 0 if the given path is a Homebrew/MacPorts/local dylib that needs
#   to be bundled into the .app, 1 if it's a system dylib (/usr/lib, /System)
#   or already-bundled (@executable_path/...).
# ---------------------------------------------------------------------------
is_external_dylib() {
    local p="$1"
    case "$p" in
        @executable_path/*|@loader_path/*)          return 1 ;;
        /usr/lib/*|/System/Library/*)               return 1 ;;
        @rpath/*)                                   return 0 ;;
        /opt/homebrew/*|/usr/local/*|/opt/local/*)  return 0 ;;
        *)                                          return 0 ;;
    esac
}

# ---------------------------------------------------------------------------
# resolve_dylib_path path
#   Echoes a real on-disk path for the given dylib reference. Handles:
#   - absolute paths: returned as-is
#   - @rpath/foo.dylib: probe Homebrew's lib dir (where Homebrew puts symlinks
#     for every formula). Sufficient for our case since every external dep
#     comes from a Homebrew formula. If we ever pick up a non-Homebrew
#     @rpath dep, we'd need to walk the parent's LC_RPATH commands instead.
# ---------------------------------------------------------------------------
resolve_dylib_path() {
    local p="$1"
    case "$p" in
        @rpath/*)
            local base=${p#@rpath/}
            local cand="/opt/homebrew/lib/$base"
            if [[ -e "$cand" ]]; then
                echo "$cand"
            else
                warn "Could not resolve $p (not found at $cand)"
                echo ""
            fi
            ;;
        *)
            echo "$p"
            ;;
    esac
}

# ---------------------------------------------------------------------------
# bundle_dylib src_dylib app_path
#   Recursively bundles a Homebrew/external dylib (and its transitive
#   external deps) into <app>/Contents/Frameworks/, rewriting install_names
#   so loads resolve via @executable_path/../Frameworks/<name>.
#   Idempotent — re-entry on an already-bundled name is a no-op.
# ---------------------------------------------------------------------------
declare -a BUNDLED_NAMES=()
# bundled_name_for path -> echoes the basename that bundle_dylib uses for `path`
#
# A dylib's load entry from otool -L might be a symlink path (e.g.
# /opt/homebrew/opt/wxwidgets/lib/libwx_baseu-3.3.dylib), but the install_name
# baked into the file is the fully-versioned canonical path (e.g.
# /opt/homebrew/Cellar/wxwidgets/3.3.2/lib/libwx_baseu-3.3.2.0.0.dylib).
# bundle_dylib copies the file under the install_name's basename, so any
# install_name_tool -change in a parent dylib must reference THAT name, not
# the symlink basename — otherwise the rewritten load points at a path that
# doesn't exist in Frameworks/.
bundled_name_for() {
    local resolved real install_name
    resolved=$(resolve_dylib_path "$1")
    [[ -n "$resolved" ]] || { echo ""; return; }
    real=$(/usr/bin/python3 -c "import os,sys;print(os.path.realpath(sys.argv[1]))" "$resolved")
    install_name=$(otool -D "$real" | tail -n 1)
    basename "$install_name"
}

bundle_dylib() {
    # All loop-iteration vars must be `local` — `read -r dep` without `local`
    # would clobber the parent stack frame's `dep` during recursion, and the
    # parent's post-recursion install_name_tool -change would then target the
    # wrong load entry (typically a /usr/lib system path the inner call last
    # iterated past), silently corrupting the bundle.
    local src="$1" app="$2" frameworks="$2/Contents/Frameworks"
    local resolved real install_name load_name dep dep_load n
    mkdir -p "$frameworks"

    resolved=$(resolve_dylib_path "$src")
    [[ -n "$resolved" ]] || return
    real=$(/usr/bin/python3 -c "import os,sys;print(os.path.realpath(sys.argv[1]))" "$resolved")
    install_name=$(otool -D "$real" | tail -n 1)
    [[ -n "$install_name" ]] || { warn "No install_name on $real, skipping."; return; }
    load_name=$(basename "$install_name")

    for n in "${BUNDLED_NAMES[@]}"; do
        [[ "$n" == "$load_name" ]] && return
    done
    BUNDLED_NAMES+=("$load_name")

    cp "$real" "$frameworks/$load_name"
    chmod u+w "$frameworks/$load_name"
    # Do NOT discard install_name_tool stderr: a failed rewrite here leaves a
    # dylib whose id/loads still point outside the bundle, which only shows up
    # as a launch crash on a Homebrew-less Mac. Fail loud instead.
    install_name_tool -id "@executable_path/../Frameworks/$load_name" "$frameworks/$load_name" \
        || err "install_name_tool -id failed on $frameworks/$load_name"

    while read -r dep; do
        [[ -z "$dep" ]] && continue
        if is_external_dylib "$dep"; then
            dep_load=$(bundled_name_for "$dep")
            # An empty bundled name means bundled_name_for could not resolve the
            # dep (e.g. an unresolvable @rpath). Rewriting with an empty target
            # would produce "@executable_path/../Frameworks/" and silently
            # corrupt the load command — skip and warn instead.
            if [[ -z "$dep_load" ]]; then
                warn "Skipping install-name rewrite for '$dep' in $load_name: could not resolve its bundled basename."
                continue
            fi
            bundle_dylib "$dep" "$app"
            install_name_tool -change "$dep" "@executable_path/../Frameworks/$dep_load" "$frameworks/$load_name" \
                || err "install_name_tool -change failed rewriting '$dep' in $load_name"
        fi
    done < <(otool -L "$real" | tail -n +2 | awk '{print $1}' | grep -vE "^$real$" || true)

    # install_name_tool changes invalidate the dylib's ad-hoc signature; re-sign.
    codesign --force --sign - "$frameworks/$load_name" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# bundle_app_dylibs app_path
#   Scans the executable's external dylib loads, bundles each (and its
#   transitive deps) into Frameworks/, then rewrites the executable's load
#   paths to point at the bundled copies. Re-signs the executable.
# ---------------------------------------------------------------------------
bundle_app_dylibs() {
    # Binary basename inside Contents/MacOS/ matches CMake's OUTPUT_NAME.
    # Pre-2.0.0 it was "PHD2"; the rebrand commit set OUTPUT_NAME to
    # "OpenAstro PHD2", so this path picks up the new name via APP_EXE.
    # If the file isn't there, fail loud — silent failure here means we
    # ship an unbundled binary with raw /opt/homebrew/... loads, which
    # crashes at launch on any Mac without Homebrew.
    local app="$1" exe="$1/Contents/MacOS/${APP_EXE}"
    local dep dep_load
    [[ -f "$exe" ]] || err "Bundle binary not found: $exe (did CMake OUTPUT_NAME drift from APP_EXE?)"
    BUNDLED_NAMES=()

    while read -r dep; do
        [[ -z "$dep" ]] && continue
        if is_external_dylib "$dep"; then
            dep_load=$(bundled_name_for "$dep")
            if [[ -z "$dep_load" ]]; then
                warn "Skipping install-name rewrite for '$dep' in $(basename "$exe"): could not resolve its bundled basename."
                continue
            fi
            bundle_dylib "$dep" "$app"
            install_name_tool -change "$dep" "@executable_path/../Frameworks/$dep_load" "$exe" \
                || err "install_name_tool -change failed rewriting '$dep' in the app binary"
        fi
    done < <(otool -L "$exe" | tail -n +2 | awk '{print $1}')

    codesign --force --sign - "$exe" 2>/dev/null || true
    info "Bundled ${#BUNDLED_NAMES[@]} dylib(s) into $app/Contents/Frameworks/"
}

# ---------------------------------------------------------------------------
# check_library_dependencies: sanity-check that PHD2 only loads system dylibs
# (/usr/lib, /System/Library/Frameworks) and bundled ones (@executable_path).
# Anything pointing at /opt/homebrew or /usr/local at this stage means
# bundle_app_dylibs missed something.
# ---------------------------------------------------------------------------
check_library_dependencies() {
    local app="$1"
    local exe="$app/Contents/MacOS/${APP_EXE}"
    local stray
    [[ -f "$exe" ]] || err "Bundle binary not found: $exe (did CMake OUTPUT_NAME drift from APP_EXE?)"
    stray=$(otool -L "$exe" | tail -n +2 | awk '{print $1}' \
        | grep -vE '^@(executable_path|rpath|loader_path)/' \
        | grep -vE '^/usr/lib/' \
        | grep -vE '^/System/Library/' || true)
    if [[ -n "$stray" ]]; then
        err "Unbundled dylib dependencies in $exe:
$stray"
    fi
    # Recurse one level into bundled dylibs.
    if [[ -d "$app/Contents/Frameworks" ]]; then
        local bad=
        for dylib in "$app"/Contents/Frameworks/*.dylib; do
            [[ -e "$dylib" ]] || continue
            local sub
            sub=$(otool -L "$dylib" | tail -n +2 | awk '{print $1}' \
                | grep -vE '^@(executable_path|rpath|loader_path)/' \
                | grep -vE '^/usr/lib/' \
                | grep -vE '^/System/Library/' || true)
            if [[ -n "$sub" ]]; then
                bad+="$(basename "$dylib"):
$sub
"
            fi
        done
        if [[ -n "$bad" ]]; then
            err "Unbundled dylib dependencies in bundled libraries:
$bad"
        fi
    fi
    info "All dylib dependencies resolved cleanly."
}

# ---------------------------------------------------------------------------
# Parse options
# ---------------------------------------------------------------------------
INSTALL_DEPS=false
CLEAN=false
FORCE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install-deps) INSTALL_DEPS=true ;;
        --clean)        CLEAN=true ;;
        --force)        FORCE=true ;;
        -h|--help)
            cat <<EOF
Usage: $0 [OPTIONS]

Build PHD2 and create a .dmg disk image for macOS arm64.
Result: openastro-phd2-<version>-arm64.dmg in tmp/

Options:
  --install-deps   Install Homebrew build dependencies, then exit.
  --clean          Remove tmp/ before building.
  --force          Skip the build-dependency check entirely.
  -h, --help       Show this help.
EOF
            exit 0
            ;;
        *) err "Unknown option: $1" ;;
    esac
    shift
done

if "$INSTALL_DEPS"; then
    install_deps
    exit 0
fi

# ---------------------------------------------------------------------------
# Pre-flight
# ---------------------------------------------------------------------------
if ! "$FORCE"; then
    if ! check_deps; then
        err "Install dependencies and rerun, or use: $0 --install-deps  (or $0 --force to try anyway)"
    fi
fi

read_version
info "Detected version: ${FULL_VERSION}"
DMG_NAME="openastro-phd2-${FULL_VERSION}-arm64.dmg"

# ---------------------------------------------------------------------------
# Clean if requested
# ---------------------------------------------------------------------------
if "$CLEAN"; then
    step "Cleaning tmp/..."
    rm -rf tmp
    info "Clean done."
fi

# ---------------------------------------------------------------------------
# Configure + build (delegates to run_dmg.sh)
# ---------------------------------------------------------------------------
step "Configuring + building PHD2..."

# Build translation targets sequentially first to avoid an intermittent
# parallel-build race in gettext-generated files, then build the rest in
# parallel. INDI is built first because we removed the indi build dependency
# from the phd2 target (per project convention) so phd2 doesn't wait on it
# when iterating; building indi explicitly here keeps the DMG path correct.
"${ROOT_DIR}/run_dmg.sh"

cd "${ROOT_DIR}/tmp"

translation_targets=()
while read -r locale; do
    translation_targets+=("${locale}_translation")
done < <(find ../locale -name messages.po | awk -F/ '{print $3}')

if (( ${#translation_targets[@]} > 0 )); then
    step "Building translations sequentially..."
    make "${translation_targets[@]}"
fi

cores=$(sysctl -n hw.logicalcpu)
step "Building INDI client (parallel x$cores)..."
make -j"$cores" indi
step "Building PHD2 (parallel x$cores)..."
make -j"$cores"

# The 2.0.0 rebrand renamed the CMake OUTPUT_NAME to "OpenAstro PHD2" so the
# .app file in /Applications no longer collides on disk with upstream PHD2's
# bundle. CMake's MACOSX_BUNDLE target uses OUTPUT_NAME for both the .app
# filename AND the binary inside Contents/MacOS/ — so APP_BUNDLE and APP_EXE
# move in lockstep and must match what CMakeLists.txt sets. Both names are
# captured here so the rest of the script (and the AppleScript heredoc, and
# the bundle/check helpers above) reference them via these variables and
# keep the spaces quoted consistently.
APP_BUNDLE="OpenAstro PHD2.app"
APP_EXE="OpenAstro PHD2"

# ---------------------------------------------------------------------------
# Bundle Homebrew dylibs into <APP_BUNDLE>/Contents/Frameworks/ so the .app
# runs on machines without Homebrew. Recursive: walks transitive deps.
# ---------------------------------------------------------------------------
step "Bundling Homebrew dylibs into ${APP_BUNDLE}/Contents/Frameworks/..."
bundle_app_dylibs "$APP_BUNDLE"

step "Checking library dependencies..."
check_library_dependencies "$APP_BUNDLE"

# ---------------------------------------------------------------------------
# Tests — always run; no skip flag. A test failure aborts the .dmg build
# (set -e is on at the top of the script). To skip the suite entirely,
# reconfigure with -DPHD_BUILD_TESTS=OFF.
# ---------------------------------------------------------------------------
step "Running ctest..."
ctest --output-on-failure

# ---------------------------------------------------------------------------
# DMG creation — "drag OpenAstro PHD2 to Applications" install window.
#
# Three-step process:
#   1. Build a writable scratch DMG (UDRW) seeded with the .app bundle.
#   2. Mount it, add the /Applications symlink and .background/ image, then
#      drive Finder via AppleScript to set the icon view: 640×360 window,
#      icon size 128, our PNG as background, the .app on the left and the
#      Applications shortcut on the right.
#   3. Detach and convert to a compressed read-only UDZO for shipping.
#
# Note: the first time you run this on a fresh macOS user account, Finder
# will prompt for "Allow Terminal to control Finder" via System Settings →
# Privacy & Security → Automation. If that prompt is denied the DMG still
# builds correctly; it just falls back to a default Finder window layout.
# ---------------------------------------------------------------------------
step "Creating ${DMG_NAME}..."

BG_IMG="${ROOT_DIR}/packaging/macos/background.png"
[[ -f "$BG_IMG" ]] || err "Missing DMG background image: $BG_IMG"

STAGING_DMG="phd2-staging-$$.dmg"
# Volume label users see when the DMG is mounted (Finder sidebar, /Volumes/...).
# Pre-2.0.0 was just "PHD2" which was inconsistent with the rebranded .app
# inside; now matches the bundle's display name.
VOLNAME="OpenAstro PHD2"
MOUNTPOINT="/Volumes/${VOLNAME}"

rm -f "$DMG_NAME" "$STAGING_DMG"
# Clean up any leftover mount from a previous failed run.
if [[ -d "$MOUNTPOINT" ]]; then
    hdiutil detach "$MOUNTPOINT" -force >/dev/null 2>&1 || true
fi

# Size the writable DMG to fit the .app + ~20 MB headroom for the symlink,
# background image, and HFS+ metadata.
APP_KB=$(du -sk "$APP_BUNDLE" | awk '{print $1}')
DMG_KB=$(( APP_KB + 20480 ))

hdiutil create \
    -srcfolder "$APP_BUNDLE" \
    -volname "$VOLNAME" \
    -fs HFS+ \
    -fsargs "-c c=64,a=16,e=16" \
    -format UDRW \
    -size "${DMG_KB}k" \
    "$STAGING_DMG"

step "Mounting staging DMG to lay out window..."
ATTACH_LOG=$(mktemp)
hdiutil attach "$STAGING_DMG" -readwrite -noverify -noautoopen >"$ATTACH_LOG"
DEVICE=$(awk '/Apple_HFS/ {print $1; exit}' "$ATTACH_LOG")
rm -f "$ATTACH_LOG"
[[ -n "$DEVICE" ]] || err "Could not determine device node for mounted staging DMG."

# From here until the explicit detach below the staging DMG is attached. If
# anything aborts in between (e.g. a failing command under set -e), make sure
# the image is detached so we don't leave a dangling /Volumes mount and a busy
# device node that blocks the next run. Cleared right after the real detach.
detach_staging() { hdiutil detach "$DEVICE" -force >/dev/null 2>&1 || true; }
trap detach_staging EXIT

# hdiutil attach returns once the kernel mount is up, but Finder learns about
# the volume asynchronously via diskarbitrationd. On Tahoe the race is tight
# enough that the AppleScript below raises -1728 ("Can't get disk PHD2")
# without this settle. Poll for the mountpoint to be readable from userspace
# before talking to Finder.
for _ in 1 2 3 4 5; do
    [[ -d "$MOUNTPOINT" ]] && break
    sleep 1
done

ln -s /Applications "${MOUNTPOINT}/Applications"
mkdir -p "${MOUNTPOINT}/.background"
cp "$BG_IMG" "${MOUNTPOINT}/.background/background.png"

# AppleScript drives Finder to set the icon view. Bounds are
# {left, top, right, bottom} in *outer* screen coords. The title bar eats
# ~28pt of that, so to give the 600-logical-px-tall background image a
# matching 600pt content area we set the outer height to 628 (600+28).
# Without this offset Finder shows a vertical scrollbar to expose the
# bottom strip of the image it would otherwise clip.
# Icon positions are in content-area coords (0,0 = top-left of the
# content area, below the title bar).
#
# Note: backticks in this heredoc would be evaluated by bash as command
# substitution (`text color` previously produced "text: command not found").
# Keep comments backtick-free.
osascript <<APPLESCRIPT || warn "Finder layout skipped (osascript failed, e.g. denied Automation prompt); shipping DMG with the default window layout."
tell application "Finder"
    -- Give Finder another beat to register the freshly attached disk so
    -- the "tell disk" below resolves cleanly on Tahoe.
    delay 1
    tell disk "${VOLNAME}"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 900, 748}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 128
        -- Standard 12pt label size. We'd rather force white text so labels
        -- read cleanly on the dark navy band, but macOS Tahoe's Finder
        -- dropped the "text color" property on icon view options (raises
        -- -1728) and there's no AppleScript path that still works. The
        -- try/end try keeps the script working if Apple ever restores
        -- the property on a future macOS.
        set text size of viewOptions to 12
        try
            set text color of viewOptions to {65535, 65535, 65535}
        end try
        set background picture of viewOptions to file ".background:background.png"
        set position of item "${APP_BUNDLE}" of container window to {175, 450}
        set position of item "Applications" of container window to {525, 450}
        update without registering applications
        delay 2
        close
    end tell
end tell
APPLESCRIPT

sync
hdiutil detach "$DEVICE" -force
trap - EXIT

step "Compressing to ${DMG_NAME}..."
hdiutil convert "$STAGING_DMG" -format UDZO -imagekey zlib-level=9 -o "$DMG_NAME"
rm -f "$STAGING_DMG"
chmod 644 "$DMG_NAME"

DMG_PATH="${ROOT_DIR}/tmp/${DMG_NAME}"
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}.dmg built successfully${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "  ${CYAN}${DMG_PATH}${NC}"
ls -la "$DMG_PATH"
echo ""
echo "Mount with: open '$DMG_PATH'"
echo "Drag '${APP_BUNDLE}' to /Applications to install."
