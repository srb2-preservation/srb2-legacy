#!/bin/bash
# Build and Package SRB2 Legacy for iOS
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
useassets=true
simulator=false
skipbuild=false
BUNDLE_NAME="SRB2 Legacy"
BUNDLE_ID="org.stjr.srb2legacy"
DEPLOYMENT_TARGET=15.0
devicename=""

for args in "$@"; do
	case "$args" in
		"--noassets")
			useassets=false
			shift
			;;
		"--skipbuild")
			skipbuild=true
			shift
			;;
		"--simulator"|"-S")
			simulator=true
			shift
			;;
		*)
			devicename="$1"
			shift
			;;
	esac
done

# Prepare assets with LFS
if [[ ! "$useassets" == true || -d "assets/installer" ]]; then
	echo "Skipping asset cloning"
else
	git clone https://codeberg.org/srb2-preservation/assets.git -b legacy assets/installer
	cd assets/installer
	git lfs pull
	echo -e "Downloaded assets: \n\n$(git lfs ls-files)"
	rm -rf .git .gitattributes
	cd ../..
fi

if [ "$simulator" == true ]; then
	echo "Simulator Build"
	TARGET=SIMULATORARM64
	BUILD_DIR="build-ios/simulator"
else
	echo "Device Build"
	TARGET=OS64
	BUILD_DIR="build-ios/device"
fi

if [ "$skipbuild" == true ]; then
	echo "Skipping build"
else
	# configure and build
	cmake -S . -B "$BUILD_DIR" -G Xcode \
		-Wno-deprecated \
		-DCMAKE_TOOLCHAIN_FILE="cmake/Modules/ios.toolchain.cmake" \
		-DPLATFORM="$TARGET" \
		-DDEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
		-DSRB2_CONFIG_EXTERNAL_ASSETS="$useassets"

	cmake --build "$BUILD_DIR" --config Release -- -quiet -allowProvisioningUpdates
fi

# Package assets into .app
if [ "$useassets" == true ]; then
	cmake --install "$BUILD_DIR" --config Release --prefix "$BUILD_DIR/bin/Release"
fi

# Insert app icon
PARTIAL_PLIST="$(mktemp)"
xcrun actool --compile "$REPO/$BUILD_DIR/bin/Release/$BUNDLE_NAME.app" \
	--platform iphoneos \
	--minimum-deployment-target "$DEPLOYMENT_TARGET" \
	--app-icon srb2 \
	--output-partial-info-plist "$PARTIAL_PLIST" \
	"$REPO/src/sdl/srb2.icon" \
	> /dev/null

/usr/libexec/PlistBuddy -c "Merge $PARTIAL_PLIST :" "$REPO/$BUILD_DIR/bin/Release/$BUNDLE_NAME.app/Info.plist"
rm -f "$PARTIAL_PLIST"

if [ "$simulator" == false ]; then
	# Package IPA for distribution
	mkdir -p "$BUILD_DIR/bin/Release/Payload"
	cp -R "$BUILD_DIR/bin/Release/$BUNDLE_NAME.app" "$BUILD_DIR/bin/Release/Payload"
	cd "$BUILD_DIR/bin/Release"
	COPYFILE_DISABLE=1 zip -qr "./$BUNDLE_NAME.ipa" Payload -x '.*'
	cd ../../../..
	rm -rf "$BUILD_DIR/bin/Release/Payload"
else
	# Launch in Device Hub
	BOOTED_UDID="$(xcrun simctl list devices booted -j | /usr/bin/python3 -c \
	'import json,sys; d=json.load(sys.stdin)["devices"]; print(next((dev["udid"] for devs in d.values() for dev in devs), ""))')"
	if [ -z "$BOOTED_UDID" ]; then
		xcrun simctl boot "$devicename"
		if [ -d "/Applications/Xcode.app/Contents/Applications/DeviceHub.app" ]; then
			open "/Applications/Xcode.app/Contents/Applications/DeviceHub.app" 2>/dev/null || true
		else
			open -a Simulator 2>/dev/null || true
		fi
		# Give the simulator a moment to finish booting before installing.
		xcrun simctl bootstatus "$devicename" -b
	fi
	xcrun simctl install booted "$BUILD_DIR/bin/Release/$BUNDLE_NAME.app"
	xcrun simctl launch --console booted "$BUNDLE_ID"
fi
