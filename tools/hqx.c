/* hqx FILE.bin OUT.hqx: a MacBinary file as BinHex 4.0, for the release (the programs, for builds that
   fetch them, and for Macs that take .hqx) */
#include <stdio.h>
#include <stdlib.h>
#include "../core/format.h"

static int out(void *ref, const unsigned char *p, long n)
{
    return fwrite(p, 1, n, (FILE *)ref) != (size_t)n;
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *b;
    long size;
    EupMacInfo mi;
    if (argc != 3 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: hqx FILE.bin OUT.hqx\n"); return 2; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 128 || !(b = malloc(size)) || fread(b, 1, size, f) != (size_t)size) { fprintf(stderr, "hqx: %s: can't be read\n", argv[1]); return 1; }
    fclose(f);
    if (!eup_macbinary_read(b, size, &mi)) { fprintf(stderr, "hqx: %s: not MacBinary\n", argv[1]); return 1; }
    if (!(f = fopen(argv[2], "wb")) || eup_binhex(mi.name, mi.type, mi.creator, mi.flags, b + mi.data_at, mi.dlen,
                                                  b + mi.rsrc_at, mi.rlen, out, f) || fclose(f)) {
        fprintf(stderr, "hqx: %s: can't be written\n", argv[2]);
        return 1;
    }
    return 0;
}
