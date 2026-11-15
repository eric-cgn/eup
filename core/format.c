/* The file formats: see format.h */
#include <stdlib.h>
#include <string.h>

#include "format.h"

unsigned long eup_get32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }
unsigned int eup_get16(const unsigned char *p) { return (unsigned int)((p[0] << 8) | p[1]); }
void eup_put32(unsigned char *p, unsigned long v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
void eup_put16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }

unsigned short eup_crc16(unsigned short crc, const unsigned char *p, long n)
{
    unsigned int c = crc;
    int k;
    while (n-- > 0) {
        c ^= (unsigned int)*p++ << 8;
        for (k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF;
    }
    return (unsigned short)c;
}

/* ---- MacBinary ---- */

static unsigned long pad128(unsigned long n) { return (n + 127) / 128 * 128; }

int eup_macbinary_read(const unsigned char *b, unsigned long size, EupMacInfo *m)
{
    unsigned long sec = 0, base, least, most;
    if (size < 128 || b[0] || b[74] || b[82] || b[1] < 1 || b[1] > 63) return 0;
    if (b[122] >= 129) {
        if (eup_crc16(0, b, 124) != eup_get16(b + 124)) return 0;
        sec = eup_get16(b + 120);
    }
    memset(m, 0, sizeof *m);
    m->dlen = eup_get32(b + 83);
    m->rlen = eup_get32(b + 87);
    if (m->dlen > 0x7FFFFFFFUL || m->rlen > 0x7FFFFFFFUL) return 0;
    base = 128 + pad128(sec) + pad128(m->dlen);
    least = m->rlen ? base + m->rlen : 128 + pad128(sec) + m->dlen;
    most = base + pad128(m->rlen);
    if (size < least || size > most) return 0;
    memcpy(m->name, b + 1, b[1] + 1);
    m->type = eup_get32(b + 65);
    m->creator = eup_get32(b + 69);
    m->flags = (unsigned int)((b[73] << 8) | b[101]);
    m->created = eup_get32(b + 91);
    m->modified = eup_get32(b + 95);
    m->data_at = 128 + pad128(sec);
    m->rsrc_at = base;
    return 1;
}

void eup_macbinary_header(const EupMacInfo *m, unsigned char *h)
{
    memset(h, 0, 128);
    memcpy(h + 1, m->name, m->name[0] + 1);
    eup_put32(h + 65, m->type);
    eup_put32(h + 69, m->creator);
    h[73] = (unsigned char)(m->flags >> 8);
    eup_put32(h + 83, m->dlen);
    eup_put32(h + 87, m->rlen);
    eup_put32(h + 91, m->created);
    eup_put32(h + 95, m->modified);
    h[101] = (unsigned char)m->flags;
    h[122] = h[123] = 129;
    eup_put16(h + 124, eup_crc16(0, h, 124));
}

/* ---- resource forks (Inside Macintosh: More Macintosh Toolbox, 1-121) ---- */

int eup_rfork_list(eup_read_at read, void *ref, unsigned long len, EupResRef **list, int *count)
{
    unsigned char head[16], *map;
    unsigned long data_at, map_at, data_len, map_len;
    unsigned int type_at, name_at, ntypes, t, k;
    int n = 0, total = 0;
    EupResRef *out;
    *list = NULL;
    *count = 0;
    if (len == 0) return 0;
    if (len < 16 || read(ref, 0, head, 16)) return -1;
    data_at = eup_get32(head); map_at = eup_get32(head + 4);
    data_len = eup_get32(head + 8); map_len = eup_get32(head + 12);
    if (data_at + data_len > len || map_at + map_len > len || map_len < 30 || map_len > 0x10000L) return -1;
    if (!(map = (unsigned char *)malloc(map_len))) return -1;
    if (read(ref, map_at, map, (long)map_len)) { free(map); return -1; }
    type_at = eup_get16(map + 24);
    name_at = eup_get16(map + 26);
    if (type_at + 2 > map_len) { free(map); return -1; }
    ntypes = (eup_get16(map + type_at) + 1) & 0xFFFF;
    for (t = 0; t < ntypes; t++) {
        unsigned int at = type_at + 2 + 8 * t;
        if (at + 8 > map_len) { free(map); return -1; }
        total += eup_get16(map + at + 4) + 1;
    }
    if (!(out = (EupResRef *)calloc(total ? total : 1, sizeof *out))) { free(map); return -1; }
    for (t = 0; t < ntypes; t++) {
        unsigned int at = type_at + 2 + 8 * t, nres = eup_get16(map + at + 4) + 1, refs = type_at + eup_get16(map + at + 6);
        for (k = 0; k < nres; k++) {
            unsigned int r = refs + 12 * k, noff;
            unsigned char lenb[4];
            EupResRef *x = &out[n];
            if (r + 12 > map_len) goto bad;
            x->type = eup_get32(map + at);
            x->id = (int)(short)eup_get16(map + r);
            noff = eup_get16(map + r + 2);
            x->attrs = map[r + 4];
            x->at = data_at + (eup_get32(map + r + 4) & 0xFFFFFFUL) + 4;
            if (x->at > len || read(ref, x->at - 4, lenb, 4)) goto bad;
            x->len = eup_get32(lenb);
            if (x->at + x->len > len) goto bad;
            if (noff != 0xFFFF) {
                unsigned int na = name_at + noff;
                if (na >= map_len || na + 1 + map[na] > map_len) goto bad;
                memcpy(x->name, map + na, map[na] + 1);
                x->has_name = 1;
            }
            n++;
        }
    }
    free(map);
    *list = out;
    *count = n;
    return 0;
bad:
    free(map);
    free(out);
    return -1;
}

typedef struct {
    unsigned long type;
    int id, has_name;
    unsigned char name[256], attrs;
    unsigned char *data;
    unsigned long len;
} WRes;

struct EupRWrite {
    WRes *res;
    int n, cap;
    int failed;
};

EupRWrite *eup_rwrite_new(void)
{
    return (EupRWrite *)calloc(1, sizeof(EupRWrite));
}

void eup_rwrite_remove(EupRWrite *w, unsigned long type, int id)
{
    int i;
    for (i = 0; i < w->n; i++)
        if (w->res[i].type == type && w->res[i].id == id) {
            free(w->res[i].data);
            memmove(&w->res[i], &w->res[i + 1], sizeof(WRes) * (w->n - i - 1));
            w->n--;
            return;
        }
}

int eup_rwrite_has(EupRWrite *w, unsigned long type, int id)
{
    int i;
    for (i = 0; i < w->n; i++) if (w->res[i].type == type && w->res[i].id == id) return 1;
    return 0;
}

int eup_rwrite_add(EupRWrite *w, unsigned long type, int id, const unsigned char *name, unsigned char attrs,
                   const unsigned char *data, unsigned long len)
{
    WRes *r;
    eup_rwrite_remove(w, type, id);
    if (w->n == w->cap) {
        int cap = w->cap ? w->cap * 2 : 32;
        WRes *a = (WRes *)realloc(w->res, sizeof(WRes) * cap);
        if (!a) { w->failed = 1; return -1; }
        w->res = a;
        w->cap = cap;
    }
    r = &w->res[w->n];
    memset(r, 0, sizeof *r);
    if (!(r->data = (unsigned char *)malloc(len ? len : 1))) { w->failed = 1; return -1; }
    memcpy(r->data, data, len);
    r->len = len;
    r->type = type;
    r->id = id;
    r->attrs = attrs;
    if (name) { memcpy(r->name, name, name[0] + 1); r->has_name = 1; }
    w->n++;
    return 0;
}

unsigned char *eup_rwrite_finish(EupRWrite *w, unsigned long *out_len)
{
    unsigned long data_len = 0, names_len = 0, map_len, data_at = 256, map_at, at;
    unsigned int ntypes = 0, i, j, type_list, refs, names;
    unsigned char *f = NULL, *m;
    unsigned long *types;
    if (w->failed) goto done;
    if (!(types = (unsigned long *)malloc(sizeof(unsigned long) * (w->n + 1)))) goto done;
    for (i = 0; i < (unsigned)w->n; i++) {
        for (j = 0; j < ntypes && types[j] != w->res[i].type; j++) ;
        if (j == ntypes) types[ntypes++] = w->res[i].type;
        data_len += 4 + w->res[i].len;
        if (w->res[i].has_name) names_len += 1 + w->res[i].name[0];
    }
    type_list = 28;
    refs = type_list + 2 + 8 * ntypes;
    names = refs + 12 * w->n;
    map_len = names + names_len;
    if (data_len > 0xFFFFFFUL || map_len > 0xFFFFUL) { free(types); goto done; }
    map_at = data_at + data_len;
    *out_len = map_at + map_len;
    if (!(f = (unsigned char *)calloc(1, *out_len))) { free(types); goto done; }
    eup_put32(f, data_at); eup_put32(f + 4, map_at); eup_put32(f + 8, data_len); eup_put32(f + 12, map_len);
    m = f + map_at;
    memcpy(m, f, 16);
    eup_put16(m + 24, type_list);
    eup_put16(m + 26, names);
    eup_put16(m + type_list, (ntypes - 1) & 0xFFFF);
    at = data_at;
    {
        unsigned int ref = refs, name_off = 0;
        for (j = 0; j < ntypes; j++) {
            unsigned int count = 0;
            unsigned char *te = m + type_list + 2 + 8 * j;
            eup_put32(te, types[j]);
            eup_put16(te + 6, ref - type_list);
            for (i = 0; i < (unsigned)w->n; i++) {
                WRes *r = &w->res[i];
                if (r->type != types[j]) continue;
                count++;
                eup_put16(m + ref, (unsigned int)(r->id & 0xFFFF));
                if (r->has_name) {
                    eup_put16(m + ref + 2, name_off);
                    memcpy(m + names + name_off, r->name, r->name[0] + 1);
                    name_off += 1 + r->name[0];
                } else eup_put16(m + ref + 2, 0xFFFF);
                eup_put32(m + ref + 4, ((unsigned long)r->attrs << 24) | (at - data_at));
                eup_put32(f + at, r->len);
                memcpy(f + at + 4, r->data, r->len);
                at += 4 + r->len;
                ref += 12;
            }
            eup_put16(te + 4, count - 1);
        }
    }
    free(types);
done:
    for (i = 0; i < (unsigned)w->n; i++) free(w->res[i].data);
    free(w->res);
    free(w);
    return f;
}

/* ---- BinHex 4.0 ---- */

static const char ALPHABET[] = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";

typedef struct {
    eup_out out;
    void *ref;
    int err;
    int last, run;                  /* the run-length step: the byte repeated, and how often */
    unsigned long bits;
    int nbits, col;
    unsigned char line[80];
    int len;
} Hqx;

static void hqx_char(Hqx *h, unsigned char c)
{
    h->line[h->len++] = c;
    if (++h->col == 64) { h->line[h->len++] = '\r'; h->col = 0; }
    if (h->len >= 64 && !h->err) { if (h->out(h->ref, h->line, h->len)) h->err = 1; h->len = 0; }
}

static void hqx_byte(Hqx *h, unsigned char b)       /* after the run-length step: six bits to a character */
{
    h->bits = (h->bits << 8) | b;
    h->nbits += 8;
    while (h->nbits >= 6) {
        h->nbits -= 6;
        hqx_char(h, (unsigned char)ALPHABET[(h->bits >> h->nbits) & 63]);
    }
}

static void hqx_flush_run(Hqx *h)
{
    if (h->last < 0) return;
    if (h->run >= 3) {
        hqx_byte(h, (unsigned char)h->last);
        if (h->last == 0x90) hqx_byte(h, 0);
        hqx_byte(h, 0x90);
        hqx_byte(h, (unsigned char)h->run);
    } else {
        int i;
        for (i = 0; i < h->run; i++) {
            hqx_byte(h, (unsigned char)h->last);
            if (h->last == 0x90) hqx_byte(h, 0);
        }
    }
    h->run = 0;
    h->last = -1;
}

static void hqx_put(Hqx *h, const unsigned char *p, unsigned long n)
{
    while (n--) {
        int b = *p++;
        if (b == h->last && h->run < 255) h->run++;
        else { hqx_flush_run(h); h->last = b; h->run = 1; }
    }
}

static void hqx_part(Hqx *h, const unsigned char *p, unsigned long n)
{
    unsigned char c[2];
    unsigned short crc = eup_crc16(0, p, (long)n);     /* the same as BinHex's CRC over the bytes and two zeros */
    hqx_put(h, p, n);
    eup_put16(c, crc);
    hqx_put(h, c, 2);
}

int eup_binhex(const unsigned char *name, unsigned long type, unsigned long creator, unsigned int flags,
               const unsigned char *data, unsigned long dlen, const unsigned char *rsrc, unsigned long rlen,
               eup_out out, void *ref)
{
    static const char HEAD[] = "(This file must be converted with BinHex 4.0)\r\r:";
    unsigned char head[96];
    int n = name[0];
    Hqx h;
    memset(&h, 0, sizeof h);
    h.out = out;
    h.ref = ref;
    h.last = -1;
    if (out(ref, (const unsigned char *)HEAD, (long)strlen(HEAD))) return 1;
    h.col = 1;                      /* the colon */
    memcpy(head, name, n + 1);
    head[n + 1] = 0;
    eup_put32(head + n + 2, type);
    eup_put32(head + n + 6, creator);
    eup_put16(head + n + 10, flags);
    eup_put32(head + n + 12, dlen);
    eup_put32(head + n + 16, rlen);
    hqx_part(&h, head, n + 20);
    hqx_part(&h, data, dlen);
    hqx_part(&h, rsrc, rlen);
    hqx_flush_run(&h);
    if (h.nbits) hqx_char(&h, (unsigned char)ALPHABET[(h.bits << (6 - h.nbits)) & 63]);     /* the last bits, with zeros */
    hqx_char(&h, ':');
    if (h.col) h.line[h.len++] = '\r';
    if (!h.err && out(ref, h.line, h.len)) h.err = 1;
    return h.err;
}

/* ---- records ---- */

const unsigned char *eup_record_read(const unsigned char *p, EupRecord *r)
{
    int k;
    r->kind = p[0];
    r->flags = p[1];
    r->type = eup_get32(p + 2);
    r->creator = eup_get32(p + 6);
    r->finder = eup_get16(p + 10);
    r->dlen = eup_get32(p + 12);
    r->rlen = eup_get32(p + 16);
    r->packed = eup_get32(p + 20);
    r->at = eup_get32(p + 24);
    p += EUP_REC_FIXED;
    memcpy(r->name, p, (p[0] > 31 ? 31 : p[0]) + 1);
    r->name[0] = p[0] > 31 ? 31 : p[0];
    p += 1 + p[0];
    r->n = *p++;
    r->names = p;
    for (k = 0; k < r->n; k++) p += 1 + p[0];
    return p;
}

long eup_record_write(const EupRecord *r, unsigned char *out)
{
    unsigned char *p = out;
    const unsigned char *s = r->names;
    int k;
    p[0] = (unsigned char)r->kind;
    p[1] = (unsigned char)r->flags;
    eup_put32(p + 2, r->type);
    eup_put32(p + 6, r->creator);
    eup_put16(p + 10, r->finder);
    eup_put32(p + 12, r->dlen);
    eup_put32(p + 16, r->rlen);
    eup_put32(p + 20, r->packed);
    eup_put32(p + 24, r->at);
    p += EUP_REC_FIXED;
    memcpy(p, r->name, r->name[0] + 1);
    p += 1 + r->name[0];
    *p++ = (unsigned char)r->n;
    for (k = 0; k < r->n; k++) {
        memcpy(p, s, s[0] + 1);
        p += 1 + s[0];
        s += 1 + s[0];
    }
    return (long)(p - out);
}

int eup_record_under(const EupRecord *r, const EupPath *path, const unsigned char **rest, int *nrest)
{
    const unsigned char *s = r->names;
    int k;
    if (r->n < path->n) return 0;
    for (k = 0; k < path->n; k++) {
        if (!eup_same_name(s, path->names[k])) return 0;
        s += 1 + s[0];
    }
    if (rest) *rest = s;
    if (nrest) *nrest = r->n - path->n;
    return 1;
}
