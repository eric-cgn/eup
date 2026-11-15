#!/bin/sh
# Builds the two Mac programs with Retro68 (pinned by digest) in a container, into prebuilt/:
#   Installer Shell.bin   the installer program that every installer is a copy of
#   EUP.bin               the packager application
# Both MacBinary. Only for changing the programs: making installers never needs this.
set -e
cd "$(dirname "$0")"
IMAGE=ghcr.io/autc04/retro68@sha256:dd923969100f408943f73c9618b3c7628686aae819583eecb0ddd1837da6943c
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src "$IMAGE" sh -c '
	set -e
	T=/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
	rm -rf build/installer build/packager      # clean: CMake does not see a change to an included .r file
	mkdir -p build/installer build/packager
	(cd build/installer && cmake ../../installer -DCMAKE_TOOLCHAIN_FILE=$T >/dev/null && make)
	(cd build/packager && cmake ../../packager -DCMAKE_TOOLCHAIN_FILE=$T >/dev/null && make)'
mkdir -p prebuilt
cp build/installer/Installer.bin "prebuilt/Installer Shell.bin"
cp build/packager/EUP.bin prebuilt/EUP.bin
# the Finder's "has bundle" flag, which Retro68 leaves off, and the shell's name on a Mac
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src "$IMAGE" sh -c '
	gcc -o build/finder tools/finder.c core/format.c core/paths.c core/ini.c &&
	build/finder "prebuilt/Installer Shell.bin" "Installer Shell" && build/finder prebuilt/EUP.bin'
ls -l prebuilt
