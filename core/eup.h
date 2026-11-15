/* EUP's core: the INI file, read into a configuration, with every rule checked. Portable C: the same
   code runs in the installer program on the Mac (68K) and in the packagers, which check the INI with it
   before they package anything. Text is Mac Roman throughout; a packager converts its input first
   (eup_from_utf8). The INI's keys are in docs/ini.md. */
#ifndef EUP_H
#define EUP_H

#include <stddef.h>

/* A string as both a Pascal string and a C string: a length byte, the bytes, then a 0. As a Pascal
   string it's s; as a C string, s + 1. */
typedef unsigned char *PStr;
#define EUP_C(s) ((char *)(s) + 1)

/* A path, split into names (each a PStr); in 1.0 every installed path is inside %chosen%, the folder
   the user picks, and every source path is inside the INI's folder */
typedef struct {
    int n;
    PStr *names;
} EupPath;

typedef struct {
    int line;
    PStr id, label, description;    /* description: one line, or NULL */
    EupPath description_file;       /* or a text file (n 0 if none) */
    int custom;                     /* shows the checkbox list */
    int remove_old;                 /* removes the [upgrade] names (a Minimal-style type doesn't) */
} EupType;

/* Finder flags that may be named in `flags`, as numbers */
enum {
    EUP_FL_ALIAS = 0x8000, EUP_FL_INVISIBLE = 0x4000, EUP_FL_BUNDLE = 0x2000, EUP_FL_NAME_LOCKED = 0x1000,
    EUP_FL_STATIONERY = 0x0800, EUP_FL_CUSTOM_ICON = 0x0400, EUP_FL_NO_INIT = 0x0080, EUP_FL_SHARED = 0x0040
};

typedef struct {
    int line;
    PStr id, label;
    int selectable;                 /* its heading in the list has a checkbox for all its components */
} EupGroup;

typedef struct {
    int line;
    PStr id, label, description;    /* description: "" if none */
    int group;                      /* its [group], by index, or -1 */
    EupPath source;                 /* a file or folder in the INI's folder */
    EupPath destination;            /* the folder it goes into, inside %chosen% (n 0 for %chosen% itself) */
    PStr as;                        /* the installed name, or NULL for the file's own */
    unsigned long type, creator;    /* overrides, if has_type, has_creator */
    int has_type, has_creator;
    unsigned int flags;             /* Finder flags, if has_flags */
    int has_flags;
    int text;                       /* plain files are text: converted, and TEXT/ttxt unless overridden */
    int compress;
    int checked;                    /* checked in a custom type's list at first */
    int ntypes;
    int *types;                     /* the types it's in, by index */
} EupComp;

typedef struct {
    int line;
    PStr name;                      /* a name in %chosen% */
    int h, v;
} EupPlace;

typedef struct {
    int line;
    char text[200];
} EupError;

enum { EUP_MAX_ERRORS = 40 };

typedef struct {
    /* [installer] */
    PStr product, version, title, file, output, folder, get_info;
    unsigned long creator;
    int has_creator;
    EupPath icon, description_screen, license_screen;      /* n 0 if none */
    unsigned char vers[4];          /* the version for a 'vers' resource: major, minor and fix, stage, release */
    /* [type], [group], [component] */
    int ntypes, ngroups, ncomps;
    EupType *types;
    EupGroup *groups;
    EupComp *comps;
    /* [upgrade] */
    int nremove, nkeep;
    EupPath *remove, *keep;
    PStr detect;                    /* a file whose folder is an earlier install, or NULL */
    /* [layout] */
    int has_window;
    int window[4];                  /* left, top, width, height */
    int nplaces;
    EupPlace *places;
    /* what's wrong, by line (0 for the whole file) */
    int nerrors;
    EupError errors[EUP_MAX_ERRORS];
    int more_errors;                /* more than EUP_MAX_ERRORS */
    /* private */
    void *arena;
    int cap_types, cap_groups, cap_comps, cap_remove, cap_keep, cap_places;
} EupConfig;

/* Reads the INI text (Mac Roman; CR, LF, or CR LF line ends) into c. Returns the number of errors (in
   c->errors); c holds what could be read either way. -1 if there's no memory. eup_free afterwards. */
int eup_parse(EupConfig *c, const char *text, long len);
void eup_free(EupConfig *c);

/* Paths, by the INI's rules: the colon separates names; %% is a %; a name is 1 to 31 characters, with no
   colon, not starting with a period. A source path is relative to the INI's folder, with or without a
   leading colon. An installed path starts with %chosen% or is relative to it. On an error, returns 0
   and puts the reason in why (at least 200 bytes). */
enum { EUP_SOURCE, EUP_INSTALLED };
int eup_split_path(EupConfig *c, const char *s, int kind, EupPath *out, char *why);
/* A name by the same rules (with %% for a %); NULL and why on an error */
PStr eup_name(EupConfig *c, const char *s, char *why);
/* Whether two paths are the same (names compared as the Mac does, ignoring case) */
int eup_same_path(const EupPath *a, const EupPath *b);
int eup_same_name(const unsigned char *a, const unsigned char *b);

/* UTF-8 to Mac Roman, for a packager's input. Returns the length written to out (which has room for
   len bytes), or -1 with *bad_line set to the line with a character Mac Roman doesn't have. */
long eup_from_utf8(const char *in, long len, char *out, int *bad_line);

#endif
