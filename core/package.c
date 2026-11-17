/* Packaging: see package.h */
#include <stdlib.h>
#include <string.h>

#include "package.h"
#include "private.h"

#define PIECE 4096L
#define OSTYPE(a, b, c, d) (((unsigned long)(a) << 24) | ((unsigned long)(b) << 16) | ((unsigned long)(c) << 8) | (unsigned long)(d))
#define TEXT_MAX 32000L
#define NAMES (32L * 32 * 8)          /* a record's source names */             /* TextEdit's limit, near enough */

typedef struct {
    int kind;
    PStr *names;                    /* the source path */
    int n;
    unsigned char name[32];         /* the installed name */
    int macbinary;
    EupMacInfo mb;
    EupStat st;
    int comp;                       /* the component that brought it in */
    int convert;                    /* UTF-8 text to Mac Roman, LF to CR */
    int compress;
    unsigned long type, creator;
    unsigned int flags;
} Item;

typedef struct {
    EupPath path;
    int line;
} Text;

struct EupPackage {
    EupPackIn in;
    EupConfig *c;
    Item *items;
    int nitems, cap;
    Text texts[64];
    int ntexts;
    unsigned long total;            /* bytes to read */
};

static void perr(EupConfig *c, int line, const char *fmt, const char *a, const char *b)
{
    if (c->nerrors == EUP_MAX_ERRORS) { c->more_errors = 1; return; }
    c->errors[c->nerrors].line = line;
    eup_fmt(c->errors[c->nerrors].text, fmt, a, b);
    c->nerrors++;
}

/* A path as the INI would write it, for messages */
static void path_text(PStr *names, int n, char *out, int room)
{
    int k = 0, i, j;
    for (i = 0; i < n && k < room - 1; i++) {
        if (i && k < room - 1) out[k++] = ':';
        for (j = 1; j <= names[i][0] && k < room - 1; j++) out[k++] = (char)names[i][j];
    }
    out[k] = 0;
}

/* ---- reading a file: its forks, wherever they are ---- */

typedef struct {
    EupFS *fs;
    void *file;
    unsigned long base;
} At;

static int read_at(void *ref, unsigned long at, void *buf, long n)
{
    At *a = (At *)ref;
    return a->fs->read_at(a->file, a->base + at, buf, n);
}

/* Whether the file is MacBinary: a data fork alone, with a MacBinary header */
static int sniff(EupFS *fs, PStr *names, int n, const EupStat *st, EupMacInfo *mb)
{
    unsigned char head[128];
    void *f;
    int ok = 0;
    if (st->rlen || st->dlen < 128 || !(f = fs->open(fs->ref, names, n, 0))) return 0;
    if (!fs->read_at(f, 0, head, 128)) ok = eup_macbinary_read(head, st->dlen, mb);
    fs->close(f);
    return ok;
}

/* ---- checking ---- */

static Item *new_item(EupPackage *p)
{
    if (p->nitems == p->cap) {
        int cap = p->cap ? p->cap * 2 : 32;
        Item *a = (Item *)realloc(p->items, sizeof(Item) * cap);
        if (!a) return NULL;
        p->items = a;
        p->cap = cap;
    }
    memset(&p->items[p->nitems], 0, sizeof(Item));
    return &p->items[p->nitems++];
}

static PStr *names_copy(EupConfig *c, PStr *names, int n, const unsigned char *more)
{
    PStr *a = (PStr *)eup_alloc(c, sizeof(PStr) * (n + 1));
    if (!a) return NULL;
    memcpy(a, names, sizeof(PStr) * n);
    if (more && !(a[n] = eup_pstr(c, (const char *)more + 1, more[0]))) return NULL;
    return a;
}

/* One file or folder of a component: k is the component, in_folder whether it's inside its source folder */
static int add(EupPackage *p, int k, PStr *names, int n, const EupStat *st, int in_folder)
{
    EupConfig *c = p->c;
    EupComp *m = &c->comps[k];
    Item *it;
    char where[200];
    int i;
    path_text(names, n, where, 120);
    /* the same file twice (in two components) is packaged once, if it's packaged the same way */
    for (i = 0; i < p->nitems; i++) {
        Item *o = &p->items[i];
        EupPath a, b;
        a.n = o->n; a.names = o->names; b.n = n; b.names = names;
        if (eup_same_path(&a, &b)) {
            EupComp *om = &c->comps[o->comp];
            if (o->kind == EUP_FILE && (om->text != m->text || om->compress != m->compress || om->has_type != m->has_type ||
                om->type != m->type || om->has_creator != m->has_creator || om->creator != m->creator || om->has_flags != m->has_flags ||
                om->flags != m->flags || (om->as != NULL) != (m->as != NULL) || (m->as && !eup_same_name(om->as, m->as))))
                perr(c, m->line, "[component %s]: \"%s\" is in another component with other settings", EUP_C(m->id), where);
            return 0;
        }
    }
    if (!(it = new_item(p))) return -1;
    it->kind = st->kind;
    it->n = n;
    if (!(it->names = names_copy(c, names, n, NULL))) return -1;
    it->st = *st;
    it->comp = k;
    it->compress = m->compress;
    memcpy(it->name, names[n - 1], names[n - 1][0] + 1);
    if (st->kind == EUP_FOLDER) return 0;
    if (sniff(p->in.fs, names, n, st, &it->mb)) {
        it->macbinary = 1;
        if (it->mb.name[0] > 31 || memchr(it->mb.name + 1, ':', it->mb.name[0])) {
            char nm[64];
            memcpy(nm, it->mb.name + 1, it->mb.name[0]);
            nm[it->mb.name[0]] = 0;
            perr(c, m->line, "\"%s\": its MacBinary name, \"%s\", is not a Mac file name", where, nm);
        } else memcpy(it->name, it->mb.name, it->mb.name[0] + 1);
        it->type = it->mb.type;
        it->creator = it->mb.creator;
        it->flags = it->mb.flags;
    } else if (st->has_info) {
        it->type = st->type;
        it->creator = st->creator;
        it->flags = st->flags;
    } else {
        it->type = m->text ? OSTYPE('T', 'E', 'X', 'T') : OSTYPE('?', '?', '?', '?');
        it->creator = m->text ? OSTYPE('t', 't', 'x', 't') : OSTYPE('?', '?', '?', '?');
        it->convert = m->text && p->in.fs->utf8_text;
    }
    /* the INI's settings: for the file itself, or, in a folder, for files that have no type of their own */
    if (!in_folder || (!it->macbinary && !st->has_info)) {
        if (m->has_type) it->type = m->type;
        if (m->has_creator) it->creator = m->creator;
    }
    if (!in_folder) {
        if (m->has_flags) it->flags = m->flags;
        if (m->as) memcpy(it->name, m->as, m->as[0] + 1);
    }
    p->total += st->dlen + st->rlen;
    if (p->nitems > EUP_MAX_FILES) { perr(c, m->line, "More than 2,000 files", NULL, NULL); return -1; }
    return 0;
}

/* A source folder's contents, and its folders' (buffers from the heap: a Mac Plus has a small stack) */
static int walk(EupPackage *p, int k, PStr *names, int n, int depth)
{
    EupConfig *c = p->c;
    EupFS *fs = p->in.fs;
    unsigned char *name = (unsigned char *)malloc(256 + 200 + 200);
    char *why = (char *)name + 256, *where = why + 200;
    int i, r, err = 0;
    if (!name) return -1;
    if (depth > 30) {
        path_text(names, n, where, 120);
        perr(c, c->comps[k].line, "[component %s]: \"%s\" is more than 30 folders deep", EUP_C(c->comps[k].id), where);
        free(name);
        return 0;
    }
    for (i = 0; !err && (r = fs->entry(fs->ref, names, n, i, name, why)) != 1; i++) {
        PStr *sub;
        EupStat st;
        if (r < 0) {
            path_text(names, n, where, 120);
            perr(c, c->comps[k].line, "In \"%s\": %s", where, why);
            continue;
        }
        if (name[0] && name[1] == '.') continue;        /* hidden */
        if (name[0] > 31) {
            name[name[0] + 1 > 255 ? 255 : name[0] + 1] = 0;
            path_text(names, n, where, 120);
            perr(c, c->comps[k].line, "In \"%s\": \"%s\" is longer than 31 characters", where, (char *)name + 1);
            continue;
        }
        if (!(sub = names_copy(c, names, n, name))) { err = -1; break; }
        if (fs->stat(fs->ref, sub, n + 1, &st) || st.kind == EUP_NOTHING) continue;
        if (add(p, k, sub, n + 1, &st, 1)) err = -1;
        else if (st.kind == EUP_FOLDER && walk(p, k, sub, n + 1, depth + 1)) err = -1;
    }
    free(name);
    return err;
}

static void add_text(EupPackage *p, const EupPath *path, int line, const char *key)
{
    EupStat st;
    char where[200];
    int i;
    if (!path->n) return;
    for (i = 0; i < p->ntexts; i++) if (eup_same_path(&p->texts[i].path, path)) return;
    path_text(path->names, path->n, where, 120);
    if (p->in.fs->stat(p->in.fs->ref, path->names, path->n, &st) || st.kind != EUP_FILE)
        perr(p->c, line, "%s: no file \"%s\"", key, where);
    else if (st.dlen > TEXT_MAX) perr(p->c, line, "%s: \"%s\" is longer than 32000 characters", key, where);
    else if (p->ntexts < 64) {
        p->texts[p->ntexts].path = *path;
        p->texts[p->ntexts++].line = line;
    }
}

/* Where an item goes, inside %chosen%, for finding two in the same place */
static int installed(EupPackage *p, const Item *it, int k, PStr *out)
{
    const EupComp *m = &p->c->comps[k];
    int n = 0, i;
    for (i = 0; i < m->destination.n; i++) out[n++] = m->destination.names[i];
    for (i = m->source.n; i < it->n - 1; i++) out[n++] = it->names[i];
    out[n++] = (PStr)it->name;
    return n;
}

int eup_pack_check(const EupPackIn *in, EupPackage **pack)
{
    EupConfig *c = in->config;
    EupPackage *p;
    int k, i, j;
    *pack = NULL;
    if (!(p = (EupPackage *)calloc(1, sizeof *p))) { perr(c, 0, "Out of memory", NULL, NULL); return c->nerrors; }
    p->in = *in;
    p->c = c;
    for (k = 0; k < c->ncomps; k++) {
        EupComp *m = &c->comps[k];
        EupStat st;
        char where[200];
        if (!m->source.n) continue;
        path_text(m->source.names, m->source.n, where, 120);
        if (in->fs->stat(in->fs->ref, m->source.names, m->source.n, &st) || st.kind == EUP_NOTHING) {
            perr(c, m->line, "[component %s]: source: no file or folder \"%s\"", EUP_C(m->id), where);
            continue;
        }
        if (st.kind == EUP_FOLDER) {
            if (m->as) perr(c, m->line, "[component %s]: as is for a file; \"%s\" is a folder", EUP_C(m->id), where);
            if (m->has_flags) perr(c, m->line, "[component %s]: flags is for a file; \"%s\" is a folder", EUP_C(m->id), where);
            if (walk(p, k, m->source.names, m->source.n, 0)) break;
        } else if (add(p, k, m->source.names, m->source.n, &st, 0)) break;
    }
    add_text(p, &c->description_screen, 0, "description_screen");
    add_text(p, &c->license_screen, 0, "license_screen");
    for (i = 0; i < c->ntypes; i++) add_text(p, &c->types[i].description_file, c->types[i].line, "description_file");
    if (c->icon.n) {
        EupStat st;
        char where[200];
        path_text(c->icon.names, c->icon.n, where, 120);
        if (in->fs->stat(in->fs->ref, c->icon.names, c->icon.n, &st) || st.kind != EUP_FILE)
            perr(c, 0, "icon: no file \"%s\"", where, NULL);
    }
    /* two things in one place */
    for (i = 0; i < p->nitems; i++) {
        PStr a[100], b[100];
        int na, nb;
        EupPath pa, pb;
        na = installed(p, &p->items[i], p->items[i].comp, a);
        for (j = i + 1; j < p->nitems; j++) {
            if (p->items[i].kind == EUP_FOLDER && p->items[j].kind == EUP_FOLDER) continue;
            nb = installed(p, &p->items[j], p->items[j].comp, b);
            pa.n = na; pa.names = a; pb.n = nb; pb.names = b;
            if (eup_same_path(&pa, &pb)) {
                char x[200], y[200];
                path_text(p->items[i].names, p->items[i].n, x, 90);
                path_text(p->items[j].names, p->items[j].n, y, 90);
                perr(c, c->comps[p->items[j].comp].line, "\"%s\" and \"%s\" would be installed in the same place", x, y);
            }
        }
    }
    if (c->nerrors) { eup_pack_free(p); return c->nerrors; }
    *pack = p;
    return 0;
}

void eup_pack_free(EupPackage *p)
{
    if (!p) return;
    free(p->items);
    free(p);
}

/* ---- packing ---- */

typedef struct {
    EupPackage *p;
    unsigned long written;          /* to the data fork */
    unsigned long done;             /* bytes read */
    int stop;
} Run;

static int data_out(void *ref, const unsigned char *b, long n)
{
    Run *r = (Run *)ref;
    if (r->p->in.data_out(r->p->in.data_ref, b, n)) return 1;
    r->written += n;
    return 0;
}

/* UTF-8 text with LF or CR LF ends to Mac Roman with CR, in pieces (a character may straddle two) */
typedef struct {
    unsigned char carry[4];
    int ncarry, after_cr, bad;
} Conv;

static long convert(Conv *v, const unsigned char *in, long n, unsigned char *out)
{
    long k = 0, i;
    for (i = 0; i < n; i++) {
        unsigned char b = in[i];
        if (v->ncarry || b >= 0x80) {
            int need;
            char one;
            int line;
            v->carry[v->ncarry++] = b;
            need = (v->carry[0] & 0xE0) == 0xC0 ? 2 : (v->carry[0] & 0xF0) == 0xE0 ? 3 : (v->carry[0] & 0xF8) == 0xF0 ? 4 : 1;
            if (v->ncarry < need) continue;
            if (eup_from_utf8((const char *)v->carry, v->ncarry, &one, &line) != 1) v->bad = 1;
            else out[k++] = (unsigned char)one;
            v->ncarry = 0;
            v->after_cr = 0;
            continue;
        }
        if (b == '\n' && v->after_cr) { v->after_cr = 0; continue; }
        v->after_cr = b == '\r';
        out[k++] = b == '\n' ? '\r' : b;
    }
    return k;
}

/* One fork of an item into the stream; *len: its length as written */
static int pack_fork(Run *r, Item *it, int fork, EupDeflate *z, unsigned char *buf, unsigned char *conv,
                     unsigned long *len, char *why)
{
    EupPackage *p = r->p;
    EupFS *fs = p->in.fs;
    unsigned long size, at = 0, base = 0;
    void *f;
    Conv v;
    int fk = fork;
    char where[200];
    memset(&v, 0, sizeof v);
    *len = 0;
    if (it->macbinary) {
        size = fork ? it->mb.rlen : it->mb.dlen;
        base = fork ? it->mb.rsrc_at : it->mb.data_at;
        fk = 0;
    } else size = fork ? it->st.rlen : it->st.dlen;
    if (!size) return 0;
    path_text(it->names, it->n, where, 120);
    if (!(f = fs->open(fs->ref, it->names, it->n, fk))) { eup_fmt(why, "\"%s\" can't be opened", where, NULL); return -1; }
    while (at < size) {
        long n = size - at > PIECE ? PIECE : (long)(size - at);
        const unsigned char *b = buf;
        if (fs->read_at(f, base + at, buf, n)) { fs->close(f); eup_fmt(why, "\"%s\" can't be read", where, NULL); return -1; }
        at += n;
        r->done += n;
        if (it->convert && !fork) {
            n = convert(&v, buf, n, conv);
            b = conv;
            if (v.bad) { fs->close(f); eup_fmt(why, "\"%s\" has a character that Mac Roman doesn't have (it's read as UTF-8 text)", where, NULL); return -1; }
        }
        *len += n;
        if (z ? eup_deflate_data(z, b, n) : data_out(r, b, n)) { fs->close(f); eup_fmt(why, "the installer can't be written", NULL, NULL); return -1; }
        if (p->in.progress && p->in.progress(p->in.progress_ref, r->done, p->total)) { fs->close(f); r->stop = 1; eup_fmt(why, "stopped", NULL, NULL); return -1; }
    }
    fs->close(f);
    if (v.ncarry) { eup_fmt(why, "\"%s\" ends in the middle of a UTF-8 character", where, NULL); return -1; }
    return 0;
}

/* The bytes of a text file, as Mac Roman with CR ends, malloc'd */
static unsigned char *read_text(EupPackage *p, const EupPath *path, unsigned long *len, char *why)
{
    EupFS *fs = p->in.fs;
    EupStat st;
    unsigned char *raw, *out;
    void *f;
    char where[200];
    Conv v;
    path_text(path->names, path->n, where, 120);
    if (fs->stat(fs->ref, path->names, path->n, &st) || !(f = fs->open(fs->ref, path->names, path->n, 0))) {
        eup_fmt(why, "\"%s\" can't be opened", where, NULL);
        return NULL;
    }
    raw = (unsigned char *)malloc(st.dlen + 1);
    out = (unsigned char *)malloc(st.dlen + 1);
    if (!raw || !out || fs->read_at(f, 0, raw, (long)st.dlen)) {
        fs->close(f);
        free(raw);
        free(out);
        eup_fmt(why, "\"%s\" can't be read", where, NULL);
        return NULL;
    }
    fs->close(f);
    memset(&v, 0, sizeof v);
    if (fs->utf8_text) {
        long start = (st.dlen >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) ? 3 : 0;
        *len = convert(&v, raw + start, (long)st.dlen - start, out);
        if (v.bad || v.ncarry) {
            free(raw);
            free(out);
            eup_fmt(why, "\"%s\" has a character that Mac Roman doesn't have (it's read as UTF-8 text)", where, NULL);
            return NULL;
        }
    } else {
        unsigned long i;
        for (i = 0; i < st.dlen; i++) out[i] = raw[i] == '\n' ? '\r' : raw[i];
        *len = st.dlen;
    }
    while (*len && out[*len - 1] == '\r') (*len)--;         /* no blank lines at the end */
    free(raw);
    return out;
}

static void joined_name(const EupPath *path, unsigned char *out)
{
    char t[256];
    path_text(path->names, path->n, t, 256);
    out[0] = (unsigned char)strlen(t);
    memcpy(out + 1, t, out[0]);
}

static const unsigned long ICONS[] = { OSTYPE('I', 'C', 'N', '#'), OSTYPE('i', 'c', 's', '#'), OSTYPE('i', 'c', 'l', '4'),
                                       OSTYPE('i', 'c', 'l', '8'), OSTYPE('i', 'c', 's', '4'), OSTYPE('i', 'c', 's', '8'), 0 };

/* The icon file's icons in place of the program's own */
static int icons(EupPackage *p, EupRWrite *w, char *why)
{
    EupConfig *c = p->c;
    EupFS *fs = p->in.fs;
    EupStat st;
    EupMacInfo mb;
    EupResRef *list;
    At a;
    int n, i, k, found = 0;
    unsigned long len;
    char where[200];
    path_text(c->icon.names, c->icon.n, where, 120);
    fs->stat(fs->ref, c->icon.names, c->icon.n, &st);
    a.fs = fs;
    a.base = 0;
    len = st.rlen;
    if (sniff(fs, c->icon.names, c->icon.n, &st, &mb)) {
        a.base = mb.rsrc_at;
        len = mb.rlen;
        a.file = fs->open(fs->ref, c->icon.names, c->icon.n, 0);
    } else a.file = fs->open(fs->ref, c->icon.names, c->icon.n, 1);
    if (!a.file || eup_rfork_list(read_at, &a, len, &list, &n)) {
        if (a.file) fs->close(a.file);
        eup_fmt(why, "icon: \"%s\" has no resources", where, NULL);
        return -1;
    }
    for (k = 0; ICONS[k]; k++) eup_rwrite_remove(w, ICONS[k], 128);
    for (k = 0; ICONS[k]; k++) {
        int pick = -1;
        for (i = 0; i < n; i++)         /* ID 128, or the first */
            if (list[i].type == ICONS[k] && (pick < 0 || list[i].id == 128)) pick = i;
        if (pick >= 0) {
            unsigned char *b = (unsigned char *)malloc(list[pick].len + 1);
            if (!b || read_at(&a, list[pick].at, b, (long)list[pick].len) ||
                eup_rwrite_add(w, ICONS[k], 128, NULL, 0, b, list[pick].len)) {
                free(b);
                free(list);
                fs->close(a.file);
                eup_fmt(why, "icon: \"%s\" can't be read", where, NULL);
                return -1;
            }
            free(b);
            if (!k) found = 1;
        }
    }
    free(list);
    fs->close(a.file);
    if (!found) { eup_fmt(why, "icon: \"%s\" has no 'ICN#' resource", where, NULL); return -1; }
    return 0;
}

int eup_pack(EupPackage *p, EupPackOut *out, char *why)
{
    EupConfig *c = p->c;
    EupRWrite *w = eup_rwrite_new();
    EupResRef *shell = NULL;
    unsigned char *buf = (unsigned char *)malloc(PIECE), *conv = (unsigned char *)malloc(PIECE), *table = NULL;
    unsigned char *names = (unsigned char *)malloc(NAMES);
    long tlen = 2, tcap = 4096;
    int nshell = 0, i, err = -1, nrec = 0;
    Run r;
    memset(out, 0, sizeof *out);
    memset(&r, 0, sizeof r);
    r.p = p;
    eup_fmt(why, "out of memory", NULL, NULL);
    if (!w || !buf || !conv || !names || !(table = (unsigned char *)malloc(tcap))) goto done;
    /* the program */
    if (eup_rfork_list(p->in.shell_read, p->in.shell_ref, p->in.shell_len, &shell, &nshell) || !nshell) {
        eup_fmt(why, "the installer program can't be read", NULL, NULL);
        goto done;
    }
    for (i = 0; i < nshell; i++) {
        unsigned char *b = (unsigned char *)malloc(shell[i].len + 1);
        if (!b) goto done;
        if (p->in.shell_read(p->in.shell_ref, shell[i].at, b, (long)shell[i].len) ||
            eup_rwrite_add(w, shell[i].type, shell[i].id, shell[i].has_name ? shell[i].name : NULL, shell[i].attrs, b, shell[i].len)) {
            free(b);
            eup_fmt(why, "the installer program can't be read", NULL, NULL);
            goto done;
        }
        free(b);
    }
    /* the payload: each file's forks, one after the other, in the data fork */
    for (i = 0; i < p->nitems; i++) {
        Item *it = &p->items[i];
        EupRecord rec;
        long nl = 0;
        int k;
        memset(&rec, 0, sizeof rec);
        rec.kind = it->kind == EUP_FOLDER ? EUP_REC_FOLDER : EUP_REC_FILE;
        rec.at = r.written;
        if (it->kind == EUP_FILE) {
            EupDeflate *z = NULL;
            if (it->compress && !(z = eup_deflate_begin(data_out, &r))) goto done;
            if (!it->compress) rec.flags = EUP_REC_STORED;
            if (pack_fork(&r, it, 0, z, buf, conv, &rec.dlen, why) || pack_fork(&r, it, 1, z, buf, conv, &rec.rlen, why)) {
                if (z) eup_deflate_end(z, NULL);
                goto done;
            }
            if (z && eup_deflate_end(z, NULL)) { eup_fmt(why, "the installer can't be written", NULL, NULL); goto done; }
            rec.type = it->type;
            rec.creator = it->creator;
            rec.finder = it->flags;
            out->files++;
            out->unpacked += rec.dlen + rec.rlen;
        } else out->folders++;
        rec.packed = r.written - rec.at;
        memcpy(rec.name, it->name, it->name[0] + 1);
        rec.n = it->n;
        for (k = 0; k < it->n && nl + 33 < NAMES; k++) {
            memcpy(names + nl, it->names[k], it->names[k][0] + 1);
            nl += 1 + it->names[k][0];
        }
        rec.names = names;
        if (tlen + EUP_REC_FIXED + 33 + nl > tcap) {
            unsigned char *t2;
            tcap = (tcap + EUP_REC_FIXED + 33 + nl) * 2;
            if (!(t2 = (unsigned char *)realloc(table, tcap))) goto done;
            table = t2;
        }
        tlen += eup_record_write(&rec, table + tlen);
        nrec++;
    }
    eup_put16(table, (unsigned int)nrec);
    out->packed = r.written;
    out->data_len = r.written;
    eup_fmt(why, "out of memory", NULL, NULL);
    if (eup_rwrite_add(w, OSTYPE('E', 'f', 'i', 'l'), 128, NULL, 0, table, (unsigned long)tlen)) goto done;
    /* the INI, and the texts it names, by their paths */
    if (eup_rwrite_add(w, OSTYPE('T', 'E', 'X', 'T'), 128, p->in.ini_name, 0, (const unsigned char *)p->in.ini, (unsigned long)p->in.ini_len)) goto done;
    for (i = 0; i < p->ntexts; i++) {
        unsigned long len;
        unsigned char name[256], *t = read_text(p, &p->texts[i].path, &len, why);
        if (!t) goto done;
        joined_name(&p->texts[i].path, name);
        if (eup_rwrite_add(w, OSTYPE('T', 'E', 'X', 'T'), 129 + i, name, 0, t, len)) { free(t); eup_fmt(why, "out of memory", NULL, NULL); goto done; }
        free(t);
    }
    /* Get Info's version */
    eup_rwrite_remove(w, OSTYPE('v', 'e', 'r', 's'), 1);
    if (c->version) {
        unsigned char v[600];
        int n = 0;
        memcpy(v, c->vers, 4);
        v[4] = v[5] = 0;            /* the U.S. */
        n = 6;
        memcpy(v + n, c->version_string, c->version_string[0] + 1);
        n += 1 + c->version_string[0];
        memcpy(v + n, c->get_info, c->get_info[0] + 1);
        n += 1 + c->get_info[0];
        if (eup_rwrite_add(w, OSTYPE('v', 'e', 'r', 's'), 1, NULL, 0, v, n)) goto done;
    }
    /* its own creator: the owner resource and the bundle; and its own icon */
    out->creator = OSTYPE('E', 'U', 'P', 'i');
    if (c->has_creator && c->creator != out->creator) {
        static const unsigned char OWNER[] = "\016Owner resource";
        unsigned char b[40];
        eup_rwrite_remove(w, out->creator, 0);
        out->creator = c->creator;
        if (eup_rwrite_add(w, c->creator, 0, OWNER, 0, c->title, c->title[0] + 1)) goto done;
        /* 'BNDL': the creator and 0, then three types, each with one ID: 'ICN#', 'ics#', and 'FREF', 0 -> 128 */
        eup_put32(b, c->creator);
        eup_put16(b + 4, 0);
        eup_put16(b + 6, 2);
        for (i = 0; i < 3; i++) {
            static const unsigned long T[3] = { OSTYPE('I', 'C', 'N', '#'), OSTYPE('i', 'c', 's', '#'), OSTYPE('F', 'R', 'E', 'F') };
            eup_put32(b + 8 + 10 * i, T[i]);
            eup_put16(b + 12 + 10 * i, 0);
            eup_put16(b + 14 + 10 * i, 0);
            eup_put16(b + 16 + 10 * i, 128);
        }
        if (eup_rwrite_add(w, OSTYPE('B', 'N', 'D', 'L'), 128, NULL, 0, b, 38)) goto done;
    }
    if (c->icon.n && icons(p, w, why)) goto done;
    out->rsrc = eup_rwrite_finish(w, &out->rsrc_len);
    w = NULL;
    if (!out->rsrc) { eup_fmt(why, "out of memory", NULL, NULL); goto done; }
    err = 0;
done:
    if (w) free(eup_rwrite_finish(w, &out->rsrc_len));
    free(shell);
    free(buf);
    free(conv);
    free(names);
    free(table);
    eup_pack_free(p);
    return err;
}
