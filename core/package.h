/* Packaging: the files an INI names, read through a file-source interface (the host's files, or the
   Mac's), checked, then packed into a copy of the installer program: the payload into its data fork,
   the INI, the texts, and the record table into its resources. Used by the command-line packager and
   the packager application alike. */
#ifndef EUP_PACKAGE_H
#define EUP_PACKAGE_H

#include "eup.h"
#include "format.h"

enum { EUP_NOTHING = 0, EUP_FILE = 1, EUP_FOLDER = 2 };

/* What's at a path */
typedef struct {
    int kind;
    unsigned long dlen, rlen;       /* the forks (a host file has only a data fork) */
    unsigned long type, creator;    /* a Mac file's own (has_info), else nothing */
    unsigned int flags;
    int has_info;
} EupStat;

/* The files, by path: n names (Pascal strings, Mac Roman) inside the INI's folder */
typedef struct {
    void *ref;
    int (*stat)(void *ref, PStr *names, int n, EupStat *st);
    /* A folder's index'th entry (from 0, in name order): its name into name (a Pascal string, Mac Roman,
       at most 255); 0, 1 at the end, or -1 for a name that can't be a Mac name (why says so) */
    int (*entry)(void *ref, PStr *names, int n, int index, unsigned char *name, char *why);
    /* Opens a fork (0 data, 1 resource) to read; NULL if it can't */
    void *(*open)(void *ref, PStr *names, int n, int fork);
    int (*read_at)(void *file, unsigned long at, void *buf, long len);     /* 0 if all read */
    void (*close)(void *file);
    int utf8_text;                  /* plain text files are UTF-8 with LF ends (any system but the Mac) */
} EupFS;

enum { EUP_MAX_FILES = 2000 };

typedef struct EupPackage EupPackage;

typedef struct {
    EupFS *fs;
    EupConfig *config;
    const char *ini;                /* the INI's text, Mac Roman, as it's stored */
    long ini_len;
    const unsigned char *ini_name;  /* its file name, a Pascal string */
    /* the installer program: its resource fork */
    eup_read_at shell_read;
    void *shell_ref;
    unsigned long shell_len;
    /* the new installer's data fork, written in order */
    eup_out data_out;
    void *data_ref;
    /* how far it's got, and whether to stop: nonzero to stop (may be NULL) */
    int (*progress)(void *ref, unsigned long done, unsigned long total);
    void *progress_ref;
} EupPackIn;

typedef struct {
    unsigned char *rsrc;            /* the new installer's resource fork, malloc'd */
    unsigned long rsrc_len, data_len;
    unsigned long creator;          /* the installer's creator */
    int files, folders;
    unsigned long unpacked, packed; /* the payload: the files' forks, and as stored */
} EupPackOut;

/* Checks the files the INI names: every source there, of the right kind, with names a Mac can have, and no two
   installed at the same place. Problems go into in->config's errors, with the line of the key. Returns the
   number of errors. On success, a package ready for eup_pack (eup_pack_free if not packing). */
int eup_pack_check(const EupPackIn *in, EupPackage **pack);
/* Writes the payload (through in->data_out) and makes the resource fork. 0, or -1 with why (200 bytes). Frees pack. */
int eup_pack(EupPackage *pack, EupPackOut *out, char *why);
void eup_pack_free(EupPackage *pack);

/* Mac Roman to UTF-8, for a host's file names: out has room for 3 * len + 1 */
void eup_to_utf8(const unsigned char *in, int len, char *out);

#endif
