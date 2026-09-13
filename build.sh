#!/usr/bin/env bash
# Configures and builds the iOS device target with CMake + Xcode.
#
# Requires full Xcode (not just Command Line Tools) for the iphoneos SDK.
#
# For the simulator instead (no device/provisioning needed to test), use
# test-sim.sh, which builds, installs, and launches in one step.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$HERE/build-ios/device"
DEPLOYMENT_TARGET=15.0

cmake -S "$HERE" -B "$BUILD_DIR" -G Xcode \
	-DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/Modules/ios.toolchain.cmake" \
	-DPLATFORM=OS64 \
	-DDEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
	-DSRB2_CONFIG_EXTERNAL_ASSETS=ON \
	-DCMAKE_BUILD_TYPE=Release

# -allowProvisioningUpdates lets xcodebuild auto-create/download a
# provisioning profile for automatic signing on the command line -- Xcode's
# GUI does this silently when you hit Run, but xcodebuild doesn't by
# default, and fails with "No profiles for '<bundle id>' were found"
# without it.
cmake --build "$BUILD_DIR" --config Release -- -quiet -allowProvisioningUpdates

# assets/CMakeLists.txt's asset-copying (SRB2_CONFIG_EXTERNAL_ASSETS) is
# wired through install(DIRECTORY ...), which only runs on `cmake --install`,
# not `cmake --build` -- so without this, the built .app never gets
# srb2.srb/patch.dta/etc. copied in. Installing with --prefix pointed back at
# the build product directory augments the just-built bundle in place rather
# than producing a separate copy elsewhere.
cmake --install "$BUILD_DIR" --config Release --prefix "$BUILD_DIR/bin/Release"

# Package as a .ipa for sideloading (AltStore/Sideloadly/LiveContainer/etc).
# An .ipa is just a zip with the .app under a top-level "Payload/" folder --
# NOT the .app zipped directly (a common mistake, e.g. via Finder's
# "Compress" on the .app itself, which sideloading tools will reject since
# there's no Payload folder to find).
APP_PATH="$(find "$BUILD_DIR/bin/Release" -maxdepth 1 -iname "*.app" | head -1)"
if [ -z "$APP_PATH" ]; then
	echo "error: no .app found under $BUILD_DIR/bin/Release, can't package .ipa" >&2
	exit 1
fi
APP_NAME="$(basename "$APP_PATH" .app)"
IPA_STAGE="$(mktemp -d)"
mkdir -p "$IPA_STAGE/Payload"
cp -R "$APP_PATH" "$IPA_STAGE/Payload/"
IPA_PATH="$BUILD_DIR/bin/Release/$APP_NAME.ipa"
rm -f "$IPA_PATH"
# COPYFILE_DISABLE prevents macOS's zip from writing __MACOSX/._* AppleDouble
# metadata entries, which some sideloading tools trip over; -x '.*' skips
# any stray dotfiles (e.g. .DS_Store) that may have been picked up.
(cd "$IPA_STAGE" && COPYFILE_DISABLE=1 zip -qr "$IPA_PATH" Payload -x '.*')
rm -rf "$IPA_STAGE"
echo "==> packaged: $IPA_PATH"
