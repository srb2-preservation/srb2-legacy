#!/bin/bash
# srb2-legacy Switch port build script
# Dependencies: devkitPRO, devkitARM, devkitA64, switch-sdl2, switch-sdl2_mixer, switch-zlib, switch-libpng

while true; do
    echo "Clear the 'build/' directory before compiling? [Y/N]"
    read -p "" yn
    case $yn in
        [Yy]* ) rm -rf ./build; break;;
        [Nn]* ) break;;
    esac
done

echo "== Running CMake =="

# LTO was disabled because i may or may not be impateint when it comes to linking :eyes:, should be reenabled for master

cmake -B build \
-DSRB2_CONFIG_USE_INTERNAL_LIBRARIES=ON \
-DSRB2_CONFIG_HAVE_OPENMPT=OFF \
-DSRB2_CONFIG_HAVE_GME=OFF \
-DSRB2_CONFIG_LTO=OFF \
-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO"/cmake/Switch.cmake"

echo "== Running make (with -j "$(nproc)") =="

make -C build -j $(nproc)

if [ ! -f ./build/src/srb2legacy.nro ]; then
    echo "Make Failed"
    exit
fi

mv ./build/src/srb2legacy.nro ./build/bin/srb2-legacy-2130.nro
echo "== Build finished, check /build/bin/ =="
