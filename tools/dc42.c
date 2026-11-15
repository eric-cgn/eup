/* dc42 RAW NAME OUT: a Disk Copy 4.2 image of an 800K raw disk: an 84-byte header (the disk's name, its
   size, and the data and tag checksums), then the disk; no tags. As Mill's release disk is made. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    static unsigned char d[819200], h[84];
    unsigned long sum = 0, i;
    size_t n;
    FILE *f;
    if (argc != 4 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: dc42 RAW NAME OUT\n"); return 2; }
    if (fread(d, 1, sizeof d, f) != sizeof d) { fprintf(stderr, "dc42: %s is not 819200 bytes\n", argv[1]); return 1; }
    fclose(f);
    for (i = 0; i < sizeof d; i += 2) {
        sum = (sum + ((unsigned long)d[i] << 8 | d[i + 1])) & 0xFFFFFFFFUL;
        sum = ((sum >> 1) | ((sum & 1) << 31)) & 0xFFFFFFFFUL;
    }
    n = strlen(argv[2]) > 63 ? 63 : strlen(argv[2]);
    h[0] = (unsigned char)n;
    memcpy(h + 1, argv[2], n);
    h[64] = 0; h[65] = 0x0C; h[66] = 0x80; h[67] = 0;           /* 819200 */
    h[72] = (unsigned char)(sum >> 24); h[73] = (unsigned char)(sum >> 16); h[74] = (unsigned char)(sum >> 8); h[75] = (unsigned char)sum;
    h[80] = 1;                                                  /* 800K */
    h[81] = 0x22;                                               /* double-sided Macintosh format */
    h[82] = 1; h[83] = 0;
    if (!(f = fopen(argv[3], "wb")) || fwrite(h, 1, 84, f) != 84 || fwrite(d, 1, sizeof d, f) != sizeof d || fclose(f)) { perror(argv[3]); return 1; }
    return 0;
}
