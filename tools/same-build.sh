#!/bin/sh
# same-build.sh OLD NEW: whether each MacBinary file in OLD (prebuilt/ as committed) is the same as the one
# of that name in NEW (just built by build-mac.sh), apart from the header's two dates (bytes 92 to 99,
# counting from 1) and its CRC (bytes 125 and 126), which change with every build. The CI uses it.
fail=0
for f in "$1"/*.bin; do
	n=$(basename "$f")
	g="$2/$n"
	if [ ! -f "$g" ] || [ "$(wc -c < "$f")" -ne "$(wc -c < "$g")" ] ||
	   cmp -l "$f" "$g" | awk '($1 < 92 || $1 > 99) && $1 != 125 && $1 != 126 { d = 1 } END { exit !d }'; then
		echo "::error::prebuilt/$n is not what the source builds. Run ./build-mac.sh and commit prebuilt/."
		fail=1
	else
		echo "prebuilt/$n: the same"
	fi
done
exit $fail
