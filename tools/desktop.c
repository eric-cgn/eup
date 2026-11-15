/* desktop OUT.bin APP.bin...: the invisible "Desktop" file ('FNDR', 'ERIK') that System 6's Finder keeps on
   every disk, as MacBinary, for applications at the top of the disk. A locked disk must have one already:
   without it the Finder can't make one and ejects the disk (Mini vMac mounts a Disk Copy image locked).
   Made as Mill's release disk makes it (Mill's tools/disk.py): from each application's bundle, its 'ICN#'
   and 'FREF' copied under new IDs with local IDs from 1, a 'BNDL', the creator's resource (a stamp and the
   BNDL's ID), and 'APPL' 0, which says where each application is. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/format.h"

#define T(a, b, c, d) (((unsigned long)(a) << 24) | ((unsigned long)(b) << 16) | ((unsigned long)(c) << 8) | (unsigned long)(d))

static unsigned char *load(const char *path, unsigned long *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = (unsigned long)ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc(*len);
    if (fread(b, 1, *len, f) != *len) { perror(path); exit(1); }
    fclose(f);
    return b;
}

typedef struct { const unsigned char *p; unsigned long len; } Mem;

static int mem_read(void *ref, unsigned long at, void *buf, long n)
{
    Mem *m = (Mem *)ref;
    if (at + n > m->len) return 1;
    memcpy(buf, m->p + at, n);
    return 0;
}

int main(int argc, char **argv)
{
    EupRWrite *w = eup_rwrite_new();
    unsigned char appl[600], head[128], *fork;
    unsigned long alen = 0, flen;
    int a;
    FILE *out;
    static const unsigned char FINDER[] = "\012Finder 1.0";
    EupMacInfo m;
    if (argc < 3) { fprintf(stderr, "usage: desktop OUT.bin APP.bin...\n"); return 2; }
    eup_rwrite_add(w, T('S', 'T', 'R', ' '), 0, NULL, 0, FINDER, sizeof FINDER - 1);
    for (a = 2; a < argc; a++) {
        unsigned long len;
        unsigned char *mb = load(argv[a], &len), bndl[16], b[8];
        EupMacInfo info;
        EupResRef *list;
        Mem rf;
        int n, i, icn = -1, bndl_id = 23395 + 10 * (a - 2), icn_id = 21677 + a, fref_id = 20798 + a;
        if (!eup_macbinary_read(mb, len, &info)) { fprintf(stderr, "%s: not MacBinary\n", argv[a]); return 1; }
        rf.p = mb + info.rsrc_at;
        rf.len = info.rlen;
        if (eup_rfork_list(mem_read, &rf, info.rlen, &list, &n)) { fprintf(stderr, "%s: no resources\n", argv[a]); return 1; }
        for (i = 0; i < n; i++) if (list[i].type == T('I', 'C', 'N', '#') && list[i].id == 128) icn = i;
        if (icn < 0) { fprintf(stderr, "%s: no 'ICN#' 128\n", argv[a]); return 1; }
        eup_rwrite_add(w, T('I', 'C', 'N', '#'), icn_id, NULL, 0, rf.p + list[icn].at, list[icn].len);
        eup_put32(b, T('A', 'P', 'P', 'L'));        /* 'FREF': APPL, local icon 1, no name */
        eup_put16(b + 4, 1);
        b[6] = 0;
        eup_rwrite_add(w, T('F', 'R', 'E', 'F'), fref_id, NULL, 0, b, 7);
        eup_put32(bndl, info.creator);              /* 'BNDL': one ICN# and one FREF, local ID 1 */
        eup_put16(bndl + 4, 0);
        eup_put16(bndl + 6, 1);
        eup_put32(bndl + 8, T('I', 'C', 'N', '#'));
        eup_put16(bndl + 12, 0);
        {
            unsigned char full[28];
            memcpy(full, bndl, 14);
            eup_put16(full + 14, 1);
            eup_put16(full + 16, (unsigned int)icn_id);
            eup_put32(full + 18, T('F', 'R', 'E', 'F'));
            eup_put16(full + 22, 0);
            eup_put16(full + 24, 1);
            eup_put16(full + 26, (unsigned int)fref_id);
            eup_rwrite_add(w, T('B', 'N', 'D', 'L'), bndl_id, NULL, 0, full, 28);
        }
        eup_put32(b, 0x77231199UL);                 /* the creator's resource: a stamp, and the BNDL's ID */
        eup_put16(b + 4, (unsigned int)bndl_id);
        eup_rwrite_add(w, info.creator, 0, NULL, 0, b, 6);
        eup_put32(appl + alen, info.creator);       /* 'APPL' 0: creator, folder (2: the top), name, even */
        eup_put32(appl + alen + 4, 2);
        memcpy(appl + alen + 8, info.name, info.name[0] + 1);
        alen += 8 + info.name[0] + 1;
        if (alen % 2) appl[alen++] = 0;
        free(list);
        free(mb);
    }
    eup_rwrite_add(w, T('A', 'P', 'P', 'L'), 0, NULL, 0, appl, alen);
    fork = eup_rwrite_finish(w, &flen);
    memset(&m, 0, sizeof m);
    memcpy(m.name, "\007Desktop", 8);
    m.type = T('F', 'N', 'D', 'R');
    m.creator = T('E', 'R', 'I', 'K');
    m.flags = 0x4000;                               /* invisible */
    m.rlen = flen;
    eup_macbinary_header(&m, head);
    if (!(out = fopen(argv[1], "wb"))) { perror(argv[1]); return 1; }
    fwrite(head, 1, 128, out);
    fwrite(fork, 1, flen, out);
    { static const unsigned char Z[128]; fwrite(Z, 1, (128 - flen % 128) % 128, out); }
    return fclose(out) != 0;
}
