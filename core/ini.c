/* The INI file, read and checked (see eup.h and docs/ini.md). No stdio: it runs in the installer
   program on the Mac too, so it stays small. */
#include <stdlib.h>
#include <string.h>

#include "eup.h"
#include "private.h"

/* ---- memory: chunks, given back together by eup_free ---- */

typedef struct Chunk {
    struct Chunk *next;
    long size, used;
} Chunk;

void *eup_alloc(EupConfig *c, size_t n)
{
    Chunk *k = (Chunk *)c->arena;
    n = (n + 7) & ~(size_t)7;          /* room for a pointer on any system */
    if (!k || k->used + (long)n > k->size) {
        long size = n > 4000 ? (long)n : 4000;
        Chunk *m = (Chunk *)malloc(sizeof(Chunk) + size);
        if (!m) return NULL;
        m->next = k;
        m->size = size;
        m->used = 0;
        c->arena = k = m;
    }
    k->used += n;
    return (char *)(k + 1) + k->used - n;
}

PStr eup_pstr(EupConfig *c, const char *s, int n)
{
    PStr p;
    if (n > 255) n = 255;
    if (!(p = (PStr)eup_alloc(c, n + 2))) return NULL;
    p[0] = (unsigned char)n;
    memcpy(p + 1, s, n);
    p[n + 1] = 0;
    return p;
}

void eup_free(EupConfig *c)
{
    Chunk *k = (Chunk *)c->arena, *next;
    for (; k; k = next) { next = k->next; free(k); }
    free(c->types);
    free(c->groups);
    free(c->comps);
    free(c->remove);
    free(c->keep);
    free(c->places);
    memset(c, 0, sizeof *c);
}

/* Room for one more in an array that grows */
static void *grow(void *p, int n, int *cap, size_t size)
{
    void *q;
    if (n < *cap) return p;
    q = realloc(p, size * (*cap ? *cap * 2 : 8));
    if (q) *cap = *cap ? *cap * 2 : 8;
    return q;
}

/* ---- messages ---- */

void eup_fmt(char *out, const char *fmt, const char *a, const char *b)
{
    int n = 0;
    const char *arg = a;
    for (; *fmt && n < 199; fmt++) {
        if (fmt[0] == '%' && fmt[1] == '%') { out[n++] = '%'; fmt++; }
        else if (fmt[0] == '%' && fmt[1] == 's') {
            const char *s = arg ? arg : "";
            while (*s && n < 199) out[n++] = *s++;
            arg = b;
            fmt++;
        } else out[n++] = *fmt;
    }
    out[n] = 0;
}

static void err(EupConfig *c, int line, const char *fmt, const char *a, const char *b)
{
    if (c->nerrors == EUP_MAX_ERRORS) { c->more_errors = 1; return; }
    c->errors[c->nerrors].line = line;
    eup_fmt(c->errors[c->nerrors].text, fmt, a, b);
    c->nerrors++;
}

static void warn(EupConfig *c, int line, const char *fmt, const char *a, const char *b)
{
    if (c->nwarnings == EUP_MAX_WARNINGS) return;
    c->warnings[c->nwarnings].line = line;
    eup_fmt(c->warnings[c->nwarnings].text, fmt, a, b);
    c->nwarnings++;
}

static void why_err(EupConfig *c, int line, const char *key, const char *why)
{
    err(c, line, "%s: %s", key, why);
}

static void itoa_(char *out, long v)
{
    char t[16];
    int n = 0, k = 0;
    if (v < 0) { out[k++] = '-'; v = -v; }
    do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 15);
    while (n) out[k++] = t[--n];
    out[k] = 0;
}

/* ---- values ---- */

static int lower_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        if (x != *b) return 0;
    }
    return !*a && !*b;
}

/* yes or no: 1 or 0, -1 for neither */
static int yes_no(const char *v)
{
    if (lower_eq(v, "yes")) return 1;
    if (lower_eq(v, "no")) return 0;
    return -1;
}

/* A whole number; 0 if it isn't one */
static int number(const char *s, long *out)
{
    long v = 0;
    int neg = 0, any = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (s += 2; *s; s++, any = 1) {
            int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 :
                    (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
            if (d < 0 || v > 0xFFFFFFL) break;
            v = v * 16 + d;
        }
    } else
        for (; *s >= '0' && *s <= '9' && v < 100000000L; s++, any = 1) v = v * 10 + (*s - '0');
    while (*s == ' ' || *s == '\t') s++;
    if (!any || *s) return 0;
    *out = neg ? -v : v;
    return 1;
}

/* Numbers separated by commas: how many were read into out (at most max); -1 if one isn't a number */
static int numbers(char *s, long *out, int max)
{
    int n = 0;
    while (n < max) {
        char *e = strchr(s, ','), save = 0;
        if (e) { save = *e; *e = 0; }
        if (!number(s, &out[n])) { if (e) *e = save; return -1; }
        n++;
        if (!e) return n;
        *e = save;
        s = e + 1;
    }
    return -1;
}

static int ostype(const char *v, unsigned long *out)
{
    if (strlen(v) != 4) return 0;
    *out = ((unsigned long)(unsigned char)v[0] << 24) | ((unsigned long)(unsigned char)v[1] << 16) |
           ((unsigned long)(unsigned char)v[2] << 8) | (unsigned char)v[3];
    return 1;
}

static const struct { const char *name; unsigned int bit; } FLAGS[] = {
    { "alias", EUP_FL_ALIAS }, { "invisible", EUP_FL_INVISIBLE }, { "bundle", EUP_FL_BUNDLE },
    { "name_locked", EUP_FL_NAME_LOCKED }, { "stationery", EUP_FL_STATIONERY }, { "custom_icon", EUP_FL_CUSTOM_ICON },
    { "no_init", EUP_FL_NO_INIT }, { "shared", EUP_FL_SHARED }, { 0, 0 }
};

/* A number, or names separated by commas; 0 and why if neither */
static int finder_flags(char *v, unsigned int *out, char *why)
{
    long n;
    unsigned int f = 0;
    if (number(v, &n)) {
        if (n < 0 || n > 0xFFFF) { eup_fmt(why, "a number from 0 to 0xFFFF", NULL, NULL); return 0; }
        *out = (unsigned int)n;
        return 1;
    }
    while (*v) {
        char *e = strchr(v, ','), name[32];
        int k = e ? (int)(e - v) : (int)strlen(v), i;
        while (k && (v[k - 1] == ' ' || v[k - 1] == '\t')) k--;
        while (k && (*v == ' ' || *v == '\t')) { v++; k--; }
        if (k > 31) k = 31;
        memcpy(name, v, k);
        name[k] = 0;
        for (i = 0; FLAGS[i].name && !lower_eq(name, FLAGS[i].name); i++) ;
        if (!FLAGS[i].name) {
            eup_fmt(why, "\"%s\" is not a Finder flag (bundle, invisible, alias, stationery, custom_icon, name_locked, no_init, shared, or a number)", name, NULL);
            return 0;
        }
        f |= FLAGS[i].bit;
        if (!e) break;
        v = e + 1;
    }
    *out = f;
    return 1;
}

/* "2.0.1", "1.0", "1.1b2" into a 'vers' resource's first four bytes */
static int version(const char *v, unsigned char *vers)
{
    long major = 0, minor, fix = 0, rel = 0;
    int stage = 0x80;
    if (*v < '0' || *v > '9') return 0;
    while (*v >= '0' && *v <= '9') major = major * 10 + (*v++ - '0');
    if (major > 99 || *v++ != '.' || *v < '0' || *v > '9') return 0;
    minor = *v++ - '0';
    if (*v == '.') {
        v++;
        if (*v < '0' || *v > '9') return 0;
        fix = *v++ - '0';
    }
    if (*v == 'd' || *v == 'a' || *v == 'b') {
        stage = *v == 'd' ? 0x20 : *v == 'a' ? 0x40 : 0x60;
        v++;
        if (*v < '0' || *v > '9') return 0;
        while (*v >= '0' && *v <= '9') rel = rel * 10 + (*v++ - '0');
        if (rel > 99) return 0;
    }
    if (*v) return 0;
    vers[0] = (unsigned char)(major / 10 * 16 + major % 10);
    vers[1] = (unsigned char)(minor * 16 + fix);
    vers[2] = (unsigned char)stage;
    vers[3] = (unsigned char)(rel / 10 * 16 + rel % 10);
    return 1;
}

/* ---- the sections and their keys ---- */

enum { SEC_NONE, SEC_INSTALLER, SEC_TYPE, SEC_COMPONENT, SEC_UPGRADE, SEC_LAYOUT, SEC_GROUP };

static const char *const KEYS[][14] = {
    { 0 },
    { "product", "version", "version_string", "title", "file", "output", "folder", "get_info", "creator", "icon", "description_screen",
      "license_screen", 0 },
    { "label", "description", "description_file", "custom", "remove_old", 0 },
    { "label", "group", "description", "source", "destination", "types", "checked", "type", "creator", "flags", "as",
      "text", "compress", 0 },
    { "remove", "keep", "detect", "require", 0 },
    { "window", "place", 0 },
    { "label", "selectable", 0 }
};

/* Keys that may be given more than once, each adding one */
static int repeats(int sec, const char *key)
{
    return (sec == SEC_UPGRADE && (!strcmp(key, "remove") || !strcmp(key, "keep"))) ||
           (sec == SEC_LAYOUT && !strcmp(key, "place"));
}

typedef struct {
    int sec, line;
    unsigned long seen;             /* the keys given in this section */
    int seen_installer, seen_upgrade, seen_layout, installer_line, require_line;
    /* a component's types, read when every type is known */
    char **types_text;
    int *types_line, *checked_given, cap;
    char **group_text;
    int *group_line, gcap;
} Parse;

static int valid_id(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_')) return 0;
    return 1;
}

static void section(EupConfig *c, Parse *ps, char *s, int line)
{
    char *id = s;
    int i;
    for (i = 0; s[i]; i++) if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32;
    while (*id && *id != ' ' && *id != '\t') id++;
    if (*id) { *id++ = 0; while (*id == ' ' || *id == '\t') id++; }
    ps->seen = 0;
    ps->line = line;
    ps->sec = SEC_NONE;
    if (!strcmp(s, "installer") || !strcmp(s, "upgrade") || !strcmp(s, "layout")) {
        int *seen = s[0] == 'i' ? &ps->seen_installer : s[0] == 'u' ? &ps->seen_upgrade : &ps->seen_layout;
        if (*id) { err(c, line, "[%s] has no name after it", s, NULL); return; }
        if (*seen) { err(c, line, "A second [%s] section", s, NULL); return; }
        *seen = 1;
        if (s[0] == 'i') ps->installer_line = line;
        ps->sec = s[0] == 'i' ? SEC_INSTALLER : s[0] == 'u' ? SEC_UPGRADE : SEC_LAYOUT;
        return;
    }
    if (strcmp(s, "type") && strcmp(s, "component") && strcmp(s, "group")) {
        err(c, line, "[%s] is not a section (they are installer, type, group, component, upgrade, and layout)", s, NULL);
        return;
    }
    if (!valid_id(id)) {
        err(c, line, "[%s %s]: a name of letters, digits, - and _ goes after it", s, id);
        return;
    }
    if (s[0] == 'g') {
        EupGroup *g;
        for (i = 0; i < c->ngroups; i++)
            if (!strcmp(EUP_C(c->groups[i].id), id)) { err(c, line, "A second [group %s]", id, NULL); return; }
        if (!(g = (EupGroup *)grow(c->groups, c->ngroups, &c->cap_groups, sizeof *g))) return;
        c->groups = g;
        g = &c->groups[c->ngroups++];
        memset(g, 0, sizeof *g);
        g->line = line;
        g->id = eup_pstr(c, id, (int)strlen(id));
        ps->sec = SEC_GROUP;
    } else if (s[0] == 't') {
        EupType *t;
        for (i = 0; i < c->ntypes; i++)
            if (!strcmp(EUP_C(c->types[i].id), id)) { err(c, line, "A second [type %s]", id, NULL); return; }
        if (!(t = (EupType *)grow(c->types, c->ntypes, &c->cap_types, sizeof *t))) return;
        c->types = t;
        t = &c->types[c->ntypes++];
        memset(t, 0, sizeof *t);
        t->line = line;
        t->id = eup_pstr(c, id, (int)strlen(id));
        t->remove_old = 1;
        ps->sec = SEC_TYPE;
    } else {
        EupComp *k;
        for (i = 0; i < c->ncomps; i++)
            if (!strcmp(EUP_C(c->comps[i].id), id)) { err(c, line, "A second [component %s]", id, NULL); return; }
        if (!(k = (EupComp *)grow(c->comps, c->ncomps, &c->cap_comps, sizeof *k))) return;
        c->comps = k;
        if (c->ncomps >= ps->cap) {
            int cap = ps->cap ? ps->cap * 2 : 8;
            char **a = (char **)realloc(ps->types_text, sizeof(char *) * cap);
            int *b, *d;
            if (a) ps->types_text = a;
            b = (int *)realloc(ps->types_line, sizeof(int) * cap);
            if (b) ps->types_line = b;
            d = (int *)realloc(ps->checked_given, sizeof(int) * cap);
            if (d) ps->checked_given = d;
            if (!a || !b || !d) return;
            a = (char **)realloc(ps->group_text, sizeof(char *) * cap);
            if (a) ps->group_text = a;
            b = (int *)realloc(ps->group_line, sizeof(int) * cap);
            if (b) ps->group_line = b;
            if (!a || !b) return;
            ps->cap = cap;
        }
        k = &c->comps[c->ncomps];
        memset(k, 0, sizeof *k);
        ps->types_text[c->ncomps] = NULL;
        ps->types_line[c->ncomps] = 0;
        ps->checked_given[c->ncomps] = 0;
        ps->group_text[c->ncomps] = NULL;
        c->ncomps++;
        k->line = line;
        k->id = eup_pstr(c, id, (int)strlen(id));
        k->description = eup_pstr(c, "", 0);
        k->group = -1;
        k->compress = 1;
        ps->sec = SEC_COMPONENT;
    }
}

/* A line of text: at most max characters */
static PStr text(EupConfig *c, int line, const char *key, const char *v, int max)
{
    char n[8];
    if ((int)strlen(v) > max) {
        itoa_(n, max);
        err(c, line, "%s: longer than %s characters", key, n);
        return NULL;
    }
    return eup_pstr(c, v, (int)strlen(v));
}

static void path_key(EupConfig *c, int line, const char *key, const char *v, int kind, EupPath *out)
{
    char why[200];
    if (!eup_split_path(c, v, kind, out, why)) why_err(c, line, key, why);
}

static void yes_no_key(EupConfig *c, int line, const char *key, const char *v, int *out)
{
    int y = yes_no(v);
    if (y < 0) err(c, line, "%s: yes or no", key, NULL);
    else *out = y;
}

static void key_installer(EupConfig *c, int line, const char *k, char *v)
{
    char why[200];
    if (!strcmp(k, "product")) c->product = text(c, line, k, v, 63);
    else if (!strcmp(k, "version")) {
        if (!version(v, c->vers)) err(c, line, "version: a number like 1.0, 2.0.1, or 1.1b2", NULL, NULL);
        else c->version = text(c, line, k, v, 15);
    } else if (!strcmp(k, "version_string")) c->version_string = text(c, line, k, v, 31);
    else if (!strcmp(k, "title")) c->title = text(c, line, k, v, 63);
    else if (!strcmp(k, "file")) { if (!(c->file = eup_name(c, v, why))) why_err(c, line, k, why); }
    else if (!strcmp(k, "output")) {
        if (strchr(v, '/') || strchr(v, ':') || strchr(v, '\\')) err(c, line, "output: a file name, without a folder", NULL, NULL);
        else c->output = text(c, line, k, v, 200);
    } else if (!strcmp(k, "folder")) { if (!(c->folder = eup_name(c, v, why))) why_err(c, line, k, why); }
    else if (!strcmp(k, "get_info")) c->get_info = text(c, line, k, v, 255);
    else if (!strcmp(k, "creator")) {
        if (!ostype(v, &c->creator)) err(c, line, "creator: four characters", NULL, NULL);
        else c->has_creator = 1;
    } else if (!strcmp(k, "icon")) path_key(c, line, k, v, EUP_SOURCE, &c->icon);
    else if (!strcmp(k, "description_screen")) path_key(c, line, k, v, EUP_SOURCE, &c->description_screen);
    else if (!strcmp(k, "license_screen")) path_key(c, line, k, v, EUP_SOURCE, &c->license_screen);
}

static void key_type(EupConfig *c, int line, const char *k, char *v)
{
    EupType *t = &c->types[c->ntypes - 1];
    if (!strcmp(k, "label")) t->label = text(c, line, k, v, 63);
    else if (!strcmp(k, "description")) t->description = text(c, line, k, v, 255);
    else if (!strcmp(k, "description_file")) path_key(c, line, k, v, EUP_SOURCE, &t->description_file);
    else if (!strcmp(k, "custom")) yes_no_key(c, line, k, v, &t->custom);
    else if (!strcmp(k, "remove_old")) yes_no_key(c, line, k, v, &t->remove_old);
}

static void key_component(EupConfig *c, Parse *ps, int line, const char *k, char *v)
{
    EupComp *m = &c->comps[c->ncomps - 1];
    char why[200];
    if (!strcmp(k, "label")) m->label = text(c, line, k, v, 63);
    else if (!strcmp(k, "group")) {
        PStr g = eup_pstr(c, v, (int)strlen(v));
        ps->group_text[c->ncomps - 1] = g ? EUP_C(g) : NULL;
        ps->group_line[c->ncomps - 1] = line;
    }
    else if (!strcmp(k, "description")) { PStr d = text(c, line, k, v, 255); if (d) m->description = d; }
    else if (!strcmp(k, "source")) path_key(c, line, k, v, EUP_SOURCE, &m->source);
    else if (!strcmp(k, "destination")) path_key(c, line, k, v, EUP_INSTALLED, &m->destination);
    else if (!strcmp(k, "types")) {
        PStr t = eup_pstr(c, v, (int)strlen(v));
        ps->types_text[c->ncomps - 1] = t ? EUP_C(t) : NULL;
        ps->types_line[c->ncomps - 1] = line;
    } else if (!strcmp(k, "checked")) {
        yes_no_key(c, line, k, v, &m->checked);
        ps->checked_given[c->ncomps - 1] = 1;
    } else if (!strcmp(k, "type")) {
        if (!ostype(v, &m->type)) err(c, line, "type: four characters", NULL, NULL);
        else m->has_type = 1;
    } else if (!strcmp(k, "creator")) {
        if (!ostype(v, &m->creator)) err(c, line, "creator: four characters", NULL, NULL);
        else m->has_creator = 1;
    } else if (!strcmp(k, "flags")) {
        if (!finder_flags(v, &m->flags, why)) why_err(c, line, k, why);
        else m->has_flags = 1;
    } else if (!strcmp(k, "as")) { if (!(m->as = eup_name(c, v, why))) why_err(c, line, k, why); }
    else if (!strcmp(k, "text")) yes_no_key(c, line, k, v, &m->text);
    else if (!strcmp(k, "compress")) yes_no_key(c, line, k, v, &m->compress);
}

static void key_group(EupConfig *c, int line, const char *k, char *v)
{
    EupGroup *g = &c->groups[c->ngroups - 1];
    if (!strcmp(k, "label")) g->label = text(c, line, k, v, 63);
    else yes_no_key(c, line, k, v, &g->selectable);
}

static void key_upgrade(EupConfig *c, int line, const char *k, char *v)
{
    char why[200];
    if (!strcmp(k, "detect")) { if (!(c->detect = eup_name(c, v, why))) why_err(c, line, k, why); }
    else if (!strcmp(k, "require")) c->require = text(c, line, k, v, 63);
    else {
        int remove = !strcmp(k, "remove"), i;
        EupPath p, *a;
        if (!eup_split_path(c, v, EUP_INSTALLED, &p, why)) { why_err(c, line, k, why); return; }
        if (!p.n) { err(c, line, "%s: a name inside %%chosen%%, not %%chosen%% itself", k, NULL); return; }
        /* an installer never removes what it keeps */
        for (i = 0; i < (remove ? c->nkeep : c->nremove); i++) {
            const EupPath *r = remove ? &p : &c->remove[i], *kp = remove ? &c->keep[i] : &p;
            if (eup_path_in(r, kp)) {
                char t[200];
                int j, n = 0;
                for (j = 0; j < r->n; j++) {        /* the removed path, as names */
                    int k = r->names[j][0];
                    if (n + k + 2 > (int)sizeof t) break;
                    if (j) t[n++] = ':';
                    memcpy(t + n, r->names[j] + 1, k);
                    n += k;
                }
                t[n] = 0;
                warn(c, line, "\"%s\" is removed and kept: it's kept", t, NULL);
                break;
            }
        }
        if (remove) {
            if (!(a = (EupPath *)grow(c->remove, c->nremove, &c->cap_remove, sizeof *a))) return;
            c->remove = a;
            c->remove[c->nremove++] = p;
        } else {
            if (!(a = (EupPath *)grow(c->keep, c->nkeep, &c->cap_keep, sizeof *a))) return;
            c->keep = a;
            c->keep[c->nkeep++] = p;
        }
    }
}

static void key_layout(EupConfig *c, int line, const char *k, char *v)
{
    long n[4];
    int i;
    if (!strcmp(k, "window")) {
        if (numbers(v, n, 4) != 4 || n[0] < 0 || n[1] < 0 || n[2] < 60 || n[3] < 60 || n[0] + n[2] > 4000 || n[1] + n[3] > 4000) {
            err(c, line, "window: left, top, width, and height, in pixels (at least 60 wide and high)", NULL, NULL);
            return;
        }
        for (i = 0; i < 4; i++) c->window[i] = (int)n[i];
        c->has_window = 1;
    } else {
        char *colon = strchr(v, ':'), why[200];
        EupPlace *p;
        PStr name;
        if (!colon) { err(c, line, "place: a name, a colon, and the icon's place across and down (Mill: 18, 9)", NULL, NULL); return; }
        *colon = 0;
        {
            int k2 = (int)strlen(v);
            while (k2 && (v[k2 - 1] == ' ' || v[k2 - 1] == '\t')) v[--k2] = 0;
        }
        if (!(name = eup_name(c, v, why))) { why_err(c, line, k, why); return; }
        if (numbers(colon + 1, n, 2) != 2 || n[0] < -2000 || n[0] > 4000 || n[1] < -2000 || n[1] > 4000) {
            err(c, line, "place: after the name, the icon's place across and down, in pixels", NULL, NULL);
            return;
        }
        for (i = 0; i < c->nplaces; i++)
            if (eup_same_name(c->places[i].name, name)) { err(c, line, "place: \"%s\" is placed twice", EUP_C(name), NULL); return; }
        if (!(p = (EupPlace *)grow(c->places, c->nplaces, &c->cap_places, sizeof *p))) return;
        c->places = p;
        p = &c->places[c->nplaces++];
        p->line = line;
        p->name = name;
        p->h = (int)n[0];
        p->v = (int)n[1];
    }
}

static void key(EupConfig *c, Parse *ps, char *k, char *v, int line)
{
    int i;
    for (i = 0; k[i]; i++) if (k[i] >= 'A' && k[i] <= 'Z') k[i] += 32;
    if (ps->sec == SEC_NONE) { if (!c->nerrors || c->errors[c->nerrors - 1].line != ps->line || !ps->line) err(c, line, "%s: not in a section", k, NULL); return; }
    for (i = 0; KEYS[ps->sec][i] && strcmp(KEYS[ps->sec][i], k); i++) ;
    if (!KEYS[ps->sec][i]) {
        static const char *const NAME[] = { "", "[installer]", "[type]", "[component]", "[upgrade]", "[layout]", "[group]" };
        err(c, line, "%s is not a key of %s", k, NAME[ps->sec]);
        return;
    }
    if (!repeats(ps->sec, k)) {
        if (ps->seen & (1UL << i)) { err(c, line, "%s: given twice", k, NULL); return; }
        ps->seen |= 1UL << i;
    }
    if (!*v) return;            /* empty: as if not given */
    switch (ps->sec) {
    case SEC_INSTALLER: key_installer(c, line, k, v); break;
    case SEC_TYPE: key_type(c, line, k, v); break;
    case SEC_COMPONENT: key_component(c, ps, line, k, v); break;
    case SEC_UPGRADE:
        if (!strcmp(k, "require")) ps->require_line = line;
        key_upgrade(c, line, k, v);
        break;
    case SEC_LAYOUT: key_layout(c, line, k, v); break;
    case SEC_GROUP: key_group(c, line, k, v); break;
    }
}

/* ---- after reading: defaults, and the rules across sections ---- */

static PStr joined(EupConfig *c, const char *a, const char *b, const char *d, const char *e)
{
    char buf[256];
    int n = 0;
    const char *parts[4];
    int i;
    parts[0] = a; parts[1] = b; parts[2] = d; parts[3] = e;
    for (i = 0; i < 4; i++) {
        const char *s = parts[i];
        if (!s) continue;
        while (*s && n < 255) buf[n++] = *s++;
    }
    return eup_pstr(c, buf, n);
}

static void resolve_types(EupConfig *c, Parse *ps)
{
    int i, j;
    for (i = 0; i < c->ncomps; i++) {
        EupComp *m = &c->comps[i];
        char *s = ps->types_text[i];
        int line = ps->types_line[i];
        if (!s) continue;
        if (!(m->types = (int *)eup_alloc(c, sizeof(int) * (c->ntypes + 1)))) return;
        while (*s) {
            char *e = strchr(s, ','), id[64];
            int k = e ? (int)(e - s) : (int)strlen(s), t;
            while (k && (s[k - 1] == ' ' || s[k - 1] == '\t')) k--;
            while (k && (*s == ' ' || *s == '\t')) { s++; k--; }
            if (k > 63) k = 63;
            for (t = 0; t < k; t++) id[t] = (s[t] >= 'A' && s[t] <= 'Z') ? (char)(s[t] + 32) : s[t];
            id[k] = 0;
            for (t = 0; t < c->ntypes && strcmp(EUP_C(c->types[t].id), id); t++) ;
            if (!k) err(c, line, "types: an empty name between commas", NULL, NULL);
            else if (t == c->ntypes) err(c, line, "types: there is no [type %s]", id, NULL);
            else {
                for (j = 0; j < m->ntypes && m->types[j] != t; j++) ;
                if (j < m->ntypes) err(c, line, "types: %s is named twice", id, NULL);
                else m->types[m->ntypes++] = t;
            }
            if (!e) break;
            s = e + 1;
        }
    }
}

static int in_type(const EupComp *m, int t)
{
    int j;
    for (j = 0; j < m->ntypes; j++) if (m->types[j] == t) return 1;
    return 0;
}

static void finish(EupConfig *c, Parse *ps)
{
    char why[200];
    int i, j, t;
    if (!ps->seen_installer) err(c, 0, "No [installer] section", NULL, NULL);
    else if (!c->product) err(c, ps->installer_line, "[installer]: product is needed (the software's name)", NULL, NULL);
    if (c->version_string && !c->version)
        err(c, ps->installer_line, "[installer]: version_string needs version, a number like 1.0", NULL, NULL);
    if (!c->version_string) c->version_string = c->version;
    if (c->product && !c->title)            /* "Install Mill 2.0.1" */
        c->title = joined(c, "Install ", EUP_C(c->product), c->version_string ? " " : NULL,
                          c->version_string ? EUP_C(c->version_string) : NULL);
    if (c->title && !c->file) {
        if (!(c->file = eup_name(c, EUP_C(c->title), why)))
            err(c, ps->installer_line, "[installer]: the title can't be the installer's file name (%s); give file", why, NULL);
    }
    if (c->file && !c->output) {
        char buf[64];
        int n = c->file[0];
        for (i = 0; i < n; i++) {
            unsigned char ch = c->file[1 + i];
            buf[i] = (ch == ' ' || ch == '/' || ch == '\\') ? '-' : (char)ch;
        }
        c->output = eup_pstr(c, buf, n);
    }
    if (c->product && !c->folder && !(c->folder = eup_name(c, EUP_C(c->product), why)))
        err(c, ps->installer_line, "[installer]: the product's name can't be a folder name (%s); give folder", why, NULL);
    if (c->title && !c->get_info) c->get_info = c->title;
    if (c->require && !c->detect)
        err(c, ps->require_line, "[upgrade]: require needs detect, the file that marks an install of %s", EUP_C(c->require), NULL);
    if (c->icon.n && !c->has_creator) err(c, ps->installer_line, "[installer]: an icon needs a creator of its own", NULL, NULL);
    for (t = 0; t < c->ntypes; t++) {
        EupType *ty = &c->types[t];
        if (!ty->label) err(c, ty->line, "[type %s]: label is needed", EUP_C(ty->id), NULL);
        if (ty->description && ty->description_file.n)
            err(c, ty->line, "[type %s]: description or description_file, not both", EUP_C(ty->id), NULL);
    }
    if (!c->ntypes) err(c, 0, "No [type] sections: at least one install type is needed", NULL, NULL);
    if (!c->ncomps) err(c, 0, "No [component] sections: at least one is needed", NULL, NULL);
    resolve_types(c, ps);
    for (i = 0; i < c->ngroups; i++)
        if (!c->groups[i].label) err(c, c->groups[i].line, "[group %s]: label is needed", EUP_C(c->groups[i].id), NULL);
    for (i = 0; i < c->ncomps; i++) {
        EupComp *m = &c->comps[i];
        char *g = ps->group_text[i];
        if (g) {
            for (t = 0; g[t]; t++) if (g[t] >= 'A' && g[t] <= 'Z') g[t] += 32;
            for (j = 0; j < c->ngroups && strcmp(EUP_C(c->groups[j].id), g); j++) ;
            if (j == c->ngroups) err(c, ps->group_line[i], "group: there is no [group %s]", g, NULL);
            else m->group = j;
        }
        if (!m->label) err(c, m->line, "[component %s]: label is needed", EUP_C(m->id), NULL);
        if (!m->source.n) err(c, m->line, "[component %s]: source is needed", EUP_C(m->id), NULL);
        if (!ps->types_text[i]) err(c, m->line, "[component %s]: types is needed (the install types it's in)", EUP_C(m->id), NULL);
        if (!ps->checked_given[i])          /* checked at first if a type without the list installs it */
            for (j = 0; j < m->ntypes; j++) if (!c->types[m->types[j]].custom) m->checked = 1;
    }
    for (t = 0; t < c->ntypes; t++) {
        EupType *ty = &c->types[t];
        int n = 0, last = -1;
        for (i = 0; i < c->ncomps; i++) {
            if (!in_type(&c->comps[i], t)) continue;
            n++;
            /* a custom list's groups: each one's components together */
            if (ty->custom && last >= 0 && c->comps[last].group != c->comps[i].group && c->comps[i].group >= 0)
                for (j = 0; j < last; j++)
                    if (in_type(&c->comps[j], t) && c->comps[j].group == c->comps[i].group) {
                        err(c, c->comps[i].line, "[component %s]: group %s is split by another; keep a group's components together",
                            EUP_C(c->comps[i].id), EUP_C(c->groups[c->comps[i].group].id));
                        break;
                    }
            last = i;
        }
        if (!n && c->ncomps) err(c, ty->line, "[type %s]: no component is in it", EUP_C(ty->id), NULL);
    }
}

/* ---- reading ---- */

int eup_parse(EupConfig *c, const char *in, long len)
{
    Parse ps;
    char *buf, *p, *end;
    int line = 0;
    memset(c, 0, sizeof *c);
    memset(&ps, 0, sizeof ps);
    if (!(buf = (char *)malloc(len + 1))) return -1;
    memcpy(buf, in, len);
    buf[len] = 0;
    for (p = buf, end = buf + len; p < end; ) {
        char *e = p, *s, *k;
        int n;
        while (e < end && *e != '\r' && *e != '\n') e++;
        line++;
        *e = 0;
        s = p;
        p = e + 1;
        if (e + 1 < end && e[0] == 0 && in[e - buf] == '\r' && e[1] == '\n') p++;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == ';' || *s == '#') continue;
        if (memchr(s, 0, e - s)) { err(c, line, "A NUL character", NULL, NULL); continue; }
        /* a comment after a value: a ; after a space or tab */
        for (k = s + 1; *k; k++) if (*k == ';' && (k[-1] == ' ' || k[-1] == '\t')) { *k = 0; break; }
        n = (int)strlen(s);
        while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
        if (*s == '[') {
            if (s[n - 1] != ']') { err(c, line, "A section's name ends with ]", NULL, NULL); ps.sec = SEC_NONE; ps.line = line; continue; }
            s[n - 1] = 0;
            s++;
            while (*s == ' ' || *s == '\t') s++;
            n = (int)strlen(s);
            while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
            section(c, &ps, s, line);
            continue;
        }
        if (!(k = strchr(s, '='))) { err(c, line, "Not a key = value, a [section], or a comment", NULL, NULL); continue; }
        *k = 0;
        {
            char *v = k + 1;
            int kn = (int)strlen(s);
            while (kn && (s[kn - 1] == ' ' || s[kn - 1] == '\t')) s[--kn] = 0;
            while (*v == ' ' || *v == '\t') v++;
            if (!kn) { err(c, line, "A value without a key", NULL, NULL); continue; }
            key(c, &ps, s, v, line);
        }
    }
    finish(c, &ps);
    free(ps.types_text);
    free(ps.types_line);
    free(ps.checked_given);
    free(ps.group_text);
    free(ps.group_line);
    free(buf);
    return c->nerrors;
}
