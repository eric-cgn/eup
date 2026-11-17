/* Host tests for EUP's core: the INI and its errors, paths, Mac Roman, MacBinary, resource forks,
   BinHex, deflate (through Mill's inflate), records, and packaging from files in memory.
   make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/eup.h"
#include "../core/format.h"
#include "../core/inflate.h"
#include "../core/package.h"

static int failures, checks;

#define CHECK(c) check((c) != 0, #c, __FILE__, __LINE__)
static void check(int ok, const char *what, const char *file, int line)
{
    checks++;
    if (!ok) { failures++; fprintf(stderr, "%s:%d: failed: %s\n", file, line, what); }
}

static int parse(EupConfig *c, const char *text) { return eup_parse(c, text, (long)strlen(text)); }

/* Whether an error on line (0: the file) contains text */
static int has_error(const EupConfig *c, int line, const char *text)
{
    int i;
    for (i = 0; i < c->nerrors; i++)
        if (c->errors[i].line == line && strstr(c->errors[i].text, text)) return 1;
    for (i = 0; i < c->nerrors; i++) fprintf(stderr, "  (line %d: %s)\n", c->errors[i].line, c->errors[i].text);
    return 0;
}

/* Whether a warning on line contains text */
static int has_warning(const EupConfig *c, int line, const char *text)
{
    int i;
    for (i = 0; i < c->nwarnings; i++)
        if (c->warnings[i].line == line && strstr(c->warnings[i].text, text)) return 1;
    for (i = 0; i < c->nwarnings; i++) fprintf(stderr, "  (warning, line %d: %s)\n", c->warnings[i].line, c->warnings[i].text);
    return 0;
}

static const char *GOOD =
    "; a comment\n"
    "# another\n"
    "[installer]\n"
    "product = Mill\n"
    "version = 2.0.1\n"
    "description_screen = :Texts:About.txt ; after a value\n"
    "\n"
    "[type easy]\n"
    "label = Easy Install\n"
    "description = Mill, and more.\n"
    "[type minimal]\n"
    "label = Minimal Install\n"
    "remove_old = no\n"
    "[type custom]\n"
    "label = Custom Install\n"
    "custom = yes\n"
    "[group plugins]\n"
    "label = Plug-Ins\n"
    "selectable = yes\n"
    "[component mill]\n"
    "label = Mill\n"
    "source = Mill.bin\n"
    "flags = bundle, no_init\n"
    "types = easy, minimal, Custom\n"
    "[component fonts]\n"
    "group = plugins\n"
    "label = Web Fonts\n"
    "source = Plug-Ins:Web Fonts.bin\n"
    "destination = %chosen%:Mill Plug-Ins\n"
    "types = easy, custom\n"
    "[component sdk]\n"
    "label = SDK\n"
    "source = SDK\n"
    "destination = :SDK 100%%\n"
    "type = TEXT\n"
    "creator = ttxt\n"
    "text = yes\n"
    "compress = no\n"
    "types = custom\n"
    "[upgrade]\n"
    "detect = Mill\n"
    "remove = %chosen%:Mill Fonts\n"
    "remove = :Mill Plug-Ins:Finger\n"
    "keep = Mill Settings\n"
    "[layout]\n"
    "window = 40, 60, 300, 190\n"
    "place = Mill: 18, 9\n"
    "place = Mill Plug-Ins: 108, 79\n";

static void test_good(void)
{
    EupConfig c;
    CHECK(parse(&c, GOOD) == 0);
    CHECK(!strcmp(EUP_C(c.product), "Mill"));
    CHECK(!strcmp(EUP_C(c.title), "Install Mill 2.0.1"));
    CHECK(!strcmp(EUP_C(c.file), "Install Mill 2.0.1"));
    CHECK(!strcmp(EUP_C(c.output), "Install-Mill-2.0.1"));
    CHECK(!strcmp(EUP_C(c.folder), "Mill"));
    CHECK(c.get_info == c.title);
    CHECK(c.version_string == c.version && c.nwarnings == 0);
    CHECK(c.vers[0] == 0x02 && c.vers[1] == 0x01 && c.vers[2] == 0x80 && c.vers[3] == 0);
    CHECK(c.description_screen.n == 2 && !strcmp(EUP_C(c.description_screen.names[1]), "About.txt"));
    CHECK(c.ntypes == 3 && c.types[2].custom && !c.types[1].remove_old && c.types[0].remove_old);
    CHECK(c.ngroups == 1 && c.groups[0].selectable);
    CHECK(c.ncomps == 3);
    CHECK(c.comps[0].has_flags && c.comps[0].flags == (EUP_FL_BUNDLE | EUP_FL_NO_INIT));
    CHECK(c.comps[0].ntypes == 3 && c.comps[0].types[2] == 2);
    CHECK(c.comps[0].checked && c.comps[1].checked && !c.comps[2].checked);    /* in a type without the list, or not */
    CHECK(c.comps[1].group == 0 && c.comps[0].group == -1);
    CHECK(c.comps[1].source.n == 2 && c.comps[1].destination.n == 1);
    CHECK(!strcmp(EUP_C(c.comps[2].destination.names[0]), "SDK 100%"));
    CHECK(c.comps[2].has_type && c.comps[2].type == 0x54455854UL && c.comps[2].text && !c.comps[2].compress);
    CHECK(c.nremove == 2 && c.remove[1].n == 2 && c.nkeep == 1 && c.keep[0].n == 1);
    CHECK(c.detect && !strcmp(EUP_C(c.detect), "Mill") && !c.require);
    CHECK(c.has_window && c.window[2] == 300 && c.nplaces == 2 && c.places[1].h == 108);
    eup_free(&c);
    /* CR and CR LF line ends read the same */
    {
        char *t = (char *)malloc(strlen(GOOD) + 1), *crlf = (char *)malloc(strlen(GOOD) * 2 + 1), *p, *q;
        strcpy(t, GOOD);
        for (p = t; *p; p++) if (*p == '\n') *p = '\r';
        CHECK(parse(&c, t) == 0 && c.ncomps == 3);
        eup_free(&c);
        for (p = (char *)GOOD, q = crlf; *p; p++) { if (*p == '\n') *q++ = '\r'; *q++ = *p; }
        *q = 0;
        CHECK(parse(&c, crlf) == 0 && c.nplaces == 2);
        eup_free(&c);
        free(t);
        free(crlf);
    }
}

#define BASE "[installer]\nproduct = P\n[type a]\nlabel = A\n[component x]\nlabel = X\nsource = x\ntypes = a\n"

static void test_errors(void)
{
    EupConfig c;
    CHECK(parse(&c, "product = P\n") && has_error(&c, 1, "not in a section"));
    eup_free(&c);
    CHECK(parse(&c, "") && has_error(&c, 0, "No [installer]") && has_error(&c, 0, "No [type]"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[installer]\n") && has_error(&c, 9, "A second [installer]"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[installr]\n") && has_error(&c, 9, "not a section"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[type a]\n") && has_error(&c, 9, "A second [type a]"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[type A b]\n") && has_error(&c, 9, "letters, digits"));
    eup_free(&c);
    CHECK(parse(&c, "[installer]\nproduct = P\nproduct = Q\n" "[type a]\nlabel = A\n[component x]\nlabel = X\nsource = x\ntypes = a\n") &&
          has_error(&c, 3, "given twice"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[upgrade]\ncolour = red\n") && has_error(&c, 10, "not a key of [upgrade]"));
    eup_free(&c);
    CHECK(parse(&c, BASE "just words\n") && has_error(&c, 9, "Not a key = value"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\ntypes = a, b\n") && has_error(&c, 12, "no [type b]"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\ntypes = a\ncompress = maybe\n") && has_error(&c, 13, "yes or no"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\ntypes = a\n") && has_error(&c, 9, "source is needed"));
    eup_free(&c);
    CHECK(parse(&c, "[installer]\nversion = 1.0\n[type a]\nlabel = A\n[component x]\nlabel = X\nsource = x\ntypes = a\n") &&
          has_error(&c, 1, "product is needed"));
    eup_free(&c);
    CHECK(parse(&c, "[installer]\nproduct = P\nversion = one\n[type a]\nlabel = A\n[component x]\nlabel = X\nsource = x\ntypes = a\n") &&
          has_error(&c, 3, "a number like"));
    eup_free(&c);
    CHECK(parse(&c, "[installer]\nproduct = P\nicon = i.bin\n[type a]\nlabel = A\n[component x]\nlabel = X\nsource = x\ntypes = a\n") &&
          has_error(&c, 1, "creator of its own"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[type b]\nlabel = B\n") && has_error(&c, 9, "no component is in it"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\ntypes = a\ndestination = Macintosh HD:Stuff\n") &&
          has_error(&c, 13, "starts with a disk's name"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\ntypes = a\ndestination = %system_folder%\n") &&
          has_error(&c, 13, "not in this version"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\ntypes = a\ndestination = %chosn%:x\n") &&
          has_error(&c, 13, "not a substitution"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = 50% off\ntypes = a\n") && has_error(&c, 11, "is written %%"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = a::b\ntypes = a\n") && has_error(&c, 11, "an empty name"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = .hidden\ntypes = a\n") && has_error(&c, 11, "starts with a period"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\nas = A name that is far too long for a Mac\ntypes = a\n") &&
          has_error(&c, 12, "longer than 31"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\ntypes = a\ntype = TOOLONG\n") && has_error(&c, 13, "four characters"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\ntypes = a\nflags = bundle, sparkly\n") && has_error(&c, 13, "not a Finder flag"));
    eup_free(&c);
    /* kept, so not removed: a warning, not an error */
    CHECK(parse(&c, BASE "[upgrade]\nremove = Old\nkeep = :Old\n") == 0 && has_warning(&c, 11, "\"Old\" is removed and kept"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[upgrade]\nkeep = Prefs\nremove = :Prefs:Old\nremove = Other\nremove = Old\n") == 0 &&
          c.nwarnings == 1 && has_warning(&c, 11, "\"Prefs:Old\" is removed and kept"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[upgrade]\nremove = Old\nkeep = :Old:Settings\n") == 0 && c.nwarnings == 0);
    eup_free(&c);
    /* require: the software that must be there first, found by detect */
    CHECK(parse(&c, BASE "[upgrade]\ndetect = Mill\nrequire = Mill\n") == 0 && c.require && !strcmp(EUP_C(c.require), "Mill"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[upgrade]\nrequire = Mill 2.0\ndetect = Mill\n") == 0 && !strcmp(EUP_C(c.require), "Mill 2.0"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[upgrade]\nremove = Old\nrequire = Mill\n") && has_error(&c, 11, "require needs detect"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[upgrade]\ndetect = Mill\nrequire = Mill\nrequire = Mill\n") && has_error(&c, 12, "given twice"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[upgrade]\ndetect = M\nrequire = A name for the software that is longer than sixty-three characters\n") &&
          has_error(&c, 11, "longer than 63"));
    eup_free(&c);
    /* a version to show, and the number for 'vers' */
    CHECK(parse(&c, "[installer]\nproduct = P\nversion = 2.0.1\nversion_string = 2.0.1-dev-46a0304\n"
                    "[type a]\nlabel = A\n[component x]\nlabel = X\nsource = x\ntypes = a\n") == 0 &&
          !strcmp(EUP_C(c.title), "Install P 2.0.1-dev-46a0304") && !strcmp(EUP_C(c.version), "2.0.1") &&
          c.vers[0] == 0x02 && c.vers[1] == 0x01);
    eup_free(&c);
    CHECK(parse(&c, "[installer]\nproduct = P\nversion_string = nightly\n"
                    "[type a]\nlabel = A\n[component x]\nlabel = X\nsource = x\ntypes = a\n") &&
          has_error(&c, 1, "version_string needs version"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[installer]\n") && has_error(&c, 9, "A second [installer]"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[layout]\nwindow = 1, 2, 3\nplace = Nowhere\n") && has_error(&c, 10, "left, top, width") &&
          has_error(&c, 11, "place: a name, a colon"));
    eup_free(&c);
    /* a custom list's groups stay together */
    CHECK(parse(&c, "[installer]\nproduct = P\n[type c]\nlabel = C\ncustom = yes\n[group g]\nlabel = G\n[group h]\nlabel = H\n"
                    "[component x]\nlabel = X\nsource = x\ngroup = g\ntypes = c\n"
                    "[component y]\nlabel = Y\nsource = y\ngroup = h\ntypes = c\n"
                    "[component z]\nlabel = Z\nsource = z\ngroup = g\ntypes = c\n") &&
          has_error(&c, 20, "is split by another"));
    eup_free(&c);
    CHECK(parse(&c, BASE "[component y]\nlabel = Y\nsource = y\ntypes = a\ngroup = nope\n") && has_error(&c, 13, "no [group nope]"));
    eup_free(&c);
}

static void test_paths(void)
{
    EupConfig c;
    EupPath p, q;
    char why[200];
    memset(&c, 0, sizeof c);
    CHECK(eup_split_path(&c, ":Plug-Ins:Web Fonts.bin", EUP_SOURCE, &p, why) && p.n == 2);
    CHECK(eup_split_path(&c, "Plug-Ins:Web Fonts.bin", EUP_SOURCE, &q, why) && eup_same_path(&p, &q));
    CHECK(eup_split_path(&c, "PLUG-INS:web FONTS.bin", EUP_SOURCE, &q, why) && eup_same_path(&p, &q));      /* case, as the Mac */
    CHECK(eup_split_path(&c, "Folder:", EUP_SOURCE, &p, why) && p.n == 1);
    CHECK(!eup_split_path(&c, ":", EUP_SOURCE, &p, why));
    CHECK(eup_split_path(&c, "%chosen%", EUP_INSTALLED, &p, why) && p.n == 0);
    CHECK(eup_split_path(&c, "%chosen%:A:B", EUP_INSTALLED, &p, why) && p.n == 2 && !strcmp(EUP_C(p.names[1]), "B"));
    CHECK(eup_split_path(&c, "A", EUP_INSTALLED, &q, why) && q.n == 1);
    CHECK(eup_split_path(&c, ":A:B", EUP_INSTALLED, &q, why) && eup_same_path(&p, &q));
    CHECK(!eup_split_path(&c, "A:B", EUP_INSTALLED, &q, why) && strstr(why, "disk's name"));
    CHECK(eup_split_path(&c, "100%%:x", EUP_SOURCE, &p, why) && !strcmp(EUP_C(p.names[0]), "100%"));
    CHECK(!eup_split_path(&c, "%chosen%x", EUP_INSTALLED, &p, why));
    CHECK(eup_name(&c, "Fine Name", why) != NULL);
    CHECK(eup_name(&c, "Bad:Name", why) == NULL);
    eup_free(&c);
}

static void test_roman(void)
{
    const char *u = "caf\xC3\xA9 \xE2\x80\xA2 \xE2\x84\xA2 \xC2\xA9 \xCF\x80\n";       /* café • ™ © π */
    char out[64], back[200];
    int line = 0;
    long n = eup_from_utf8(u, (long)strlen(u), out, &line);
    CHECK(n == 13 && (unsigned char)out[3] == 0x8E && (unsigned char)out[5] == 0xA5 && (unsigned char)out[7] == 0xAA &&
          (unsigned char)out[9] == 0xA9 && (unsigned char)out[11] == 0xB9);
    eup_to_utf8((unsigned char *)out, (int)n, back);
    CHECK(!strcmp(back, u));
    CHECK(eup_from_utf8("ok\nbad \xE2\x82\xAC\xF0\x9F\x98\x80\n", 15, out, &line) < 0 && line == 2);     /* an emoji */
    CHECK(eup_from_utf8("\xEF\xBB\xBFx", 4, out, &line) == 1 && out[0] == 'x');                         /* a byte order mark */
}

static void test_macbinary(void)
{
    EupMacInfo m, r;
    unsigned char head[128], file[1024];
    unsigned long size;
    memset(&m, 0, sizeof m);
    memcpy(m.name, "\005Hello", 6);
    m.type = 0x4150504CUL;
    m.creator = 0x45555069UL;
    m.flags = 0x2100;
    m.dlen = 300;
    m.rlen = 10;
    eup_macbinary_header(&m, head);
    size = 128 + 384 + 128;
    CHECK(eup_macbinary_read(head, size, &r) && r.dlen == 300 && r.rlen == 10 && r.flags == 0x2100 && r.type == m.type &&
          r.data_at == 128 && r.rsrc_at == 512 && r.name[0] == 5);
    CHECK(eup_macbinary_read(head, 128 + 384 + 10, &r));             /* the last fork not padded */
    CHECK(!eup_macbinary_read(head, size + 128, &r));               /* longer than the forks */
    head[124] ^= 1;
    CHECK(!eup_macbinary_read(head, size, &r));                     /* MacBinary II with a bad CRC */
    /* as Retro68 writes it: version 0, no CRC */
    eup_macbinary_header(&m, head);
    head[122] = head[123] = head[124] = head[125] = 0;
    CHECK(eup_macbinary_read(head, size, &r) && r.rlen == 10);
    memset(file, 'A', sizeof file);
    CHECK(!eup_macbinary_read(file, sizeof file, &r));              /* text isn't */
}

typedef struct { const unsigned char *p; unsigned long len; } Mem;

static int mem_read(void *ref, unsigned long at, void *buf, long n)
{
    Mem *m = (Mem *)ref;
    if (at + n > m->len) return 1;
    memcpy(buf, m->p + at, n);
    return 0;
}

static void test_rfork(void)
{
    EupRWrite *w = eup_rwrite_new();
    unsigned char *f;
    unsigned long len;
    EupResRef *list;
    int n;
    Mem m;
    eup_rwrite_add(w, 0x54455854UL, 128, (const unsigned char *)"\003INI", 0, (const unsigned char *)"abc", 3);
    eup_rwrite_add(w, 0x434F4445UL, 0, NULL, 0x20, (const unsigned char *)"zz", 2);
    eup_rwrite_add(w, 0x54455854UL, -1, NULL, 0, (const unsigned char *)"", 0);
    eup_rwrite_add(w, 0x54455854UL, 128, (const unsigned char *)"\003ini", 0, (const unsigned char *)"abcd", 4);   /* replaces */
    CHECK(eup_rwrite_has(w, 0x434F4445UL, 0));
    f = eup_rwrite_finish(w, &len);
    CHECK(f != NULL);
    m.p = f;
    m.len = len;
    CHECK(eup_rfork_list(mem_read, &m, len, &list, &n) == 0 && n == 3);
    if (n == 3) {
        int i, seen = 0;
        for (i = 0; i < n; i++) {
            if (list[i].type == 0x54455854UL && list[i].id == 128)
                seen += list[i].len == 4 && !memcmp(f + list[i].at, "abcd", 4) && list[i].has_name && !memcmp(list[i].name, "\003ini", 4);
            if (list[i].type == 0x434F4445UL) seen += list[i].attrs == 0x20 && list[i].len == 2 && !list[i].has_name;
            if (list[i].id == -1) seen += list[i].len == 0;
        }
        CHECK(seen == 3);
    }
    free(list);
    CHECK(eup_rfork_list(mem_read, &m, 10, &list, &n) < 0);         /* cut short */
    free(f);
}

/* ---- BinHex: decoded here to check what's encoded ---- */

typedef struct { unsigned char *p; long len, cap; } Buf;

static int buf_out(void *ref, const unsigned char *p, long n)
{
    Buf *b = (Buf *)ref;
    if (b->len + n > b->cap) { b->cap = (b->len + n) * 2; b->p = (unsigned char *)realloc(b->p, b->cap); }
    memcpy(b->p + b->len, p, n);
    b->len += n;
    return 0;
}

static long unhqx(const unsigned char *text, long len, unsigned char *out)
{
    static const char A[] = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";
    unsigned char packed[70000];
    long i, np = 0, n = 0;
    unsigned long bits = 0;
    int nbits = 0;
    const unsigned char *s = (const unsigned char *)strchr((const char *)text, ':') + 1;
    for (; s < text + len && *s != ':'; s++) {
        const char *k;
        if (*s == '\r' || *s == '\n') continue;
        k = strchr(A, *s);
        bits = (bits << 6) | (unsigned long)(k - A);
        nbits += 6;
        if (nbits >= 8) { nbits -= 8; packed[np++] = (unsigned char)(bits >> nbits); }
    }
    for (i = 0; i < np; i++) {
        if (packed[i] != 0x90) { out[n++] = packed[i]; continue; }
        if (packed[++i] == 0) out[n++] = 0x90;
        else { int k; for (k = 1; k < packed[i]; k++, n++) out[n] = out[n - 1]; }
    }
    return n;
}

static void test_binhex(void)
{
    unsigned char data[3000], rsrc[50], raw[80000];
    Buf b = { 0, 0, 0 };
    long n, at;
    int i;
    for (i = 0; i < 3000; i++) data[i] = (unsigned char)(i < 1000 ? 0x90 : i < 2000 ? 'x' : i * 7);
    for (i = 0; i < 50; i++) rsrc[i] = (unsigned char)i;
    CHECK(eup_binhex((const unsigned char *)"\004Test", 0x54455854UL, 0x74747874UL, 0x0100, data, 3000, rsrc, 50, buf_out, &b) == 0);
    CHECK(b.len > 45 && !memcmp(b.p, "(This file must be converted with BinHex 4.0)\r\r:", 48));
    for (at = 0, i = 0; at < b.len; at++) if (b.p[at] == '\r') i++;
    CHECK(i == 2 + (b.len - 48 + 63) / 65);             /* lines of 64 characters */
    n = unhqx(b.p, b.len, raw);
    /* the header: name, 0, type, creator, flags, lengths, CRC; then each fork and its CRC */
    CHECK(n == 24 + 2 + 3000 + 2 + 50 + 2);           /* the header (24) and each part with its CRC */
    CHECK(raw[0] == 4 && !memcmp(raw + 1, "Test", 4) && eup_get32(raw + 6) == 0x54455854UL && eup_get16(raw + 14) == 0x0100);
    CHECK(eup_get32(raw + 16) == 3000 && eup_get32(raw + 20) == 50);
    CHECK(eup_crc16(0, raw, 24) == eup_get16(raw + 24));
    CHECK(!memcmp(raw + 26, data, 3000) && eup_crc16(0, data, 3000) == eup_get16(raw + 3026));
    CHECK(!memcmp(raw + 3028, rsrc, 50) && eup_crc16(0, rsrc, 50) == eup_get16(raw + 3078));
    CHECK(eup_crc16(0, (const unsigned char *)"123456789", 9) == 0x31C3);       /* XMODEM's check value */
    free(b.p);
}

/* ---- deflate, and Mill's inflate ---- */

static int collect(void *ref, const unsigned char *p, long n) { return buf_out(ref, p, n); }

static int round_trip(const unsigned char *data, long len, long piece, long *packed)
{
    Buf z = { 0, 0, 0 }, back = { 0, 0, 0 };
    EupDeflate *d = eup_deflate_begin(collect, &z);
    void *g;
    long at;
    int r, ok;
    unsigned long size;
    for (at = 0; at < len; at += piece) eup_deflate_data(d, data + at, len - at > piece ? piece : len - at);
    eup_deflate_end(d, &size);
    *packed = z.len;
    g = gz_begin(collect, &back);
    r = gz_data(g, z.p, z.len);
    gz_end(g);
    ok = r == 1 && back.len == len && (!len || !memcmp(back.p, data, len)) && (long)size == z.len &&
         (unsigned long)(z.p[z.len - 8] | z.p[z.len - 7] << 8 | z.p[z.len - 6] << 16 | (unsigned long)z.p[z.len - 5] << 24) == eup_crc32(0, data, len);
    free(z.p);
    free(back.p);
    return ok;
}

static void test_deflate(void)
{
    static unsigned char d[300000];
    long i, packed;
    unsigned long x = 1;
    CHECK(eup_crc32(0, (const unsigned char *)"123456789", 9) == 0xCBF43926UL);
    CHECK(round_trip(d, 0, 1, &packed));
    CHECK(round_trip((const unsigned char *)"a", 1, 1, &packed));
    memset(d, 0, sizeof d);
    CHECK(round_trip(d, 300000, 4096, &packed) && packed < 1000);
    for (i = 0; i < 300000; i++) { x = x * 1103515245UL + 12345; d[i] = (unsigned char)(x >> 16); }
    CHECK(round_trip(d, 300000, 4096, &packed) && packed < 300000 + 3000);      /* random: stored as Huffman, a little bigger */
    for (i = 0; i < 300000; i++) d[i] = "the quick brown fox jumps over the lazy dog. "[i % 45] ^ (unsigned char)((i / 997) & 1);
    CHECK(round_trip(d, 300000, 1, &packed) && packed < 20000);
    CHECK(round_trip(d, 300000, 777, &packed));
    for (i = 0; i < 300000; i++) { x = x * 1103515245UL + 12345; d[i] = (unsigned char)("abcd"[(x >> 16) & 3]); }
    CHECK(round_trip(d, 300000, 65536, &packed) && packed < 300000 / 3);
}

/* ---- records ---- */

static void test_records(void)
{
    EupRecord r, s;
    EupConfig c;
    EupPath p;
    unsigned char buf[300];
    char why[200];
    const unsigned char *rest;
    int nrest;
    long n;
    memset(&r, 0, sizeof r);
    r.kind = EUP_REC_FILE;
    r.flags = EUP_REC_STORED;
    r.type = 1; r.creator = 2; r.finder = 0x2000; r.dlen = 300; r.rlen = 40; r.packed = 340; r.at = 99;
    memcpy(r.name, "\004Mill", 5);
    r.n = 3;
    r.names = (const unsigned char *)"\003SDK\002MP\007Read Me";
    n = eup_record_write(&r, buf);
    CHECK(n == EUP_REC_FIXED + 5 + 1 + 4 + 3 + 8);
    CHECK(eup_record_read(buf, &s) == buf + n && s.dlen == 300 && s.finder == 0x2000 && s.at == 99 && s.n == 3 && s.name[0] == 4);
    memset(&c, 0, sizeof c);
    eup_split_path(&c, "sdk", EUP_SOURCE, &p, why);
    CHECK(eup_record_under(&s, &p, &rest, &nrest) && nrest == 2 && rest[0] == 2);
    eup_split_path(&c, "SDK:MP:Read Me", EUP_SOURCE, &p, why);
    CHECK(eup_record_under(&s, &p, &rest, &nrest) && nrest == 0);
    eup_split_path(&c, "Other", EUP_SOURCE, &p, why);
    CHECK(!eup_record_under(&s, &p, NULL, NULL));
    eup_free(&c);
}

/* ---- packaging, from files in memory ---- */

typedef struct { const char *path; int folder; const unsigned char *data; unsigned long len; } MFile;

static MFile FILES[8];
static int NFILES;

static void path_of(PStr *names, int n, char *out)
{
    int i;
    out[0] = 0;
    for (i = 0; i < n; i++) { if (i) strcat(out, "/"); strncat(out, EUP_C(names[i]), names[i][0]); }
}

static int f_stat(void *ref, PStr *names, int n, EupStat *st)
{
    char p[300];
    int i;
    (void)ref;
    memset(st, 0, sizeof *st);
    path_of(names, n, p);
    for (i = 0; i < NFILES; i++)
        if (!strcmp(FILES[i].path, p)) { st->kind = FILES[i].folder ? EUP_FOLDER : EUP_FILE; st->dlen = FILES[i].len; }
    return 0;
}

static int f_entry(void *ref, PStr *names, int n, int index, unsigned char *name, char *why)
{
    char p[300];
    int i, k = 0;
    size_t pl;
    (void)ref;
    (void)why;
    path_of(names, n, p);
    pl = strlen(p);
    for (i = 0; i < NFILES; i++)
        if (!strncmp(FILES[i].path, p, pl) && FILES[i].path[pl] == '/' && !strchr(FILES[i].path + pl + 1, '/') && k++ == index) {
            name[0] = (unsigned char)strlen(FILES[i].path + pl + 1);
            memcpy(name + 1, FILES[i].path + pl + 1, name[0]);
            return 0;
        }
    return 1;
}

static void *f_open(void *ref, PStr *names, int n, int fork)
{
    char p[300];
    int i;
    (void)ref;
    path_of(names, n, p);
    for (i = 0; i < NFILES; i++) if (!fork && !strcmp(FILES[i].path, p)) return &FILES[i];
    return NULL;
}

static int f_read_at(void *file, unsigned long at, void *buf, long n)
{
    MFile *f = (MFile *)file;
    if (at + n > f->len) return 1;
    memcpy(buf, f->data + at, n);
    return 0;
}

static void f_close(void *file) { (void)file; }

extern const unsigned char eup_shell[];
extern const unsigned long eup_shell_len;

static void test_package(void)
{
    static const char *INI =
        "[installer]\nproduct = T\nlicense_screen = L.txt\n"
        "[type a]\nlabel = A\n"
        "[component app]\nlabel = App\nsource = App.bin\nflags = bundle\nas = The App\ntypes = a\n"
        "[component docs]\nlabel = Docs\nsource = Docs\ndestination = %chosen%:Docs\ntext = yes\ntypes = a\n";
    static unsigned char mb[128 + 128 + 128];
    EupMacInfo m, shell;
    EupConfig c;
    EupFS fs;
    EupPackIn in;
    EupPackOut out;
    EupPackage *pack;
    Buf data = { 0, 0, 0 };
    Mem rm;
    EupResRef *list;
    char why[200];
    int n, i, found = 0;
    memset(&m, 0, sizeof m);
    memcpy(m.name, "\003App", 4);
    m.type = 0x4150504CUL;
    m.creator = 0x58585858UL;
    m.dlen = 5;
    m.rlen = 7;
    eup_macbinary_header(&m, mb);
    memcpy(mb + 128, "hello", 5);
    memcpy(mb + 256, "rsrc!!!", 7);
    NFILES = 0;
    FILES[NFILES].path = "App.bin"; FILES[NFILES].data = mb; FILES[NFILES++].len = 128 + 128 + 7;
    FILES[NFILES].path = "L.txt"; FILES[NFILES].data = (const unsigned char *)"Terms\n\nca\xC3\xA9\n"; FILES[NFILES++].len = 12;
    FILES[NFILES].path = "Docs"; FILES[NFILES++].folder = 1;
    FILES[NFILES].path = "Docs/A"; FILES[NFILES].data = (const unsigned char *)"line one\r\nline two\n"; FILES[NFILES++].len = 19;
    FILES[NFILES].path = "Docs/Sub"; FILES[NFILES++].folder = 1;
    FILES[NFILES].path = "Docs/.hidden"; FILES[NFILES].data = (const unsigned char *)"x"; FILES[NFILES++].len = 1;
    CHECK(parse(&c, INI) == 0);
    memset(&fs, 0, sizeof fs);
    fs.stat = f_stat; fs.entry = f_entry; fs.open = f_open; fs.read_at = f_read_at; fs.close = f_close;
    fs.utf8_text = 1;
    CHECK(eup_macbinary_read(eup_shell, eup_shell_len, &shell));
    rm.p = eup_shell + shell.rsrc_at;
    rm.len = shell.rlen;
    memset(&in, 0, sizeof in);
    in.fs = &fs; in.config = &c; in.ini = INI; in.ini_len = (long)strlen(INI); in.ini_name = (const unsigned char *)"\005t.ini";
    in.shell_read = mem_read; in.shell_ref = &rm; in.shell_len = shell.rlen;
    in.data_out = buf_out; in.data_ref = &data;
    CHECK(eup_pack_check(&in, &pack) == 0);
    CHECK(pack && eup_pack(pack, &out, why) == 0);
    CHECK(out.files == 2 && out.folders == 1 && out.unpacked == 12 + 18 && out.creator == 0x45555069UL);
    rm.p = out.rsrc;
    rm.len = out.rsrc_len;
    CHECK(eup_rfork_list(mem_read, &rm, out.rsrc_len, &list, &n) == 0);
    for (i = 0; i < n; i++) {
        const unsigned char *p = out.rsrc + list[i].at;
        if (list[i].type == 0x54455854UL && list[i].id == 128) found += list[i].len == strlen(INI);
        if (list[i].type == 0x54455854UL && list[i].id == 129)          /* the license, Mac Roman, CR, no blank end */
            found += list[i].len == 10 && !memcmp(p, "Terms\r\rca\x8E", 10) && list[i].has_name && !memcmp(list[i].name, "\005L.txt", 6);
        if (list[i].type == 0x4566696CUL && list[i].id == 128) {       /* 'Efil': App, Docs:A, Docs:Sub */
            EupRecord r;
            const unsigned char *q = p + 2;
            int k = 0;
            Buf back = { 0, 0, 0 };
            found += eup_get16(p) == 3;
            for (k = 0; k < 3; k++) {
                void *g;
                q = eup_record_read(q, &r);
                if (r.kind != EUP_REC_FILE) { found += r.n == 2 && r.name[0] == 3; continue; }
                back.len = 0;
                g = gz_begin(collect, &back);
                gz_data(g, data.p + r.at, (long)r.packed);
                gz_end(g);
                if (r.n == 1) found += r.dlen == 5 && r.rlen == 7 && r.finder == 0x2000 && r.creator == 0x58585858UL &&
                                     !memcmp(r.name, "\007The App", 8) && back.len == 12 && !memcmp(back.p, "hellorsrc!!!", 12);
                else found += r.dlen == 18 && r.type == 0x54455854UL && back.len == 18 && !memcmp(back.p, "line one\rline two\r", 18);
            }
            free(back.p);
        }
        if (list[i].type == 0x76657273UL) found += 100;                 /* no version given: no 'vers' */
    }
    if (found != 6) fprintf(stderr, "  found %d\n", found);
    CHECK(found == 6);
    free(list);
    free(out.rsrc);
    free(data.p);
    eup_free(&c);
    /* the files' rules */
    CHECK(parse(&c, "[installer]\nproduct = T\n[type a]\nlabel = A\n"
                    "[component x]\nlabel = X\nsource = Missing\ntypes = a\n"
                    "[component y]\nlabel = Y\nsource = Docs\nas = Z\ntypes = a\n"
                    "[component z]\nlabel = Z\nsource = L.txt\nas = A\ntypes = a\n") == 0);
    in.config = &c;
    CHECK(eup_pack_check(&in, &pack) > 0 && has_error(&c, 5, "no file or folder \"Missing\"") &&
          has_error(&c, 9, "as is for a file") && has_error(&c, 14, "the same place"));
    eup_free(&c);
}

int main(void)
{
    test_good();
    test_errors();
    test_paths();
    test_roman();
    test_macbinary();
    test_rfork();
    test_binhex();
    test_deflate();
    test_records();
    test_package();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
