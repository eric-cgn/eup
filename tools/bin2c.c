/* bin2c FILE NAME: FILE as a C array, const unsigned char NAME[], and its length, NAME_len */
#include <stdio.h>

int main(int argc, char **argv)
{
    FILE *f;
    int c;
    unsigned long n = 0;
    if (argc != 3 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: bin2c FILE NAME\n"); return 2; }
    printf("/* Made from %s by tools/bin2c.c */\nconst unsigned char %s[] = {", argv[1], argv[2]);
    while ((c = getc(f)) != EOF) printf("%s0x%02x,", n++ % 16 ? "" : "\n", c);
    printf("\n};\nconst unsigned long %s_len = %luUL;\n", argv[2], n);
    return 0;
}
