#!/bin/sh
# Host tests for the eup command: both examples (Mill's only if its files have been copied in), and
# the errors it reports. Run by make test.
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
ok() { echo "ok: $1"; }
bad() { echo "FAILED: $1"; fail=1; }

V=$(sed -n 's/^#define EUP_VERSION "\(.*\)"/\1/p' core/version.h)
[ "$(./eup version)" = "eup $V" ] && ok version || bad version
./eup check examples/hello >/dev/null && ok "check hello" || bad "check hello"
./eup check examples/hello/hello.ini >/dev/null && ok "check hello.ini" || bad "check hello.ini"
./eup build examples/hello -o "$T" >/dev/null && ok "build hello" || bad "build hello"
[ -s "$T/Install-Hello-1.0.bin" ] && [ -s "$T/Install-Hello-1.0.hqx" ] && ok "hello outputs" || bad "hello outputs"
# the MacBinary header: name, APPL, EUPi, the bundle flag
head -c 128 "$T/Install-Hello-1.0.bin" | od -An -c | tr -d ' \n' | grep -q 'InstallHello1\.0' && ok "hello name" || bad "hello name"
[ "$(dd if="$T/Install-Hello-1.0.bin" bs=1 skip=65 count=8 2>/dev/null)" = APPLEUPi ] && ok "hello type and creator" || bad "hello type and creator"
[ "$(dd if="$T/Install-Hello-1.0.bin" bs=1 skip=73 count=1 2>/dev/null | od -An -tx1 | tr -d ' ')" = 20 ] && ok "hello bundle flag" || bad "hello bundle flag"
head -c 46 "$T/Install-Hello-1.0.hqx" | grep -q '^(This file must be converted with BinHex 4.0)' && ok "hello hqx" || bad "hello hqx"

# errors: by line, and nothing written
mkdir -p "$T/broken/Files"
cp -R examples/hello/. "$T/broken/"
sed -e 's/^types = easy, custom$/types = easy, custm/' -e 's/^text = yes$/text = sure/' examples/hello/hello.ini > "$T/broken/hello.ini"
if ./eup build "$T/broken" -o "$T/out-broken" 2>"$T/err"; then bad "broken INI accepted"; else ok "broken INI refused"; fi
grep -q 'hello.ini:[0-9]*: types: there is no \[type custm\]' "$T/err" && ok "line-numbered error" || { bad "line-numbered error"; cat "$T/err"; }
grep -q 'text: yes or no' "$T/err" && ok "every error reported" || bad "every error reported"
[ ! -e "$T/out-broken" ] && ok "nothing written" || bad "nothing written"
rm "$T/broken/Files/Hello Read Me.txt"
cp examples/hello/hello.ini "$T/broken/hello.ini"
./eup check "$T/broken" 2>"$T/err"; grep -q 'no file or folder "Files:Hello Read Me.txt"' "$T/err" && ok "missing file" || { bad "missing file"; cat "$T/err"; }
cp "$T/broken/hello.ini" "$T/broken/second.ini"
./eup check "$T/broken" 2>"$T/err"; grep -q 'more than one .ini' "$T/err" && ok "two INI files" || bad "two INI files"
printf '[installer]\nproduct = Caf\351\n' > "$T/latin1.ini"
./eup check "$T/latin1.ini" 2>"$T/err"; grep -q 'latin1.ini:2: a character that Mac Roman' "$T/err" && ok "not UTF-8" || { bad "not UTF-8"; cat "$T/err"; }

if [ -f examples/mill/Mill.bin ]; then
	./eup build examples/mill -o "$T" >/dev/null && ok "build mill" || bad "build mill"
	[ "$(dd if="$T/Install-Mill-2.0.1.bin" bs=1 skip=65 count=8 2>/dev/null)" = APPLMiLI ] && ok "mill creator" || bad "mill creator"
else
	echo "skipped: mill (copy Mill's files in: examples/mill/README)"
fi
exit $fail
