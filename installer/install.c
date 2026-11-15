/* The EUP installer program: one window, from the optional description and license screens to the
   install types, the choices, and the installing. The packager copies it and adds what it installs:

     'TEXT' 128      the INI file, read here with EUP's core (core/ini.c) as the packager read it
     'TEXT' 129 on   the texts the INI names, each named by its path ("Read Me.txt", "Texts:License")
     'Efil' 128      a record of each file and folder packaged: its source path, its installed name,
                     type, creator, Finder flags, fork lengths, and where its payload is (core/format.h)
     the data fork   the payload: each file's two forks, one after the other, gzip-compressed or stored

   Plain File Manager calls, so it runs on System 6. */
#include <Multiverse.h>
#include <string.h>
#include <stdlib.h>

#include "../core/eup.h"
#include "../core/format.h"
#include "../core/inflate.h"

/* One window, 440 by 256, for every stage; the program draws what's in it over a user item, around the buttons */
enum { PROG = 130, WIN = 128, B_PRIMARY = 1, B_QUIT, B_CHOOSE, CANVAS };
enum { S_DESC, S_LICENSE, S_INSTALL };

/* EUP's own strings: 'STR#' 128 */
enum { STR_INSTALL = 1, STR_QUIT, STR_CHOOSE, STR_OK, STR_CONTINUE, STR_AGREE, STR_DISAGREE, STR_SPACE, STR_LOCATION,
       STR_ITEMS_GO, STR_INSTALL_IN, STR_INSTALLED, STR_CANCELED, STR_FAILED, STR_FOLDER_FAILED, STR_INSTALLING,
       STR_REMOVING, STR_AGREE_NOTE, STR_K };

enum { PIECE = 4096 };              /* the payload is read this much at a time */

/* ---- what's installed: the INI, and the records of the files ---- */

typedef struct {
    long disk, unpacked;            /* bytes on a disk (in 1K blocks), and unpacked */
    short n, *recs;                 /* its records */
    Boolean checked;
} Comp;

typedef struct { short comp, group; } Row;          /* comp -1: group's heading */

static EupConfig cfg;
static short nrecs;
static const unsigned char **rec_at;    /* each record, in 'Efil' */
static Comp *comps;
static Row *rows;
static short nrows;
static Boolean headings_boxed;      /* some heading in the list has a checkbox: all headings' text lines up after it */
static short payload;               /* the data fork, open */

static unsigned char *res_ptr(ResType t, short id, long *len)
{
    Handle h = Get1Resource(t, id);
    if (!h) return NULL;
    HNoPurge(h);
    MoveHHi(h);
    HLock(h);
    if (len) *len = GetHandleSize(h);
    return (unsigned char *)*h;
}

/* The data fork of this program, to read the payload */
static OSErr open_payload(void)
{
    FCBPBRec f;
    HParamBlockRec h;
    Str255 name;
    OSErr err;
    memset(&f, 0, sizeof f);
    f.ioNamePtr = name;
    f.ioRefNum = CurResFile();
    if ((err = PBGetFCBInfoSync(&f)) != noErr) return err;
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = name;
    h.fileParam.ioVRefNum = f.ioFCBVRefNum;
    h.fileParam.ioDirID = f.ioFCBParID;
    h.ioParam.ioPermssn = fsRdPerm;
    err = PBHOpenSync(&h);
    payload = h.ioParam.ioRefNum;
    return err;
}

static Boolean load(void)
{
    unsigned char *p;
    long len;
    short i, k;
    if (!(p = res_ptr('TEXT', 128, &len)) || eup_parse(&cfg, (const char *)p, len) != 0) return false;
    ReleaseResource(Get1Resource('TEXT', 128));
    if (!(p = res_ptr('Efil', 128, NULL))) return false;
    nrecs = (short)eup_get16(p);
    p += 2;
    if (!(rec_at = (const unsigned char **)NewPtr(sizeof(Ptr) * (nrecs + 1)))) return false;
    for (i = 0; i < nrecs; i++) {
        EupRecord r;
        rec_at[i] = p;
        p = (unsigned char *)eup_record_read(p, &r);
    }
    if (!(comps = (Comp *)NewPtrClear(sizeof(Comp) * (cfg.ncomps + 1)))) return false;
    /* each component's records: its source, or what's inside it */
    for (k = 0; k < cfg.ncomps; k++) {
        Comp *c = &comps[k];
        c->checked = cfg.comps[k].checked != 0;
        for (i = 0; i < nrecs; i++) {
            EupRecord r;
            eup_record_read(rec_at[i], &r);
            if (eup_record_under(&r, &cfg.comps[k].source, NULL, NULL)) c->n++;
        }
        if (!(c->recs = (short *)NewPtr(sizeof(short) * (c->n + 1)))) return false;
        c->n = 0;
        for (i = 0; i < nrecs; i++) {
            EupRecord r;
            eup_record_read(rec_at[i], &r);
            if (!eup_record_under(&r, &cfg.comps[k].source, NULL, NULL)) continue;
            c->recs[c->n++] = i;
            c->disk += (long)(r.dlen + 1023) / 1024 * 1024 + (long)(r.rlen + 1023) / 1024 * 1024;
            c->unpacked += (long)(r.dlen + r.rlen);
        }
    }
    /* the longest list: every component and a heading for each */
    if (!(rows = (Row *)NewPtr(sizeof(Row) * (2 * cfg.ncomps + 1)))) return false;
    return open_payload() == noErr;
}

/* ---- strings ---- */

static void pcopy(Str255 out, ConstStr255Param s) { memcpy(out, s, s[0] + 1); }

static void pcat(Str255 out, ConstStr255Param s)
{
    short n = s[0];
    if (out[0] + n > 255) n = 255 - out[0];
    memcpy(out + 1 + out[0], s + 1, n);
    out[0] += n;
}

/* One of EUP's strings, with ^0 and ^1 replaced */
static void fmt(Str255 out, short index, ConstStr255Param a0, ConstStr255Param a1)
{
    Str255 t;
    short i;
    GetIndString(t, 128, index);
    out[0] = 0;
    for (i = 1; i <= t[0]; i++) {
        if (t[i] == '^' && i < t[0] && (t[i + 1] == '0' || t[i + 1] == '1')) {
            ConstStr255Param a = t[i + 1] == '0' ? a0 : a1;
            if (a) pcat(out, a);
            i++;
        } else if (out[0] < 255) out[++out[0]] = t[i];
    }
}

static void number_k(Str255 out, long k)
{
    Str255 n;
    NumToString(k, n);
    fmt(out, STR_K, n, NULL);
}

/* A path's names joined with colons: how the packager names a text resource */
static void joined(const EupPath *p, Str255 out)
{
    short i;
    out[0] = 0;
    for (i = 0; i < p->n; i++) {
        if (i && out[0] < 255) out[++out[0]] = ':';
        pcat(out, p->names[i]);
    }
}

/* ---- the File Manager ---- */

/* a file being written, from the forks that come out of inflate: the data fork, then the resource fork */
typedef struct {
    short vref;
    long dir;
    Str255 name, tmp;       /* the file's name, and what it's called while it's written */
    EupRecord rec;
    Point loc;              /* where its icon goes in the folder's window, if place */
    char place;
    long at, polled;        /* where in the stream the next piece begins, and where Cancel was last looked for */
    short df, rf;
    char opened;
    OSErr err;
} Out;

static short place_vref;    /* the install folder: volume and directory */
static long place_dir;

static OSErr dir_of(short vref, long parent, ConstStr255Param name, long *dir)
{
    HParamBlockRec h;
    CInfoPBRec c;
    OSErr err;
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = (StringPtr)name;
    h.fileParam.ioVRefNum = vref;
    h.fileParam.ioDirID = parent;
    err = PBDirCreateSync(&h);
    if (err == noErr) { *dir = h.fileParam.ioDirID; return noErr; }
    if (err != dupFNErr) return err;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioNamePtr = (StringPtr)name;
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = parent;
    if ((err = PBGetCatInfoSync(&c)) != noErr) return err;
    if (!(c.dirInfo.ioFlAttrib & 16)) return dupFNErr;      /* a file in the way */
    *dir = c.dirInfo.ioDrDirID;
    return noErr;
}

/* A folder that's there (not made, as dir_of does) */
static OSErr find_dir(short vref, long parent, ConstStr255Param name, long *dir)
{
    CInfoPBRec c;
    OSErr err;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioNamePtr = (StringPtr)name;
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = parent;
    if ((err = PBGetCatInfoSync(&c)) != noErr) return err;
    if (!(c.dirInfo.ioFlAttrib & 16)) return fnfErr;
    *dir = c.dirInfo.ioDrDirID;
    return noErr;
}

/* 0 nothing there, 1 a file, 2 a folder */
static short what_is(short vref, long dir, ConstStr255Param name)
{
    CInfoPBRec c;
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = (StringPtr)name;
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = dir;
    if (PBGetCatInfoSync(&c) != noErr) return 0;
    return (c.hFileInfo.ioFlAttrib & 16) ? 2 : 1;
}

static void remove_file(short vref, long dir, ConstStr255Param name)
{
    HParamBlockRec h;
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = (StringPtr)name;
    h.fileParam.ioVRefNum = vref;
    h.fileParam.ioDirID = dir;
    PBHDeleteSync(&h);
}

static OSErr put(short ref, const unsigned char *p, long n)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ref;
    pb.ioParam.ioBuffer = (Ptr)p;
    pb.ioParam.ioReqCount = n;
    pb.ioParam.ioPosMode = fsAtMark;
    return PBWriteSync(&pb);
}

static OSErr get_at(short ref, long at, unsigned char *p, long n)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ref;
    pb.ioParam.ioBuffer = (Ptr)p;
    pb.ioParam.ioReqCount = n;
    pb.ioParam.ioPosMode = fsFromStart;
    pb.ioParam.ioPosOffset = at;
    return PBReadSync(&pb);
}

static OSErr open_fork(short vref, long dir, ConstStr255Param name, Boolean rsrc, short *ref)
{
    HParamBlockRec h;
    OSErr err;
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = (StringPtr)name;
    h.fileParam.ioVRefNum = vref;
    h.fileParam.ioDirID = dir;
    h.ioParam.ioPermssn = fsWrPerm;
    err = rsrc ? PBHOpenRFSync(&h) : PBHOpenSync(&h);
    *ref = h.ioParam.ioRefNum;
    return err;
}

static void close_fork(short ref)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ref;
    PBCloseSync(&pb);
}

/* The file, written under a name of its own and put in place when it's whole, so a Cancel or a full disk
   leaves what was there */
static OSErr begin_file(Out *o)
{
    HParamBlockRec h;
    OSErr err;
    short n = o->name[0] > 30 ? 30 : o->name[0];
    o->tmp[0] = (unsigned char)(n + 1);
    o->tmp[1] = '~';
    memcpy(o->tmp + 2, o->name + 1, n);
    remove_file(o->vref, o->dir, o->tmp);
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = o->tmp;
    h.fileParam.ioVRefNum = o->vref;
    h.fileParam.ioDirID = o->dir;
    if ((err = PBHCreateSync(&h)) != noErr) return err;
    o->opened = 1;
    o->df = o->rf = 0;
    if (o->rec.dlen && (err = open_fork(o->vref, o->dir, o->tmp, false, &o->df)) != noErr) return err;
    if (o->rec.rlen && (err = open_fork(o->vref, o->dir, o->tmp, true, &o->rf)) != noErr) return err;
    return noErr;
}

/* Done with it: whole, it gets its type, creator, and flags and its name (replacing the old file); else it's deleted */
static void end_file(Out *o, Boolean whole)
{
    HParamBlockRec h;
    if (o->df) close_fork(o->df);
    if (o->rf) close_fork(o->rf);
    o->df = o->rf = 0;
    if (!o->opened) return;
    if (!whole) { remove_file(o->vref, o->dir, o->tmp); return; }
    /* "inited" off so the Finder places it */
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = o->tmp;
    h.fileParam.ioVRefNum = o->vref;
    h.fileParam.ioDirID = o->dir;
    if (PBHGetFInfoSync(&h) == noErr) {
        h.fileParam.ioDirID = o->dir;
        h.fileParam.ioFlFndrInfo.fdType = o->rec.type;
        h.fileParam.ioFlFndrInfo.fdCreator = o->rec.creator;
        h.fileParam.ioFlFndrInfo.fdFlags = (short)(o->rec.finder & ~0x0100);
        h.fileParam.ioFlFndrInfo.fdLocation.h = h.fileParam.ioFlFndrInfo.fdLocation.v = 0;
        h.fileParam.ioFlFndrInfo.fdFldr = 0;
        if (o->place) {         /* "inited" on, so the Finder keeps the place */
            h.fileParam.ioFlFndrInfo.fdFlags |= 0x0100;
            h.fileParam.ioFlFndrInfo.fdLocation = o->loc;
        }
        PBHSetFInfoSync(&h);
    }
    remove_file(o->vref, o->dir, o->name);
    memset(&h, 0, sizeof h);
    h.ioParam.ioNamePtr = o->tmp;
    h.ioParam.ioVRefNum = o->vref;
    h.fileParam.ioDirID = o->dir;
    h.ioParam.ioMisc = (Ptr)o->name;
    PBHRenameSync(&h);
}

/* ---- the progress window, and Cancel: a click on the button, Escape, or Command-period ---- */

static DialogPtr prog_dlg;
static ControlHandle c_primary, c_cancel;
static Rect prog_bar;
static long prog_total, prog_done;            /* bytes of the files to write, and done */
static short prog_drawn;                      /* pixels of the bar filled in */
static Str255 prog_line;
static Boolean prog_over;                     /* the end: the message and one button, no bar */

/* The bar moves with every piece that comes out of the decompressor */
static void prog_add(long n)
{
    short w = prog_bar.right - prog_bar.left - 4, x;
    Rect r;
    GrafPtr save;
    prog_done += n;
    if (!prog_dlg || prog_total <= 0) return;
    x = (short)((long)w * (prog_done > prog_total ? prog_total : prog_done) / prog_total);
    if (x <= prog_drawn) return;
    GetPort(&save);
    SetPort(prog_dlg);
    SetRect(&r, prog_bar.left + 2 + prog_drawn, prog_bar.top + 2, prog_bar.left + 2 + x, prog_bar.bottom - 2);
    PaintRect(&r);
    prog_drawn = x;
    SetPort(save);
}

static Boolean poll_cancel(void)
{
    EventRecord e;
    while (GetNextEvent(mDownMask | keyDownMask | autoKeyMask, &e)) {
        if (e.what == mouseDown) {
            Point p = e.where;
            ControlHandle c;
            WindowPtr w;
            if (prog_dlg && FindWindow(p, &w) == inContent && w == (WindowPtr)prog_dlg) {
                SetPort(prog_dlg);
                GlobalToLocal(&p);
                if (FindControl(p, prog_dlg, &c) && c == c_cancel && TrackControl(c, p, NULL)) return true;
            }
        } else {
            char ch = e.message & charCodeMask;
            if (ch == 27 || (ch == '.' && (e.modifiers & cmdKey))) return true;
        }
    }
    return false;
}

/* inflate's output: the data fork, then the resource fork */
static int sink(void *ref, const unsigned char *p, long n)
{
    Out *o = ref;
    long at = o->at, end = at + n, de = (long)o->rec.dlen, re = de + (long)o->rec.rlen, a, b;
    if (o->err) return 1;
    if (n <= 0) return 0;
    if (at - o->polled >= 4096) {       /* every 4K or so */
        o->polled = at;
        if (poll_cancel()) { o->err = userCanceledErr; return 1; }
    }
    a = at; b = end < de ? end : de;
    if (o->df && b > a && (o->err = put(o->df, p, b - a)) != noErr) return 1;
    a = at > de ? at : de; b = end < re ? end : re;
    if (o->rf && b > a && (o->err = put(o->rf, p + (a - at), b - a)) != noErr) return 1;
    o->at = end;
    prog_add(n);
    return 0;
}

/* Where the Finder shows a name in the install folder's window, from [layout] */
static Boolean place_of(ConstStr255Param name, Point *loc)
{
    short i;
    for (i = 0; i < cfg.nplaces; i++)
        if (eup_same_name(name, cfg.places[i].name)) {
            loc->h = (short)cfg.places[i].h;
            loc->v = (short)cfg.places[i].v;
            return true;
        }
    return false;
}

static void put_folder(short vref, long parent, ConstStr255Param name, const Point *loc, const Rect *window)
{
    CInfoPBRec c;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioNamePtr = (StringPtr)name;
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = parent;
    if (PBGetCatInfoSync(&c) != noErr) return;
    if (loc) c.dirInfo.ioDrUsrWds.frLocation = *loc;
    if (window) c.dirInfo.ioDrUsrWds.frRect = *window;
    c.dirInfo.ioDrUsrWds.frFlags |= 0x0100;
    c.dirInfo.ioNamePtr = (StringPtr)name;
    c.dirInfo.ioDrDirID = parent;
    PBSetCatInfoSync(&c);
}

/* The install folder's own window, if the INI gives one */
static void size_window(short vref, long dir)
{
    Str255 name;
    CInfoPBRec c;
    Rect r;
    if (!cfg.has_window) return;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioNamePtr = name;
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = dir;
    c.dirInfo.ioFDirIndex = -1;
    if (PBGetCatInfoSync(&c) != noErr) return;
    SetRect(&r, cfg.window[0], cfg.window[1], cfg.window[0] + cfg.window[2], cfg.window[1] + cfg.window[3]);
    put_folder(vref, c.dirInfo.ioDrParID, name, NULL, &r);
}

/* Into a folder, made if need be, on the way down from the install folder; a folder there gets its place */
static OSErr into(long *dir, ConstStr255Param name)
{
    long parent = *dir;
    Point loc;
    OSErr err = dir_of(place_vref, parent, name, dir);
    if (!err && parent == place_dir && place_of(name, &loc)) put_folder(place_vref, parent, name, &loc, NULL);
    return err;
}

/* One record of component k into the install folder: a folder, or a file from the payload */
static OSErr install_record(short k, short index)
{
    const EupComp *m = &cfg.comps[k];
    const unsigned char *rest;
    unsigned char *buf;
    long dir = place_dir, at;
    int nrest;
    short i;
    OSErr err = noErr;
    Out o;
    memset(&o, 0, sizeof o);
    eup_record_read(rec_at[index], &o.rec);
    eup_record_under(&o.rec, &m->source, &rest, &nrest);
    for (i = 0; i < m->destination.n; i++) if ((err = into(&dir, m->destination.names[i])) != noErr) return err;
    for (i = 0; i < nrest - 1; i++, rest += 1 + rest[0]) if ((err = into(&dir, rest)) != noErr) return err;
    if (o.rec.kind == EUP_REC_FOLDER) return into(&dir, o.rec.name);
    /* a file: kept if it's one the INI says to keep and it's there */
    for (i = 0; i < cfg.nkeep; i++) {
        const EupPath *kp = &cfg.keep[i];
        long kd = place_dir;
        short j;
        for (j = 0; j < kp->n - 1 && find_dir(place_vref, kd, kp->names[j], &kd) == noErr; j++) ;
        if (j == kp->n - 1 && kd == dir && eup_same_name(kp->names[kp->n - 1], o.rec.name) && what_is(place_vref, dir, o.rec.name) == 1) {
            prog_add((long)(o.rec.dlen + o.rec.rlen));
            return noErr;
        }
    }
    o.vref = place_vref;
    o.dir = dir;
    pcopy(o.name, o.rec.name);
    o.polled = -4096;
    if (dir == place_dir && place_of(o.name, &o.loc)) o.place = 1;
    if (!(buf = (unsigned char *)NewPtr(PIECE))) return memFullErr;
    if ((o.err = begin_file(&o)) == noErr) {
        void *z = NULL;
        int r = 0;
        if (!(o.rec.flags & EUP_REC_STORED) && !(z = gz_begin(sink, &o))) o.err = memFullErr;
        for (at = 0; at < (long)o.rec.packed && !o.err && r == 0; at += PIECE) {
            long n = (long)o.rec.packed - at > PIECE ? PIECE : (long)o.rec.packed - at;
            if ((o.err = get_at(payload, (long)o.rec.at + at, buf, n)) != noErr) break;
            if (z) r = gz_data(z, buf, n);
            else sink(&o, buf, n);
        }
        if (z) gz_end(z);
        if (!o.err && (r < 0 || o.at != (long)(o.rec.dlen + o.rec.rlen))) o.err = paramErr;   /* the payload is damaged */
    }
    end_file(&o, o.err == noErr);
    DisposePtr((Ptr)buf);
    return o.err;
}

/* ---- the window ---- */

enum { MARGIN = 12, ROW = 16, BAR = 16 };

static short stage;
static Str255 note;                 /* a note under the box */
static ControlHandle c_quit, c_choose;

static void small_font(void) { TextFont(kFontIDGeneva); TextSize(9); TextFace(0); }
static void big_font(void) { TextFont(systemFont); TextSize(12); TextFace(0); }

static void ring(ControlHandle c)
{
    Rect r = (*c)->contrlRect;
    PenState ps;
    if (!(*c)->contrlVis) return;
    GetPenState(&ps);
    PenSize(3, 3);
    InsetRect(&r, -4, -4);
    FrameRoundRect(&r, 16, 16);
    SetPenState(&ps);
}

/* Text cut to fit in width, with an ellipsis, in the current font */
static void draw_fit(ConstStr255Param t, short width)
{
    Str255 s;
    short n;
    pcopy(s, t);
    n = s[0];
    if (StringWidth(s) > width) {
        while (n > 0) {
            s[0] = (unsigned char)(n + 1);
            s[n + 1] = (char)0xC9;
            if (StringWidth(s) <= width) break;
            n--;
        }
    }
    DrawString(s);
}

/* ---- text boxes that scroll: a type's description, and the description and license screens ---- */

typedef struct {
    TEHandle te;
    Rect box, view;
    short line, top, max;       /* line height, lines scrolled, most lines to scroll */
    ControlHandle bar;
    Boolean barred;             /* the text is longer than the box: the bar shows */
} TBox;

static TBox tb_type, tb_screen, *tracking;

static void tb_new(DialogPtr d, TBox *b, short left, short top_, short right, short bottom)
{
    Rect r;
    SetRect(&b->box, left, top_, right, bottom);
    b->view = b->box;
    InsetRect(&b->view, 6, 5);
    small_font();
    b->te = TENew(&b->view, &b->view);
    b->line = (*b->te)->lineHeight;
    SetRect(&r, right - BAR, top_, right, bottom);
    b->bar = NewControl(d, &r, "\p", false, 0, 0, 0, scrollBarProc, 0);
}

/* The text: wrapped to the whole box, or, if it's longer than the box, beside the bar */
static void tb_set(TBox *b, Ptr text, long len)
{
    short lines;
    small_font();
    b->view = b->box;
    InsetRect(&b->view, 6, 5);
    b->view.bottom = b->view.top + (b->view.bottom - b->view.top) / b->line * b->line;     /* whole lines only */
    (*b->te)->viewRect = b->view;
    (*b->te)->destRect = b->view;
    TESetText(text, len, b->te);
    lines = (b->view.bottom - b->view.top) / b->line;
    b->barred = (*b->te)->nLines > lines;
    if (b->barred) {
        b->view.right = b->box.right - BAR - 6;
        (*b->te)->viewRect = b->view;
        (*b->te)->destRect = b->view;
        TECalText(b->te);
    }
    b->top = 0;
    b->max = (*b->te)->nLines - lines;
    if (b->max < 0) b->max = 0;
    SetControlMaximum(b->bar, b->max);
    SetControlValue(b->bar, 0);
    HiliteControl(b->bar, b->max ? 0 : 255);
}

/* A text the INI names, from its resource */
static void tb_set_file(TBox *b, const EupPath *path)
{
    Str255 name;
    Handle h;
    joined(path, name);
    if ((h = Get1NamedResource('TEXT', name))) {
        HLock(h);
        tb_set(b, *h, GetHandleSize(h));
        HUnlock(h);
        ReleaseResource(h);
    } else tb_set(b, (Ptr)"", 0);
}

static void tb_show(TBox *b, Boolean on)
{
    if (on && b->barred) ShowControl(b->bar); else HideControl(b->bar);
}

static void tb_draw(TBox *b)
{
    FrameRect(&b->box);
    small_font();
    TEUpdate(&b->view, b->te);
}

static void tb_scroll(TBox *b, short to)
{
    if (to < 0) to = 0;
    if (to > b->max) to = b->max;
    if (to == b->top) return;
    small_font();
    TEScroll(0, (b->top - to) * b->line, b->te);
    b->top = to;
    SetControlValue(b->bar, to);
}

static pascal void tb_track(ControlHandle c, short part)
{
    TBox *b = tracking;
    short page = (b->view.bottom - b->view.top) / b->line - 1;
    (void)c;
    if (part == inUpButton) tb_scroll(b, b->top - 1);
    else if (part == inDownButton) tb_scroll(b, b->top + 1);
    else if (part == inPageUp) tb_scroll(b, b->top - page);
    else if (part == inPageDown) tb_scroll(b, b->top + page);
}

/* ---- the install choices ---- */

static short place_vol;             /* where the install folder is: a volume, the folder it's in, and its name; */
static long place_parent, place_found;   /* or an existing install's own folder (place_found, else 0) */
static Str255 place_name, place_vol_name;
static short mode;                  /* the install type shown */
static long avail_k, need_k;
static short sel = -1, top;         /* the selected row (an item, or -1), and the first row shown */
static MenuHandle popup;
static Rect w2_popup, w2_box, w2_list;
static ControlHandle w2_bar;

#define CUSTOM (cfg.types[mode].custom)

static Boolean in_type(short k, short t)
{
    short j;
    for (j = 0; j < cfg.comps[k].ntypes; j++) if (cfg.comps[k].types[j] == t) return true;
    return false;
}

/* Whether the type shown installs component k */
static Boolean wanted(short k)
{
    if (!in_type(k, mode)) return false;
    return CUSTOM ? comps[k].checked : true;
}

/* The disk's free space and what's picked needs; Install is dim if nothing is picked or there's no room */
static void count_space(void)
{
    HParamBlockRec pb;
    Str255 vn;
    long need = 0;
    short i;
    Boolean any = false;
    for (i = 0; i < cfg.ncomps; i++) if (wanted(i)) { need += comps[i].disk; any = true; }
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioNamePtr = vn;
    pb.volumeParam.ioVRefNum = place_vol;
    if (PBHGetVInfoSync(&pb) == noErr) {
        avail_k = ((long)(unsigned short)pb.volumeParam.ioVFrBlk * (pb.volumeParam.ioVAlBlkSiz / 512)) / 2;
        pcopy(place_vol_name, vn);
    }
    need_k = (need + 1023) / 1024;
    if (stage == S_INSTALL) HiliteControl(c_primary, any && need_k <= avail_k ? 0 : 255);
}

static void folder_name(Str255 n)
{
    if (place_found) {
        CInfoPBRec c;
        memset(&c, 0, sizeof c);
        c.dirInfo.ioNamePtr = n;
        c.dirInfo.ioVRefNum = place_vol;
        c.dirInfo.ioDrDirID = place_found;
        c.dirInfo.ioFDirIndex = -1;
        if (PBGetCatInfoSync(&c) == noErr) return;
    }
    pcopy(n, place_name);
}

/* A checkbox: 0 empty, 1 checked (an X), 2 some of a group checked (a dash) */
static void draw_check(short left, short top_, short state)
{
    Rect b;
    SetRect(&b, left, top_, left + 12, top_ + 12);
    FrameRect(&b);
    if (state == 1) { MoveTo(b.left + 2, b.top + 2); LineTo(b.right - 3, b.bottom - 3); MoveTo(b.right - 3, b.top + 2); LineTo(b.left + 2, b.bottom - 3); }
    else if (state == 2) { Rect dash; SetRect(&dash, b.left + 3, b.top + 5, b.right - 3, b.top + 7); PaintRect(&dash); }
}

/* The custom list's rows for the type shown: its components, with a heading where the group changes */
static void make_rows(void)
{
    short k, group = -1;
    nrows = 0;
    headings_boxed = false;
    for (k = 0; k < cfg.ncomps; k++) {
        if (!in_type(k, mode)) continue;
        if (cfg.comps[k].group >= 0 && cfg.comps[k].group != group) {
            rows[nrows].comp = -1;
            rows[nrows++].group = (short)cfg.comps[k].group;
            if (cfg.groups[cfg.comps[k].group].selectable) headings_boxed = true;
        }
        group = (short)cfg.comps[k].group;
        rows[nrows].comp = k;
        rows[nrows++].group = group;
    }
}

/* A group's box, in the type shown: 0 none checked, 1 all, 2 some */
static short group_state(short g)
{
    short k, on = 0, n = 0;
    for (k = 0; k < cfg.ncomps; k++)
        if (cfg.comps[k].group == g && in_type(k, mode)) { n++; if (comps[k].checked) on++; }
    return on == 0 ? 0 : on == n ? 1 : 2;
}

static short list_max(void)
{
    short max = nrows - (w2_list.bottom - w2_list.top) / ROW;
    return max < 0 ? 0 : max;
}

static void draw_list(void)
{
    short i, y;
    RgnHandle save = NewRgn();
    GetClip(save);
    ClipRect(&w2_list);
    small_font();
    for (i = top; i < nrows && (y = w2_list.top + (i - top) * ROW) < w2_list.bottom; i++) {
        Str255 s;
        Rect row;
        const EupComp *m;
        SetRect(&row, w2_list.left, y, w2_list.right, y + ROW);
        EraseRect(&row);            /* each row whole, so drawing it twice (an update after a redraw) can't undo the inverting */
        if (rows[i].comp < 0) {
            const EupGroup *g = &cfg.groups[rows[i].group];
            if (g->selectable) draw_check(row.left + 6, y + 2, group_state(rows[i].group));
            MoveTo(row.left + (headings_boxed ? 22 : 6), y + 12);
            DrawString(g->label);
            continue;
        }
        m = &cfg.comps[rows[i].comp];
        draw_check(row.left + 22 + (headings_boxed ? 16 : 0), y + 2, comps[rows[i].comp].checked);
        MoveTo(row.left + 40 + (headings_boxed ? 16 : 0), y + 12);
        DrawString(m->label);
        number_k(s, (comps[rows[i].comp].disk + 1023) / 1024);
        MoveTo(row.right - 8 - StringWidth(s), y + 12);
        DrawString(s);
        if (i == sel) InvertRect(&row);
    }
    SetClip(save);
    DisposeRgn(save);
}

static void draw_install(void)
{
    Rect frame, lab;
    Str255 name, s, a, b;
    FrameRect(&w2_popup);                                           /* the pop-up menu */
    MoveTo(w2_popup.left + 2, w2_popup.bottom); LineTo(w2_popup.right, w2_popup.bottom); LineTo(w2_popup.right, w2_popup.top + 2);
    big_font();
    MoveTo(w2_popup.left + 8, w2_popup.top + 15);
    draw_fit(cfg.types[mode].label, w2_popup.right - w2_popup.left - 34);
    {                                   /* the pop-up arrow: a solid triangle, rows 11, 9, 7, 5, 3, 1 wide */
        short k, cx = w2_popup.right - 17;
        Rect row;
        for (k = 0; k < 6; k++) {
            SetRect(&row, cx - 5 + k, w2_popup.top + 7 + k, cx + 6 - k, w2_popup.top + 8 + k);
            PaintRect(&row);
        }
    }
    if (CUSTOM) { FrameRect(&w2_box); draw_list(); }            /* the box: the description, or the list */
    else tb_draw(&tb_type);
    small_font();
    MoveTo(w2_box.left, w2_box.bottom + 14);                        /* what the selected item is */
    if (note[0]) draw_fit(note, 416);
    else if (CUSTOM && sel >= 0) draw_fit(cfg.comps[rows[sel].comp].description, 416);
    number_k(a, avail_k);
    number_k(b, need_k);
    fmt(s, STR_SPACE, a, b);
    MoveTo(w2_box.left, w2_box.bottom + 30);
    DrawString(s);
    SetRect(&frame, w2_box.left, w2_box.bottom + 58, 428, w2_box.bottom + 90);      /* where it goes */
    FrameRect(&frame);
    big_font();
    GetIndString(s, 128, STR_LOCATION);
    {
        short w = StringWidth(s);       /* the gap in the line: 5 pixels each side of the text */
        SetRect(&lab, frame.left + 12, frame.top, frame.left + 12 + w + 10, frame.top + 1);
        EraseRect(&lab);
        MoveTo(lab.left + 5, frame.top + 5);
        DrawString(s);
    }
    small_font();
    folder_name(name);
    fmt(s, STR_ITEMS_GO, name, place_vol_name);
    MoveTo(frame.left + 8, frame.top + 20);
    draw_fit(s, 330 - 8);
    big_font();
}

static void draw_screen(void)
{
    Str255 s;
    tb_draw(&tb_screen);
    if (stage == S_LICENSE) {
        small_font();
        GetIndString(s, 128, STR_AGREE_NOTE);
        MoveTo(MARGIN, 236);
        draw_fit(s, 240);
        big_font();
    }
}

static pascal void draw_all(DialogPtr d, short item)
{
    (void)item;
    if (stage == S_INSTALL) draw_install(); else draw_screen();
    ring(c_primary);
    DrawControls(d);
}

static void redraw(DialogPtr d)
{
    Rect r = d->portRect;
    count_space();
    EraseRect(&r);
    draw_all(d, CANVAS);
}

static void place_control(DialogPtr d, short item, short top_, short left, short bottom, short right, short title, Boolean show)
{
    ControlHandle c = item == B_PRIMARY ? c_primary : item == B_QUIT ? c_quit : c_choose;
    Str255 t;
    short type;
    Handle h;
    Rect r;
    HideControl(c);
    GetDialogItem(d, item, &type, &h, &r);
    if (show) SetRect(&r, left, top_, right, bottom);
    else SetRect(&r, -100, -100, -80, -80);             /* the dialog's item goes with it, so a click can't find it */
    SetDialogItem(d, item, type, h, &r);
    if (!show) return;
    MoveControl(c, left, top_);
    SizeControl(c, right - left, bottom - top_);
    GetIndString(t, 128, title);
    SetControlTitle(c, t);
    HiliteControl(c, 0);
    ShowControl(c);
}

/* Which controls show, and where, for a stage */
static void set_stage(DialogPtr d, short st)
{
    stage = st;
    tb_show(&tb_type, st == S_INSTALL && !CUSTOM);
    tb_show(&tb_screen, false);
    if (st == S_INSTALL && CUSTOM) ShowControl(w2_bar); else HideControl(w2_bar);
    if (st == S_INSTALL) {
        place_control(d, B_PRIMARY, 12, 348, 34, 428, STR_INSTALL, true);
        place_control(d, B_QUIT, 46, 348, 68, 428, STR_QUIT, true);
        place_control(d, B_CHOOSE, 220, 342, 240, 422, STR_CHOOSE, true);
    } else {
        tb_set_file(&tb_screen, st == S_DESC ? &cfg.description_screen : &cfg.license_screen);
        tb_show(&tb_screen, true);
        place_control(d, B_PRIMARY, 222, 348, 244, 428, st == S_DESC ? STR_CONTINUE : STR_AGREE, true);
        place_control(d, B_QUIT, 222, 258, 244, 338, st == S_DESC ? STR_QUIT : STR_DISAGREE, true);
        place_control(d, B_CHOOSE, 0, 0, 0, 0, 0, false);
    }
    redraw(d);
}

static void set_top(DialogPtr d, short to)
{
    short max = list_max();
    if (to > max) to = max;
    if (to < 0) to = 0;
    if (to == top) return;
    top = to;
    SetControlValue(w2_bar, to);
    redraw(d);
}

static DialogPtr bar_dialog;
static pascal void w2_track(ControlHandle c, short part)
{
    short page = (w2_list.bottom - w2_list.top) / ROW - 1;
    (void)c;
    if (part == inUpButton) set_top(bar_dialog, top - 1);
    else if (part == inDownButton) set_top(bar_dialog, top + 1);
    else if (part == inPageUp) set_top(bar_dialog, top - page);
    else if (part == inPageDown) set_top(bar_dialog, top + page);
}

static void set_mode(DialogPtr d, short m)
{
    const EupType *t;
    note[0] = 0;
    mode = m;
    sel = -1;
    top = 0;
    t = &cfg.types[mode];
    if (CUSTOM) {
        make_rows();
        SetControlMaximum(w2_bar, list_max());
        SetControlValue(w2_bar, 0);
        HiliteControl(w2_bar, list_max() ? 0 : 255);
    } else if (t->description_file.n) tb_set_file(&tb_type, &t->description_file);
    else if (t->description) tb_set(&tb_type, (Ptr)t->description + 1, t->description[0]);
    else tb_set(&tb_type, (Ptr)"", 0);
    if (stage == S_INSTALL) {
        if (CUSTOM) ShowControl(w2_bar); else HideControl(w2_bar);
        tb_show(&tb_type, !CUSTOM);
        redraw(d);
    }
}

static void choose_place(void)
{
    SFReply reply;
    Point where;
    WDPBRec wd;
    Str255 prompt;
    long dir;
    where.h = 90;
    where.v = 70;
    fmt(prompt, STR_INSTALL_IN, cfg.product, NULL);
    SFPutFile(where, prompt, place_name, NULL, &reply);
    if (!reply.good) return;
    memset(&wd, 0, sizeof wd);
    wd.ioVRefNum = reply.vRefNum;
    if (PBGetWDInfoSync(&wd) != noErr) { place_vol = reply.vRefNum; dir = 2; } else { place_vol = wd.ioWDVRefNum; dir = wd.ioWDDirID; }
    place_parent = dir;
    /* the folder shown holds the detect file: an earlier install, used as it is */
    place_found = cfg.detect && what_is(place_vol, dir, cfg.detect) == 1 ? dir : 0;
    pcopy(place_name, reply.fName);
}

/* The folder to install in, made if need be */
static OSErr resolve_place(void)
{
    place_vref = place_vol;
    if (place_found) { place_dir = place_found; return noErr; }
    return dir_of(place_vol, place_parent, place_name, &place_dir);
}

/* The progress window: one plain line, and the bar */
static pascal void prog_draw(DialogPtr d, short item);

static void open_progress(void)
{
    short type;
    Handle h;
    Rect r;
    prog_dlg = GetNewDialog(PROG, NULL, (WindowPtr)-1);
    if (!prog_dlg) return;
    SetWTitle(prog_dlg, cfg.title);
    GetDialogItem(prog_dlg, 1, &type, &h, &r);
    c_cancel = (ControlHandle)h;
    GetDialogItem(prog_dlg, 2, &type, &h, &r);
    SetDialogItem(prog_dlg, 2, type, (Handle)(UserItemUPP)prog_draw, &r);
    SetRect(&prog_bar, 12, 44, 288, 60);
    prog_line[0] = 0;
    prog_over = false;
    prog_drawn = 0;
    ShowWindow(prog_dlg);
    SetPort(prog_dlg);
    DrawDialog(prog_dlg);
}

static pascal Boolean prog_filter(DialogPtr d, EventRecord *e, short *item)
{
    (void)d;
    if (e->what == keyDown || e->what == autoKey) {
        char c = e->message & charCodeMask;
        if (c == 13 || c == 3) { *item = 1; return true; }
    }
    return false;
}

static void prog_text(ConstStr255Param t)
{
    Rect r;
    GrafPtr save;
    GetPort(&save);
    SetPort(prog_dlg);
    SetRect(&r, 12, 12, 288, prog_over ? 46 : 42);    /* above the bar, which starts at 44 */
    EraseRect(&r);
    big_font();
    MoveTo(12 + (276 - StringWidth(t)) / 2, prog_over ? 42 : 28);
    DrawString(t);
    if (t != prog_line) pcopy(prog_line, t);
    SetPort(save);
}

static pascal void prog_draw(DialogPtr d, short item)
{
    Rect fill;
    (void)item;
    if (!prog_over) {
        FrameRect(&prog_bar);
        fill = prog_bar;
        InsetRect(&fill, 2, 2);
        fill.right = fill.left + prog_drawn;
        PaintRect(&fill);
    }
    ring(c_cancel);
    DrawControls(d);
    prog_text(prog_line);
}

/* The end, in the same window: a message and one button (Quit, or OK for an error) until it's pressed */
static void prog_finish(ConstStr255Param text, short button)
{
    short item;
    Str255 t;
    Rect r;
    GetIndString(t, 128, button);
    SetControlTitle(c_cancel, t);
    prog_over = true;
    SetPort(prog_dlg);
    r = prog_dlg->portRect;
    EraseRect(&r);
    pcopy(prog_line, text);
    prog_draw(prog_dlg, 2);
    InitCursor();
    do ModalDialog((ModalFilterUPP)prog_filter, &item); while (item != 1);
}

static void close_progress(void)
{
    if (prog_dlg) DisposeDialog(prog_dlg);
    prog_dlg = NULL;
}

/* An old name from an earlier version, gone if it's there (a folder only if it's empty) */
static Boolean removing;

static OSErr retire(const EupPath *p)
{
    short k;
    long dir = place_dir;
    for (k = 0; k < p->n - 1; k++) if (find_dir(place_vref, dir, p->names[k], &dir) != noErr) return noErr;
    if (!what_is(place_vref, dir, p->names[p->n - 1])) return noErr;
    if (poll_cancel()) return userCanceledErr;
    if (!removing) {
        Str255 s;
        removing = 1;
        GetIndString(s, 128, STR_REMOVING);
        prog_text(s);
    }
    remove_file(place_vref, dir, p->names[p->n - 1]);
    return noErr;
}

static OSErr run_install(void)
{
    short i, k;
    OSErr err = noErr;
    size_window(place_vref, place_dir);
    removing = false;
    prog_done = 0;
    prog_drawn = 0;
    prog_total = 0;
    for (i = 0; i < cfg.ncomps; i++) if (wanted(i)) prog_total += comps[i].unpacked;
    /* names from earlier versions (a type that keeps old files leaves what's there alone) */
    if (cfg.types[mode].remove_old)
        for (i = 0; i < cfg.nremove && !err; i++) err = retire(&cfg.remove[i]);
    if (err) return err;
    for (i = 0; i < cfg.ncomps && !err; i++) {
        Str255 s;
        long dir = place_dir;
        if (!wanted(i)) continue;
        if (poll_cancel()) return userCanceledErr;
        fmt(s, STR_INSTALLING, cfg.comps[i].label, NULL);
        prog_text(s);
        for (k = 0; k < cfg.comps[i].destination.n && !err; k++) err = into(&dir, cfg.comps[i].destination.names[k]);
        for (k = 0; k < comps[i].n && !err; k++) err = install_record(i, comps[i].recs[k]);
    }
    FlushVol(NULL, place_vref);
    return err;
}

/* Clicks on what the program draws: the pop-up menu, the list */
static Boolean install_click(DialogPtr d, Point p)
{
    if (PtInRect(p, &w2_popup)) {
        Point g;
        long c;
        short i;
        g.v = w2_popup.top;
        g.h = w2_popup.left;
        LocalToGlobal(&g);
        InsertMenu(popup, -1);
        for (i = 0; i < cfg.ntypes; i++) CheckItem(popup, i + 1, mode == i);
        c = PopUpMenuSelect(popup, g.v, g.h, mode + 1);
        DeleteMenu(200);
        if (HiWord(c) && LoWord(c) - 1 != mode) set_mode(d, LoWord(c) - 1);
        return true;
    }
    if (CUSTOM && PtInRect(p, &w2_list)) {
        short i = top + (p.v - w2_list.top) / ROW, box = w2_list.left + (headings_boxed ? 16 : 0);
        if (i < nrows && rows[i].comp >= 0) {
            Comp *c = &comps[rows[i].comp];
            note[0] = 0;
            if (p.h < box + 38) c->checked = !c->checked;
            sel = i;
            redraw(d);
        } else if (i < nrows && cfg.groups[rows[i].group].selectable && p.h < w2_list.left + 20) {
            /* a group's box: all of it off if it's all on, else all of it on */
            short k, g = rows[i].group, on = group_state(g) != 1;
            for (k = 0; k < cfg.ncomps; k++) if (cfg.comps[k].group == g && in_type(k, mode)) comps[k].checked = on;
            note[0] = 0;
            redraw(d);
        }
        return true;
    }
    return false;
}

static Point click_pt;

static pascal Boolean filter(DialogPtr d, EventRecord *e, short *item)
{
    if (e->what == keyDown || e->what == autoKey) {
        char c = e->message & charCodeMask;
        if ((c == 13 || c == 3) && (*c_primary)->contrlVis && (*c_primary)->contrlHilite != 255) { *item = B_PRIMARY; return true; }
    }
    if (e->what == mouseDown) {
        Point p = e->where;
        ControlHandle c;
        short part;
        WindowPtr w;
        Rect limit = qd.screenBits.bounds;
        if (FindWindow(p, &w) == inDrag && w == (WindowPtr)d) {      /* it moves by its title bar */
            InsetRect(&limit, 4, 4);
            DragWindow(w, p, &limit);
            click_pt.h = click_pt.v = -32000;      /* nothing of what's drawn was clicked */
            *item = CANVAS;
            return true;
        }
        SetPort(d);
        GlobalToLocal(&p);
        part = FindControl(p, d, &c);
        if (part && (c == tb_type.bar || c == tb_screen.bar)) {
            tracking = c == tb_type.bar ? &tb_type : &tb_screen;
            if (part == inThumb) { TrackControl(c, p, NULL); tb_scroll(tracking, GetControlValue(c)); }
            else TrackControl(c, p, (ControlActionUPP)tb_track);
            click_pt.h = click_pt.v = -32000;
            *item = CANVAS;
            return true;
        }
        if (part && stage == S_INSTALL && c == w2_bar) {
            bar_dialog = d;
            if (part == inThumb) { TrackControl(c, p, NULL); set_top(d, GetControlValue(c)); }
            else TrackControl(c, p, (ControlActionUPP)w2_track);
            click_pt.h = click_pt.v = -32000;
            *item = CANVAS;
            return true;
        }
        click_pt = p;       /* a click on what's drawn comes back as CANVAS, with the button still down */
    }
    return false;
}

static void back_to_choices(DialogPtr d)
{
    close_progress();
    ShowWindow(d);
    SetPort(d);
    set_stage(d, S_INSTALL);
}

int main(void)
{
    DialogPtr d;
    short item, type, i, w;
    Handle h;
    Rect r;
    Boolean done = false;
    SysEnvRec env;
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
    if (!load()) { SysBeep(10); return 1; }      /* not made by the packager, or damaged */
    if (!(d = GetNewDialog(WIN, NULL, (WindowPtr)-1))) return 1;
    SetWTitle(d, cfg.title);
    SetPort(d);
    c_primary = (GetDialogItem(d, B_PRIMARY, &type, &h, &r), (ControlHandle)h);
    c_quit = (GetDialogItem(d, B_QUIT, &type, &h, &r), (ControlHandle)h);
    c_choose = (GetDialogItem(d, B_CHOOSE, &type, &h, &r), (ControlHandle)h);
    GetDialogItem(d, CANVAS, &type, &h, &r);
    SetDialogItem(d, CANVAS, type, (Handle)(UserItemUPP)draw_all, &r);
    tb_new(d, &tb_type, MARGIN, 44, MARGIN + 300, 156);
    tb_new(d, &tb_screen, MARGIN, MARGIN, 428, 206);
    /* the choices */
    SysEnvirons(1, &env);
    place_vol = env.sysVRefNum;
    place_parent = 2;
    place_found = 0;
    pcopy(place_name, cfg.folder);
    SetRect(&w2_box, MARGIN, 44, MARGIN + 300, 156);
    w2_list = w2_box;
    InsetRect(&w2_list, 1, 1);
    w2_list.right = w2_box.right - BAR;
    SetRect(&r, w2_box.right - BAR, w2_box.top, w2_box.right, w2_box.bottom);
    w2_bar = NewControl(d, &r, "\p", false, 0, 0, 0, scrollBarProc, 0);
    popup = NewMenu(200, "\p");
    big_font();
    w = 140 - 34;               /* the pop-up: as wide as its longest label */
    for (i = 0; i < cfg.ntypes; i++) {
        AppendMenu(popup, "\px");
        SetMenuItemText(popup, i + 1, cfg.types[i].label);
        if (StringWidth(cfg.types[i].label) > w) w = StringWidth(cfg.types[i].label);
    }
    if (w > 300 - 34) w = 300 - 34;
    SetRect(&w2_popup, MARGIN, MARGIN, MARGIN + w + 34, MARGIN + 20);
    stage = S_DESC;
    set_mode(d, 0);
    ShowWindow(d);
    set_stage(d, cfg.description_screen.n ? S_DESC : cfg.license_screen.n ? S_LICENSE : S_INSTALL);
    while (!done) {
        ModalDialog((ModalFilterUPP)filter, &item);
        if (item == B_QUIT) done = true;
        else if (item == CANVAS) { if (stage == S_INSTALL) install_click(d, click_pt); }
        else if (item == B_CHOOSE) { choose_place(); note[0] = 0; redraw(d); }
        else if (item == B_PRIMARY) {
            if (stage == S_DESC) set_stage(d, cfg.license_screen.n ? S_LICENSE : S_INSTALL);
            else if (stage == S_LICENSE) set_stage(d, S_INSTALL);
            else {
                OSErr err;
                Str255 s;
                SetCursor(*GetCursor(watchCursor));
                HideWindow(d);
                open_progress();
                if ((err = resolve_place()) != noErr) {
                    GetIndString(s, 128, STR_FOLDER_FAILED);
                    prog_finish(s, STR_OK);
                    back_to_choices(d);
                    continue;
                }
                err = run_install();
                InitCursor();
                note[0] = 0;
                if (err == userCanceledErr) {
                    GetIndString(note, 128, STR_CANCELED);
                    back_to_choices(d);
                } else if (err) {
                    Str255 n;
                    NumToString(err, n);
                    fmt(s, STR_FAILED, n, NULL);
                    prog_finish(s, STR_OK);
                    back_to_choices(d);
                } else {
                    fmt(s, STR_INSTALLED, cfg.product, NULL);
                    prog_finish(s, STR_QUIT);
                    close_progress();
                    done = true;
                }
            }
        }
    }
    close_fork(payload);        /* System 6 leaves a file open when the program quits: the Finder would find it busy */
    return 0;
}
