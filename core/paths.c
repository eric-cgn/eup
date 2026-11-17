/* Paths and names, by the INI's rules (see eup.h), and Mac Roman */
#include <string.h>

#include "eup.h"
#include "private.h"

/* Mac Roman's upper half, as Unicode code points */
static const unsigned short ROMAN[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

long eup_from_utf8(const char *in, long len, char *out, int *bad_line)
{
    const unsigned char *p = (const unsigned char *)in, *end = p + len;
    long n = 0;
    int line = 1;
    if (len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) p += 3;     /* a byte order mark */
    while (p < end) {
        unsigned long u;
        int k, more;
        if (*p < 0x80) {
            if (*p == '\n') line++;
            out[n++] = (char)*p++;
            continue;
        }
        if ((*p & 0xE0) == 0xC0) { u = *p & 0x1F; more = 1; }
        else if ((*p & 0xF0) == 0xE0) { u = *p & 0x0F; more = 2; }
        else if ((*p & 0xF8) == 0xF0) { u = *p & 0x07; more = 3; }
        else { *bad_line = line; return -1; }
        if (end - p <= more) { *bad_line = line; return -1; }
        for (k = 1; k <= more; k++) {
            if ((p[k] & 0xC0) != 0x80) { *bad_line = line; return -1; }
            u = (u << 6) | (p[k] & 0x3F);
        }
        p += more + 1;
        for (k = 0; k < 128 && ROMAN[k] != u; k++) ;
        if (k == 128) { *bad_line = line; return -1; }
        out[n++] = (char)(0x80 + k);
    }
    return n;
}

void eup_to_utf8(const unsigned char *in, int len, char *out)
{
    int i, k = 0;
    for (i = 0; i < len; i++) {
        unsigned int u = in[i] < 0x80 ? in[i] : ROMAN[in[i] - 0x80];
        if (u < 0x80) out[k++] = (char)u;
        else if (u < 0x800) { out[k++] = (char)(0xC0 | (u >> 6)); out[k++] = (char)(0x80 | (u & 0x3F)); }
        else { out[k++] = (char)(0xE0 | (u >> 12)); out[k++] = (char)(0x80 | ((u >> 6) & 0x3F)); out[k++] = (char)(0x80 | (u & 0x3F)); }
    }
    out[k] = 0;
}

/* Mac Roman's letters without case, as the File Manager compares names (near enough: the accented
   capitals that Mac Roman has) */
static unsigned char fold(unsigned char c)
{
    static const unsigned char UP[] = { 0x8A, 0x80, 0x8C, 0x81, 0x8D, 0x82, 0x8E, 0x83, 0x96, 0x84, 0x9A, 0x85, 0x9F, 0x86,
                                        0x88, 0xCB, 0x8B, 0xCC, 0x9B, 0xCD, 0xBE, 0xAE, 0xBF, 0xAF, 0xCF, 0xCE, 0 };
    int i;
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 32);
    for (i = 0; UP[i]; i += 2) if (c == UP[i]) return UP[i + 1];
    return c;
}

int eup_same_name(const unsigned char *a, const unsigned char *b)
{
    int i;
    if (a[0] != b[0]) return 0;
    for (i = 1; i <= a[0]; i++) if (fold(a[i]) != fold(b[i])) return 0;
    return 1;
}

int eup_same_path(const EupPath *a, const EupPath *b)
{
    int i;
    if (a->n != b->n) return 0;
    for (i = 0; i < a->n; i++) if (!eup_same_name(a->names[i], b->names[i])) return 0;
    return 1;
}

int eup_path_in(const EupPath *a, const EupPath *b)
{
    int i;
    if (a->n < b->n) return 0;
    for (i = 0; i < b->n; i++) if (!eup_same_name(a->names[i], b->names[i])) return 0;
    return 1;
}

/* One name from s[0..n), %% made %; NULL and why if it breaks a rule */
static PStr name_of(EupConfig *c, const char *s, int n, char *why)
{
    char buf[256];
    int i, k = 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '%') {
            if (i + 1 < n && s[i + 1] == '%') i++;
            else { eup_fmt(why, "a %% in a name is written %%%%", NULL, NULL); return NULL; }
        }
        if (k >= 255) break;
        buf[k++] = s[i];
    }
    buf[k] = 0;
    if (k == 0) { eup_fmt(why, "an empty name (two colons together)", NULL, NULL); return NULL; }
    if (k > 31) { eup_fmt(why, "\"%s\" is longer than 31 characters", buf, NULL); return NULL; }
    if (buf[0] == '.') { eup_fmt(why, "\"%s\" starts with a period", buf, NULL); return NULL; }
    for (i = 0; i < k; i++)
        if ((unsigned char)buf[i] < 32) { eup_fmt(why, "\"%s\" has a control character", buf, NULL); return NULL; }
    return eup_pstr(c, buf, k);
}

PStr eup_name(EupConfig *c, const char *s, char *why)
{
    if (strchr(s, ':')) { eup_fmt(why, "\"%s\": a name can\'t have a colon", s, NULL); return NULL; }
    return name_of(c, s, (int)strlen(s), why);
}

/* The substitutions of later versions, refused by name */
static const char *const LATER[] = { "%system_folder%", "%extensions%", "%control_panels%", "%preferences%", "%fonts%",
                                     "%apple_menu%", "%startup_items%", "%system_file%", 0 };

int eup_split_path(EupConfig *c, const char *s, int kind, EupPath *out, char *why)
{
    const char *p = s;
    int n = 0, i, len = (int)strlen(s);
    PStr names[64];
    out->n = 0;
    out->names = NULL;
    if (kind == EUP_INSTALLED && p[0] == '%' && p[1] != '%') {
        const char *e = strchr(p + 1, '%');
        if (e && e - p == 7 && !memcmp(p, "%chosen", 7)) {
            p = e + 1;
            if (*p && *p != ':') { eup_fmt(why, "a colon goes after %%chosen%%", NULL, NULL); return 0; }
        } else {
            char sub[64];
            int k = e ? (int)(e - p + 1) : len;
            if (k > 63) k = 63;
            memcpy(sub, p, k);
            sub[k] = 0;
            for (i = 0; LATER[i]; i++)
                if (!strcmp(sub, LATER[i])) { eup_fmt(why, "%s is not in this version of EUP: everything goes inside %%chosen%%", sub, NULL); return 0; }
            eup_fmt(why, "%s is not a substitution (the only one is %%chosen%%)", sub, NULL);
            return 0;
        }
    } else if (kind == EUP_INSTALLED && p[0] != ':' && strchr(p, ':')) {
        eup_fmt(why, "\"%s\" starts with a disk's name; everything goes inside %%chosen%%", s, NULL);
        return 0;
    }
    if (*p == ':') p++;
    if (!*p) {
        if (kind == EUP_SOURCE) { eup_fmt(why, "no file or folder named", NULL, NULL); return 0; }
        return 1;       /* %chosen% itself */
    }
    len = (int)strlen(p);
    if (p[len - 1] == ':') len--;           /* a folder's path may end in a colon */
    for (i = 0; i <= len; ) {
        int start = i;
        while (i < len && p[i] != ':') i++;
        if (n == 64) { eup_fmt(why, "more than 64 names deep", NULL, NULL); return 0; }
        if (!(names[n++] = name_of(c, p + start, i - start, why))) return 0;
        i++;
    }
    if (!(out->names = (PStr *)eup_alloc(c, sizeof(PStr) * n))) { eup_fmt(why, "out of memory", NULL, NULL); return 0; }
    memcpy(out->names, names, sizeof(PStr) * n);
    out->n = n;
    return 1;
}
