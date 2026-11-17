/* eup: the command-line packager.
     eup check INI       checks an INI file and the files it names
     eup build INI [-o DIR]   makes the installer: NAME.bin (MacBinary) and NAME.hqx (BinHex 4.0)
   INI may be the INI file, or the folder holding it (if it holds one). The files are read as they are
   on this system: MacBinary files, plain files (text as UTF-8), and folders. */
#define _POSIX_C_SOURCE 200809L     /* strdup, opendir */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../core/eup.h"
#include "../core/format.h"
#include "../core/package.h"
#include "../core/version.h"

extern const unsigned char eup_shell[];     /* the installer program, MacBinary (shell.c, made by the Makefile) */
extern const unsigned long eup_shell_len;

static const char *ini_path;
static char root[4096];

/* ---- the host's files ---- */

static int host_path(PStr *names, int n, char *out, size_t room)
{
    size_t k = strlen(root);
    int i;
    if (k + 1 >= room) return -1;
    memcpy(out, root, k + 1);
    for (i = 0; i < n; i++) {
        char u[800];
        eup_to_utf8(names[i] + 1, names[i][0], u);
        if (k + strlen(u) + 2 >= room) return -1;
        out[k++] = '/';
        strcpy(out + k, u);
        k += strlen(u);
    }
    return 0;
}

static int h_stat(void *ref, PStr *names, int n, EupStat *st)
{
    char p[4096];
    struct stat s;
    (void)ref;
    memset(st, 0, sizeof *st);
    if (host_path(names, n, p, sizeof p) || stat(p, &s)) return 0;
    if (S_ISDIR(s.st_mode)) st->kind = EUP_FOLDER;
    else if (S_ISREG(s.st_mode)) { st->kind = EUP_FILE; st->dlen = (unsigned long)s.st_size; }
    return 0;
}

static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int h_entry(void *ref, PStr *names, int n, int index, unsigned char *name, char *why)
{
    char p[4096], **list = NULL;
    DIR *d;
    struct dirent *e;
    int count = 0, cap = 0, i, r = 1;
    (void)ref;
    if (host_path(names, n, p, sizeof p) || !(d = opendir(p))) return 1;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (count == cap) { cap = cap ? cap * 2 : 64; list = (char **)realloc(list, sizeof(char *) * cap); }
        list[count++] = strdup(e->d_name);
    }
    closedir(d);
    if (count) qsort(list, count, sizeof(char *), cmp);
    if (index < count) {
        const char *u = list[index];
        int line;
        long len = (long)strlen(u);
        char *m = (char *)malloc(len + 1);
        long k = eup_from_utf8(u, len, m, &line);
        if (k < 0 || k > 255 || memchr(m, ':', k)) {
            snprintf(why, 200, "\"%s\" can't be a Mac file name", u);
            r = -1;
        } else {
            name[0] = (unsigned char)k;
            memcpy(name + 1, m, k);
            r = 0;
        }
        free(m);
    }
    for (i = 0; i < count; i++) free(list[i]);
    free(list);
    return r;
}

static void *h_open(void *ref, PStr *names, int n, int fork)
{
    char p[4096];
    (void)ref;
    if (fork || host_path(names, n, p, sizeof p)) return NULL;
    return fopen(p, "rb");
}

static int h_read_at(void *f, unsigned long at, void *buf, long len)
{
    return fseek((FILE *)f, (long)at, SEEK_SET) || fread(buf, 1, len, (FILE *)f) != (size_t)len;
}

static void h_close(void *f) { fclose((FILE *)f); }

/* ---- the installer program: its resource fork, from the MacBinary built in ---- */

static EupMacInfo shell_info;

static int shell_read(void *ref, unsigned long at, void *buf, long n)
{
    (void)ref;
    if (at + n > shell_info.rlen) return 1;
    memcpy(buf, eup_shell + shell_info.rsrc_at + at, n);
    return 0;
}

/* ---- output ---- */

typedef struct { unsigned char *p; unsigned long len, cap; } Buf;

static int buf_out(void *ref, const unsigned char *p, long n)
{
    Buf *b = (Buf *)ref;
    if (b->len + n > b->cap) {
        unsigned long cap = (b->len + n) * 2;
        unsigned char *q = (unsigned char *)realloc(b->p, cap);
        if (!q) return 1;
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->len, p, n);
    b->len += n;
    return 0;
}

static int file_out(void *ref, const unsigned char *p, long n)
{
    return fwrite(p, 1, n, (FILE *)ref) != (size_t)n;
}

/* ---- reading the INI ---- */

static char *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)malloc(*len + 1);
    if (b && fread(b, 1, *len, f) != (size_t)*len) { free(b); b = NULL; }
    fclose(f);
    return b;
}

/* The INI: the file named, or the only .ini file in the folder named */
static int find_ini(const char *arg, char *out, size_t room)
{
    struct stat s;
    DIR *d;
    struct dirent *e;
    int found = 0;
    if (stat(arg, &s)) { fprintf(stderr, "eup: %s: %s\n", arg, strerror(errno)); return -1; }
    if (!S_ISDIR(s.st_mode)) { snprintf(out, room, "%s", arg); return 0; }
    if (!(d = opendir(arg))) { fprintf(stderr, "eup: %s: %s\n", arg, strerror(errno)); return -1; }
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n > 4 && e->d_name[0] != '.' && (!strcmp(e->d_name + n - 4, ".ini") || !strcmp(e->d_name + n - 4, ".INI"))) {
            if (found++) { fprintf(stderr, "eup: %s holds more than one .ini file; name one\n", arg); closedir(d); return -1; }
            snprintf(out, room, "%s/%s", arg, e->d_name);
        }
    }
    closedir(d);
    if (!found) { fprintf(stderr, "eup: %s holds no .ini file\n", arg); return -1; }
    return 0;
}

static void report(const EupConfig *c)
{
    int i;
    for (i = 0; i < c->nerrors; i++) {
        char u[800];
        eup_to_utf8((const unsigned char *)c->errors[i].text, (int)strlen(c->errors[i].text), u);
        if (c->errors[i].line) fprintf(stderr, "%s:%d: %s\n", ini_path, c->errors[i].line, u);
        else fprintf(stderr, "%s: %s\n", ini_path, u);
    }
    if (c->more_errors) fprintf(stderr, "%s: And more\n", ini_path);
}

static void report_warnings(const EupConfig *c)
{
    int i;
    for (i = 0; i < c->nwarnings; i++) {
        char u[800];
        eup_to_utf8((const unsigned char *)c->warnings[i].text, (int)strlen(c->warnings[i].text), u);
        fprintf(stderr, "%s:%d: Warning: %s\n", ini_path, c->warnings[i].line, u);
    }
}

static int usage(void)
{
    fprintf(stderr, "usage: eup check INI\n       eup build INI [-o DIR]\n       eup version\n"
                    "INI: an INI file, or the folder that holds it\n");
    return 2;
}

int main(int argc, char **argv)
{
    static char ini_file[4096];
    const char *outdir = ".";
    char *raw, *text, *slash;
    long len, tlen;
    int line, build, i;
    EupConfig c;
    EupFS fs;
    EupPackIn in;
    EupPackOut out;
    EupPackage *pack;
    Buf data = { 0, 0, 0 };
    char why[200];
    unsigned char ini_name[64];
    if (argc == 2 && !strcmp(argv[1], "version")) { printf("eup %s\n", EUP_VERSION); return 0; }
    if (argc < 3) return usage();
    build = !strcmp(argv[1], "build");
    if (!build && strcmp(argv[1], "check")) return usage();
    for (i = 3; i < argc; i++) {
        if (build && !strcmp(argv[i], "-o") && i + 1 < argc) outdir = argv[++i];
        else return usage();
    }
    if (find_ini(argv[2], ini_file, sizeof ini_file)) return 1;
    ini_path = ini_file;
    if (!(raw = read_file(ini_file, &len))) { fprintf(stderr, "eup: %s: %s\n", ini_file, strerror(errno)); return 1; }
    text = (char *)malloc(len + 1);
    if ((tlen = eup_from_utf8(raw, len, text, &line)) < 0) {
        fprintf(stderr, "%s:%d: A character that Mac Roman doesn't have (the INI is read as UTF-8)\n", ini_file, line);
        return 1;
    }
    for (i = 0; i < tlen; i++) if (text[i] == '\n') text[i] = '\r';     /* as a Mac would keep it */
    if (eup_parse(&c, text, tlen) < 0) { fprintf(stderr, "eup: out of memory\n"); return 1; }
    if (c.nerrors) { report(&c); return 1; }
    report_warnings(&c);
    /* the files: in the INI's folder */
    snprintf(root, sizeof root, "%s", ini_file);
    if ((slash = strrchr(root, '/'))) *slash = 0; else strcpy(root, ".");
    {
        const char *base = strrchr(ini_file, '/') ? strrchr(ini_file, '/') + 1 : ini_file;
        long k = eup_from_utf8(base, (long)strlen(base), (char *)ini_name + 1, &line);
        ini_name[0] = (unsigned char)(k < 0 || k > 31 ? 3 : k);
        if (k < 0 || k > 31) memcpy(ini_name + 1, "INI", 3);
    }
    memset(&fs, 0, sizeof fs);
    fs.stat = h_stat;
    fs.entry = h_entry;
    fs.open = h_open;
    fs.read_at = h_read_at;
    fs.close = h_close;
    fs.utf8_text = 1;
    if (!eup_macbinary_read(eup_shell, eup_shell_len, &shell_info)) { fprintf(stderr, "eup: the built-in installer program is damaged; rebuild eup\n"); return 1; }
    memset(&in, 0, sizeof in);
    in.fs = &fs;
    in.config = &c;
    in.ini = text;
    in.ini_len = tlen;
    in.ini_name = ini_name;
    in.shell_read = shell_read;
    in.shell_len = shell_info.rlen;
    in.data_out = buf_out;
    in.data_ref = &data;
    if (eup_pack_check(&in, &pack)) { report(&c); return 1; }
    if (!build) {
        printf(c.nwarnings ? "%s: no errors\n" : "%s: no problems\n", ini_file);
        eup_pack_free(pack);
        return 0;
    }
    {
        EupMacInfo mi;
        unsigned char head[128];
        char path[4096], name[800];
        FILE *f;
        const char *epoch = getenv("SOURCE_DATE_EPOCH");     /* for builds that come out the same each time */
        unsigned long now = (epoch ? strtoul(epoch, NULL, 10) : (unsigned long)time(NULL)) + 2082844800UL;
        static const unsigned char ZERO[128];
        if (eup_pack(pack, &out, why)) { fprintf(stderr, "eup: %s\n", why); return 1; }
        memset(&mi, 0, sizeof mi);
        memcpy(mi.name, c.file, c.file[0] + 1);
        mi.type = 0x4150504CUL;     /* APPL */
        mi.creator = out.creator;
        mi.flags = 0x2000;          /* has a bundle */
        mi.dlen = data.len;
        mi.rlen = out.rsrc_len;
        mi.created = mi.modified = now;
        eup_macbinary_header(&mi, head);
        eup_to_utf8(c.output + 1, c.output[0], name);
        snprintf(path, sizeof path, "%s/%s.bin", outdir, name);
        if (!(f = fopen(path, "wb"))) { fprintf(stderr, "eup: %s: %s\n", path, strerror(errno)); return 1; }
        fwrite(head, 1, 128, f);
        fwrite(data.p, 1, data.len, f);
        fwrite(ZERO, 1, (128 - data.len % 128) % 128, f);
        fwrite(out.rsrc, 1, out.rsrc_len, f);
        fwrite(ZERO, 1, (128 - out.rsrc_len % 128) % 128, f);
        if (fclose(f)) { fprintf(stderr, "eup: %s: %s\n", path, strerror(errno)); return 1; }
        printf("%s\n", path);
        snprintf(path, sizeof path, "%s/%s.hqx", outdir, name);
        if (!(f = fopen(path, "wb"))) { fprintf(stderr, "eup: %s: %s\n", path, strerror(errno)); return 1; }
        if (eup_binhex(c.file, mi.type, mi.creator, mi.flags, data.p, data.len, out.rsrc, out.rsrc_len, file_out, f) || fclose(f)) {
            fprintf(stderr, "eup: %s: can't be written\n", path);
            return 1;
        }
        printf("%s\n", path);
        printf("%d files, %d folders: %lu bytes, %lu packed; the installer program %lu bytes\n", out.files, out.folders,
               out.unpacked, out.packed, out.rsrc_len);
        free(data.p);
        free(out.rsrc);
    }
    eup_free(&c);
    return 0;
}
