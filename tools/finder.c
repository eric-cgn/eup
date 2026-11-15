/* finder FILE.bin [NAME]: sets the Finder's "has bundle" flag in a MacBinary file (Retro68 leaves it off),
   and its name if one is given, and redoes the header's CRC */
#include <stdio.h>
#include <string.h>
#include "../core/format.h"

int main(int argc, char **argv)
{
    unsigned char h[128];
    FILE *f;
    if (argc < 2 || !(f = fopen(argv[1], "r+b")) || fread(h, 1, 128, f) != 128) { fprintf(stderr, "usage: finder FILE.bin [NAME]\n"); return 2; }
    h[73] |= 0x20;
    if (argc > 2) {
        size_t n = strlen(argv[2]);
        if (n < 1 || n > 31) { fprintf(stderr, "finder: a name is 1 to 31 characters\n"); return 1; }
        memset(h + 1, 0, 64);
        h[1] = (unsigned char)n;
        memcpy(h + 2, argv[2], n);
    }
    eup_put16(h + 124, eup_crc16(0, h, 124));
    fseek(f, 0, SEEK_SET);
    return fwrite(h, 1, 128, f) != 128 || fclose(f) != 0;
}
