/* gzip (RFC 1952) with deflate (RFC 1951), written as the input arrives. Small enough for a Mac Plus:
   an 8K window, a 4K-entry hash table, and 8K symbols to a block, about 70K in all. LZ77 with hash
   chains and one step of lazy matching; each block in fixed or dynamic Huffman codes, whichever is
   shorter. */
#include <stdlib.h>
#include <string.h>

#include "format.h"

#define WBITS 13
#define WSIZE (1L << WBITS)
#define WMASK (WSIZE - 1)
#define HBITS 12
#define HSIZE (1 << HBITS)
#define HMASK (HSIZE - 1)
#define HSHIFT 4                    /* (HBITS + 2) / 3: three bytes make the hash */
#define MIN_MATCH 3
#define MAX_MATCH 258
#define LOOKAHEAD (MAX_MATCH + MIN_MATCH + 1)
#define MAX_DIST (WSIZE - LOOKAHEAD)
#define LBUF 8192                   /* symbols to a block */
#define CHAIN 48                    /* match candidates looked at */
#define NICE 128                    /* a match this long is good enough */
#define LAZY 32                     /* don't look for a longer match after one this long */

struct EupDeflate {
    eup_out out;
    void *ref;
    int err;
    unsigned long crc, isize, total;
    unsigned char window[2 * WSIZE + MAX_MATCH];   /* the end: room for a match's comparison to run over */
    unsigned short head[HSIZE], prev[WSIZE];
    long strstart, lookahead, fill;     /* fill: bytes in window */
    unsigned int ins_h;
    int prev_length, match_length, match_available, hinit;
    long match_start, prev_match;
    unsigned char lbuf[LBUF];           /* a literal, or a match's length less 3 */
    unsigned short dbuf[LBUF];          /* 0 for a literal, else the distance */
    int last;
    unsigned short lfreq[286], dfreq[30];
    unsigned long bits;
    int nbits;
    unsigned char obuf[1024];
    int olen;
};

/* ---- output ---- */

static void out_flush(EupDeflate *d)
{
    if (d->olen && !d->err && d->out(d->ref, d->obuf, d->olen)) d->err = 1;
    d->total += d->olen;
    d->olen = 0;
}

static void out_byte(EupDeflate *d, unsigned char b)
{
    d->obuf[d->olen++] = b;
    if (d->olen == (int)sizeof d->obuf) out_flush(d);
}

static void put_bits(EupDeflate *d, unsigned long v, int n)
{
    d->bits |= v << d->nbits;
    d->nbits += n;
    while (d->nbits >= 8) {
        out_byte(d, (unsigned char)d->bits);
        d->bits >>= 8;
        d->nbits -= 8;
    }
}

static void align(EupDeflate *d)
{
    if (d->nbits) put_bits(d, 0, 8 - d->nbits);
}

/* ---- Huffman codes ---- */

static const unsigned short LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                                          2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const unsigned char CLORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

static int len_code(int len)        /* 3..258 to 0..28 */
{
    int i = 28;
    while (LBASE[i] > len) i--;
    return i;
}

static int dist_code(int dist)
{
    int i = 29;
    while (DBASE[i] > dist) i--;
    return i;
}

/* Code lengths for freq[0..n), none longer than max; every code complete (two codes at least) */
static void lengths(const unsigned short *freq_in, int n, int max, unsigned char *len)
{
    unsigned long freq[320];
    short sym[320], parent[640];
    unsigned long w[640];
    int i, k, used;
    for (i = 0; i < n; i++) freq[i] = freq_in[i];
    for (;;) {
        int leaves, nodes, a, b, li, ni, deepest;
        used = 0;
        for (i = 0; i < n; i++) if (freq[i]) sym[used++] = (short)i;
        for (i = 0; used < 2 && i < n; i++) if (!freq[i]) { freq[i] = 1; sym[used++] = (short)i; }
        /* leaves in order of weight (insertion sort: at most 286) */
        for (i = 1; i < used; i++) {
            short s = sym[i];
            for (k = i; k > 0 && freq[sym[k - 1]] > freq[s]; k--) sym[k] = sym[k - 1];
            sym[k] = s;
        }
        /* two queues: the leaves, and the nodes as they're made (which come in order of weight) */
        leaves = used;
        for (i = 0; i < leaves; i++) w[i] = freq[sym[i]];
        nodes = leaves;
        li = 0;
        ni = leaves;
        while (nodes < 2 * leaves - 1) {
            int pick[2], j;
            for (j = 0; j < 2; j++) {
                if (li < leaves && (ni >= nodes || w[li] <= w[ni])) pick[j] = li++;
                else pick[j] = ni++;
            }
            a = pick[0];
            b = pick[1];
            w[nodes] = w[a] + w[b];
            parent[a] = parent[b] = (short)nodes;
            nodes++;
        }
        memset(len, 0, n);
        deepest = 0;
        for (i = 0; i < leaves; i++) {
            int dpt = 0, x = i;
            while (x != nodes - 1) { x = parent[x]; dpt++; }
            len[sym[i]] = (unsigned char)dpt;
            if (dpt > deepest) deepest = dpt;
        }
        if (deepest <= max) return;
        for (i = 0; i < n; i++) if (freq[i]) freq[i] = (freq[i] >> 1) | 1;      /* flatter, and again */
    }
}

/* The codes for lengths, bit-reversed for writing (deflate sends a code's top bit first) */
static void codes(const unsigned char *len, int n, unsigned short *code)
{
    unsigned short count[16], next[16];
    int i, b;
    unsigned int c = 0;
    memset(count, 0, sizeof count);
    for (i = 0; i < n; i++) count[len[i]]++;
    count[0] = 0;
    for (b = 1; b < 16; b++) { c = (c + count[b - 1]) << 1; next[b] = (unsigned short)c; }
    for (i = 0; i < n; i++) {
        unsigned int v, r = 0;
        int k;
        if (!len[i]) continue;
        v = next[len[i]]++;
        for (k = 0; k < len[i]; k++) { r = (r << 1) | (v & 1); v >>= 1; }
        code[i] = (unsigned short)r;
    }
}

/* The fixed codes' lengths (RFC 1951 3.2.6) */
static void fixed_lengths(unsigned char *ll, unsigned char *dl)
{
    int i;
    for (i = 0; i < 144; i++) ll[i] = 8;
    for (; i < 256; i++) ll[i] = 9;
    for (; i < 280; i++) ll[i] = 7;
    for (; i < 288; i++) ll[i] = 8;
    for (i = 0; i < 30; i++) dl[i] = 5;
}

/* The bits the block's symbols take in these codes */
static unsigned long cost(EupDeflate *d, const unsigned char *ll, const unsigned char *dl)
{
    unsigned long bits = 0;
    int i;
    for (i = 0; i < 286; i++) bits += (unsigned long)d->lfreq[i] * (ll[i] + (i > 256 ? LEXT[i - 257] : 0));
    for (i = 0; i < 30; i++) bits += (unsigned long)d->dfreq[i] * (dl[i] + DEXT[i]);
    return bits;
}

/* The code length sequence for a dynamic header, run-length coded: symbols in rl, extra bits in rx */
static int rle_lengths(const unsigned char *lens, int n, unsigned char *rl, unsigned char *rx)
{
    int i = 0, k = 0;
    while (i < n) {
        int v = lens[i], run = 1;
        while (i + run < n && lens[i + run] == v) run++;
        if (v == 0 && run >= 3) {
            int r = run > 138 ? 138 : run;
            if (r <= 10) { rl[k] = 17; rx[k++] = (unsigned char)(r - 3); }
            else { rl[k] = 18; rx[k++] = (unsigned char)(r - 11); }
            i += r;
        } else if (v != 0 && run >= 4) {
            int r = run - 1 > 6 ? 6 : run - 1;
            rl[k] = (unsigned char)v; rx[k++] = 0;
            rl[k] = 16; rx[k++] = (unsigned char)(r - 3);
            i += 1 + r;
        } else {
            rl[k] = (unsigned char)v; rx[k++] = 0;
            i++;
        }
    }
    return k;
}

static void send_block(EupDeflate *d, int final)
{
    unsigned char ll[288], dl[30], fll[288], fdl[30], all[320], rl[320], rx[320], cl[19];
    unsigned short lcode[288], dcode[30], ccode[19], cfreq[19];
    unsigned long dyn, fix;
    int i, hlit, hdist, hclen, nrl;
    d->lfreq[256] = 1;
    lengths(d->lfreq, 286, 15, ll);
    lengths(d->dfreq, 30, 15, dl);
    for (hlit = 286; hlit > 257 && !ll[hlit - 1]; hlit--) ;
    for (hdist = 30; hdist > 1 && !dl[hdist - 1]; hdist--) ;
    memcpy(all, ll, hlit);
    memcpy(all + hlit, dl, hdist);
    nrl = rle_lengths(all, hlit + hdist, rl, rx);
    memset(cfreq, 0, sizeof cfreq);
    for (i = 0; i < nrl; i++) cfreq[rl[i]]++;
    lengths(cfreq, 19, 7, cl);
    for (hclen = 19; hclen > 4 && !cl[CLORDER[hclen - 1]]; hclen--) ;
    dyn = 14 + 3 * hclen + cost(d, ll, dl);
    for (i = 0; i < nrl; i++) dyn += cl[rl[i]] + (rl[i] == 16 ? 2 : rl[i] == 17 ? 3 : rl[i] == 18 ? 7 : 0);
    fixed_lengths(fll, fdl);
    fix = cost(d, fll, fdl);
    if (fix <= dyn) {
        memcpy(ll, fll, sizeof ll);
        memcpy(dl, fdl, sizeof dl);
        put_bits(d, final, 1);
        put_bits(d, 1, 2);
        codes(ll, 288, lcode);
        codes(dl, 30, dcode);
    } else {
        put_bits(d, final, 1);
        put_bits(d, 2, 2);
        put_bits(d, hlit - 257, 5);
        put_bits(d, hdist - 1, 5);
        put_bits(d, hclen - 4, 4);
        for (i = 0; i < hclen; i++) put_bits(d, cl[CLORDER[i]], 3);
        codes(cl, 19, ccode);
        for (i = 0; i < nrl; i++) {
            put_bits(d, ccode[rl[i]], cl[rl[i]]);
            if (rl[i] == 16) put_bits(d, rx[i], 2);
            else if (rl[i] == 17) put_bits(d, rx[i], 3);
            else if (rl[i] == 18) put_bits(d, rx[i], 7);
        }
        codes(ll, 286, lcode);
        codes(dl, 30, dcode);
    }
    for (i = 0; i < d->last; i++) {
        int dist = d->dbuf[i];
        if (!dist) put_bits(d, lcode[d->lbuf[i]], ll[d->lbuf[i]]);
        else {
            int len = d->lbuf[i] + MIN_MATCH, lc = len_code(len), dc = dist_code(dist);
            put_bits(d, lcode[257 + lc], ll[257 + lc]);
            if (LEXT[lc]) put_bits(d, len - LBASE[lc], LEXT[lc]);
            put_bits(d, dcode[dc], dl[dc]);
            if (DEXT[dc]) put_bits(d, dist - DBASE[dc], DEXT[dc]);
        }
    }
    put_bits(d, lcode[256], ll[256]);
    d->last = 0;
    memset(d->lfreq, 0, sizeof d->lfreq);
    memset(d->dfreq, 0, sizeof d->dfreq);
}

static void tally(EupDeflate *d, int dist, int lc)
{
    d->lbuf[d->last] = (unsigned char)lc;
    d->dbuf[d->last++] = (unsigned short)dist;
    if (!dist) d->lfreq[lc]++;
    else { d->lfreq[257 + len_code(lc + MIN_MATCH)]++; d->dfreq[dist_code(dist)]++; }
    if (d->last == LBUF) send_block(d, 0);
}

/* ---- matching ---- */

static void insert(EupDeflate *d, long pos)
{
    d->ins_h = ((d->ins_h << HSHIFT) ^ d->window[pos + MIN_MATCH - 1]) & HMASK;
    d->prev[pos & WMASK] = d->head[d->ins_h];
    d->head[d->ins_h] = (unsigned short)pos;
}

static int longest(EupDeflate *d, long cur, int prev_len)
{
    const unsigned char *w = d->window, *scan = w + d->strstart;
    long limit = d->strstart > MAX_DIST ? d->strstart - MAX_DIST : 0;
    int best = prev_len, chain = CHAIN, maxlen = d->lookahead < MAX_MATCH ? (int)d->lookahead : MAX_MATCH;
    if (prev_len >= LAZY) chain >>= 2;
    do {
        const unsigned char *m = w + cur;
        int len;
        if (m[best] != scan[best] || m[0] != scan[0] || m[1] != scan[1]) continue;
        for (len = 2; len < maxlen && m[len] == scan[len]; len++) ;
        if (len > best) {
            d->match_start = cur;
            best = len;
            if (len >= NICE || len >= maxlen) break;
        }
    } while ((cur = d->prev[cur & WMASK]) > limit && --chain);
    return best > d->lookahead ? (int)d->lookahead : best;
}

/* Move the upper half of the window down, when the input reaches its end */
static void slide(EupDeflate *d)
{
    int i;
    memcpy(d->window, d->window + WSIZE, WSIZE);
    d->strstart -= WSIZE;
    d->match_start -= WSIZE;
    d->prev_match -= WSIZE;
    d->fill -= WSIZE;
    for (i = 0; i < HSIZE; i++) d->head[i] = d->head[i] >= WSIZE ? (unsigned short)(d->head[i] - WSIZE) : 0;
    for (i = 0; i < WSIZE; i++) d->prev[i] = d->prev[i] >= WSIZE ? (unsigned short)(d->prev[i] - WSIZE) : 0;
}

/* As far as the input goes (all of it if flush): the lazy matching of RFC 1951's section 4 */
static void compress(EupDeflate *d, int flush)
{
    if (!d->hinit && d->lookahead >= 2) {
        d->ins_h = ((d->window[0] << HSHIFT) ^ d->window[1]) & HMASK;
        d->hinit = 1;
    }
    while (d->lookahead >= (flush ? 1 : LOOKAHEAD)) {
        long hash_head = 0;
        if (d->lookahead >= MIN_MATCH) {
            insert(d, d->strstart);
            hash_head = d->prev[d->strstart & WMASK];
        }
        d->prev_length = d->match_length;
        d->prev_match = d->match_start;
        d->match_length = MIN_MATCH - 1;
        if (hash_head && d->prev_length < LAZY && d->strstart - hash_head <= MAX_DIST) {
            d->match_length = longest(d, hash_head, d->prev_length);
            if (d->match_length == MIN_MATCH && d->strstart - d->match_start > 4096) d->match_length = MIN_MATCH - 1;
        }
        if (d->prev_length >= MIN_MATCH && d->match_length <= d->prev_length) {
            /* the match before is as good: take it */
            long end = d->strstart + d->lookahead - MIN_MATCH;
            int n = d->prev_length;
            tally(d, (int)(d->strstart - 1 - d->prev_match), n - MIN_MATCH);
            d->lookahead -= n - 1;
            n -= 2;
            while (n--) if (++d->strstart <= end) insert(d, d->strstart);
            d->strstart++;
            d->match_available = 0;
            d->match_length = MIN_MATCH - 1;
        } else {
            if (d->match_available) tally(d, 0, d->window[d->strstart - 1]);
            d->match_available = 1;
            d->strstart++;
            d->lookahead--;
        }
    }
    if (flush && d->match_available) { tally(d, 0, d->window[d->strstart - 1]); d->match_available = 0; }
}

/* ---- the gzip wrapper, and CRC-32 ---- */

unsigned long eup_crc32(unsigned long crc, const unsigned char *p, long n)
{
    static unsigned long table[256];
    static int made;
    if (!made) {
        unsigned long c;
        int i, k;
        for (i = 0; i < 256; i++) {
            c = (unsigned long)i;
            for (k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = 1;
    }
    crc = ~crc & 0xFFFFFFFFUL;
    while (n-- > 0) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc & 0xFFFFFFFFUL;
}

EupDeflate *eup_deflate_begin(eup_out out, void *ref)
{
    static const unsigned char HEAD[10] = { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 7 };      /* 7: written on a Mac */
    EupDeflate *d = (EupDeflate *)calloc(1, sizeof *d);
    int i;
    if (!d) return NULL;
    d->out = out;
    d->ref = ref;
    for (i = 0; i < 10; i++) out_byte(d, HEAD[i]);
    return d;
}

int eup_deflate_data(EupDeflate *d, const unsigned char *p, long n)
{
    d->crc = eup_crc32(d->crc, p, n);
    d->isize += n;
    while (n > 0 && !d->err) {
        long room;
        if (d->fill == 2 * WSIZE) slide(d);     /* compress has left fewer than LOOKAHEAD: strstart is past WSIZE */
        room = 2 * WSIZE - d->fill;
        if (room > n) room = n;
        memcpy(d->window + d->fill, p, room);
        d->fill += room;
        d->lookahead += room;
        p += room;
        n -= room;
        compress(d, 0);
    }
    return d->err;
}

/* The rest, and the trailer; frees d. 0, or nonzero if out failed. *size: the bytes written in all. */
int eup_deflate_end(EupDeflate *d, unsigned long *size)
{
    int err, i;
    compress(d, 1);
    send_block(d, 1);
    align(d);
    for (i = 0; i < 4; i++) out_byte(d, (unsigned char)(d->crc >> (8 * i)));
    for (i = 0; i < 4; i++) out_byte(d, (unsigned char)(d->isize >> (8 * i)));
    out_flush(d);
    err = d->err;
    if (size) *size = d->total;
    free(d);
    return err;
}
