/* EUP's core, the file formats: CRCs, gzip, MacBinary, resource forks, BinHex 4.0, and the record of
   each packaged file. Portable C, for the installer program, the packager on the Mac, and the
   command-line packager. */
#ifndef EUP_FORMAT_H
#define EUP_FORMAT_H

#include "eup.h"

/* Output, in pieces: 0 to go on, nonzero to stop */
typedef int (*eup_out)(void *ref, const unsigned char *p, long n);

unsigned short eup_crc16(unsigned short crc, const unsigned char *p, long n);  /* CRC-CCITT (XMODEM) */
unsigned long eup_crc32(unsigned long crc, const unsigned char *p, long n);    /* gzip's; start at 0 */

/* gzip, written as it's given */
typedef struct EupDeflate EupDeflate;
EupDeflate *eup_deflate_begin(eup_out out, void *ref);     /* NULL if there's no memory */
int eup_deflate_data(EupDeflate *d, const unsigned char *p, long n);
int eup_deflate_end(EupDeflate *d, unsigned long *size);

/* Big-endian numbers */
unsigned long eup_get32(const unsigned char *p);
unsigned int eup_get16(const unsigned char *p);
void eup_put32(unsigned char *p, unsigned long v);
void eup_put16(unsigned char *p, unsigned int v);

/* MacBinary: a file's description from a 128-byte header, if it is one (MacBinary I as Retro68 writes it,
   or II), and a MacBinary II header for one */
typedef struct {
    unsigned char name[64];         /* a Pascal string */
    unsigned long type, creator;
    unsigned int flags;
    unsigned long dlen, rlen;
    unsigned long created, modified;
    unsigned long data_at, rsrc_at; /* where the forks start in the MacBinary file */
} EupMacInfo;
/* 1 if head (128 bytes of a file size bytes long) is a MacBinary header, filling info */
int eup_macbinary_read(const unsigned char *head, unsigned long size, EupMacInfo *info);
void eup_macbinary_header(const EupMacInfo *info, unsigned char *head);

/* Resource forks. Reading takes the fork through a function that reads at an offset (so a fork needn't
   be all in memory); writing builds one in memory. */
typedef int (*eup_read_at)(void *ref, unsigned long at, void *buf, long n);     /* 0 if all read */
typedef struct {
    unsigned long type;
    int id;
    unsigned char name[256];        /* a Pascal string; empty for none */
    int has_name;
    unsigned char attrs;
    unsigned long at, len;          /* the data, where it is in the fork */
} EupResRef;
/* The resources in a fork len bytes long: *n of them in a malloc'd array (free it); 0, or -1 if it isn't one */
int eup_rfork_list(eup_read_at read, void *ref, unsigned long len, EupResRef **list, int *n);

typedef struct EupRWrite EupRWrite;
EupRWrite *eup_rwrite_new(void);
/* Adds one (data copied); 0, or -1 if there's no memory */
int eup_rwrite_add(EupRWrite *w, unsigned long type, int id, const unsigned char *name, unsigned char attrs,
                   const unsigned char *data, unsigned long len);
int eup_rwrite_has(EupRWrite *w, unsigned long type, int id);
void eup_rwrite_remove(EupRWrite *w, unsigned long type, int id);
/* The fork's bytes, malloc'd (*len long), or NULL; frees w either way */
unsigned char *eup_rwrite_finish(EupRWrite *w, unsigned long *len);

/* BinHex 4.0 of a file, in pieces to out (CR line ends) */
int eup_binhex(const unsigned char *name, unsigned long type, unsigned long creator, unsigned int flags,
               const unsigned char *data, unsigned long dlen, const unsigned char *rsrc, unsigned long rlen,
               eup_out out, void *ref);

/* The record of each packaged file, in 'Efil' 128: what the installer needs to put it in place */
enum { EUP_REC_FILE = 0, EUP_REC_FOLDER = 1 };
enum { EUP_REC_STORED = 1 };        /* the forks as they are, not gzip */
typedef struct {
    int kind, flags;
    unsigned long type, creator;
    unsigned int finder;            /* Finder flags */
    unsigned long dlen, rlen;       /* the forks */
    unsigned long packed, at;       /* the gzip stream of both forks, one after the other, in the data fork */
    unsigned char name[32];         /* the installed name (a Pascal string) */
    int n;                          /* the source path, inside the INI's folder: n names */
    const unsigned char *names;     /* the names, Pascal strings one after another */
} EupRecord;
#define EUP_REC_FIXED 28            /* bytes before the names */
/* The record at p (from a record table); returns the next one's address */
const unsigned char *eup_record_read(const unsigned char *p, EupRecord *r);
/* A record's bytes into out (room for EUP_REC_FIXED + 32 + 1 + 32 * n); returns its length */
long eup_record_write(const EupRecord *r, unsigned char *out);
/* Whether the record's source path is path, or (prefix) inside it; at sets the first name after it */
int eup_record_under(const EupRecord *r, const EupPath *path, const unsigned char **rest, int *nrest);

#endif
