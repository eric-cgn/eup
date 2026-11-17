#!/bin/sh
# The release, in dist/ (V is EUP_VERSION, from core/version.h):
#   EUP-V.image              the Mac disk, EUP Program Disk (Disk Copy 4.2, 800K): EUP, the Installer Shell, a Read Me,
#                            the licenses, and the Hello example, from prebuilt/ (./build-mac.sh makes those)
#   EUP-V.bin, .hqx          the packager application, alone
#   Installer-Shell-V.bin, .hqx   the installer program, alone, for other projects' builds
#   eup-V.tar.gz, eup-V.zip  the source, for the command-line packager (git archive of HEAD)
# The disk is made with hfsutils in the Retro68 image (the one build-mac.sh uses).
# It refuses uncommitted changes, as the source archives are of HEAD; the workflows, which stamp
# a dev version and rebuild prebuilt/, set RELEASE_ANY_TREE=1.
set -e
cd "$(dirname "$0")"
V=$(sed -n 's/^#define EUP_VERSION "\(.*\)"/\1/p' core/version.h)
[ -n "$V" ] || { echo "release.sh: no EUP_VERSION in core/version.h"; exit 1; }
IMAGE=ghcr.io/autc04/retro68@sha256:dd923969100f408943f73c9618b3c7628686aae819583eecb0ddd1837da6943c
[ -n "$RELEASE_ANY_TREE" ] || [ -z "$(git status --porcelain)" ] || { echo "release.sh: commit first (the source archives are of HEAD)"; exit 1; }
mkdir -p dist build
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src -e V="$V" "$IMAGE" sh -c '
	set -e
	S=/tmp/stage; mkdir -p $S/Example
	gcc -O2 -o $S/desktop tools/desktop.c core/format.c core/paths.c core/ini.c
	gcc -O2 -o $S/dc42 tools/dc42.c
	gcc -O2 -o $S/hqx tools/hqx.c core/format.c core/paths.c core/ini.c
	mac() { iconv -f UTF-8 -t MACINTOSH "$1" | tr "\n" "\r" > "$2"; }
	sed -e "1s/^EUP .*/EUP $V/" -e "s/^EUP [^ ]*\. ©/EUP $V. ©/" "disk/Read Me" > $S/readme
	mac $S/readme "$S/Read Me"
	{ cat LICENSE; for f in licences/newlib.txt licences/multiversal.txt licences/retro68-gcc-runtime.txt licences/inflate.txt; do
	  printf "\n\n"; cat "$f"; done; } > $S/licenses.txt
	mac $S/licenses.txt $S/Licenses
	$S/desktop $S/Desktop.bin prebuilt/EUP.bin "prebuilt/Installer Shell.bin"
	D=build/EUP-$V.dsk
	dd if=/dev/zero of=$D bs=1024 count=800 2>/dev/null
	hformat -l "EUP Program Disk" $D >/dev/null
	hmount $D >/dev/null
	hcopy -m prebuilt/EUP.bin :
	hcopy -m "prebuilt/Installer Shell.bin" :
	hcopy -m $S/Desktop.bin :
	for f in "Read Me" Licenses; do hcopy -r "$S/$f" ":$f"; hattrib -t TEXT -c ttxt ":$f"; done
	hmkdir :Example
	cd examples/hello
	find . -type d ! -name . | sort | while read -r d; do hmkdir ":Example:$(echo "${d#./}" | tr / :)"; done
	find . -type f | sort | while read -r f; do
		m=":Example:$(echo "${f#./}" | tr / :)"
		mac "$f" $S/f; hcopy -r $S/f "$m"; hattrib -t TEXT -c ttxt "$m"
	done
	cd /src
	hls -laR : | sed -n "1,40p"
	hvol | grep -i free || true
	humount
	$S/dc42 $D "EUP Program Disk" dist/EUP-$V.image
	cp prebuilt/EUP.bin dist/EUP-$V.bin
	cp "prebuilt/Installer Shell.bin" dist/Installer-Shell-$V.bin
	$S/hqx prebuilt/EUP.bin dist/EUP-$V.hqx
	$S/hqx "prebuilt/Installer Shell.bin" dist/Installer-Shell-$V.hqx'
git archive --format=tar.gz --prefix=eup-$V/ -o dist/eup-$V.tar.gz HEAD
git archive --format=zip --prefix=eup-$V/ -o dist/eup-$V.zip HEAD
ls -l dist/EUP-$V.image dist/EUP-$V.bin dist/EUP-$V.hqx dist/Installer-Shell-$V.bin dist/Installer-Shell-$V.hqx \
	dist/eup-$V.tar.gz dist/eup-$V.zip
