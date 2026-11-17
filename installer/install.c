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
enum { PROG = 130, WIN = 128, ABOUT = 132, B_PRIMARY = 1, B_QUIT, B_CHOOSE, CANVAS };
enum { M_APPLE = 128, M_FILE, M_EDIT };
enum { S_DESC, S_LICENSE, S_INSTALL };

/* EUP's own strings: 'STR#' 128 */
enum { STR_INSTALL = 1, STR_QUIT, STR_CHOOSE, STR_OK, STR_CONTINUE, STR_AGREE, STR_DISAGREE, STR_SPACE, STR_LOCATION,
       STR_ITEMS_GO, STR_INSTALL_IN, STR_INSTALLED, STR_CANCELED, STR_FAILED, STR_FOLDER_FAILED, STR_INSTALLING,
       STR_REMOVING, STR_AGREE_NOTE, STR_K, STR_ABOUT, STR_IN_USE, STR_NOT_REPLACED, STR_REQUIRED };

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

static void put_folder(short vref, long parent, ConstStr255Param name, const Point *loc, const Rect *window, Boolean made);

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
    if (err == noErr) {
        *dir = h.fileParam.ioDirID;
        put_folder(vref, parent, name, NULL, NULL, true);
        return noErr;
    }
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

static OSErr remove_file(short vref, long dir, ConstStr255Param name)
{
    HParamBlockRec h;
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = (StringPtr)name;
    h.fileParam.ioVRefNum = vref;
    h.fileParam.ioDirID = dir;
    return PBHDeleteSync(&h);
}

static OSErr rename_file(short vref, long dir, ConstStr255Param from, ConstStr255Param to)
{
    HParamBlockRec h;
    memset(&h, 0, sizeof h);
    h.ioParam.ioNamePtr = (StringPtr)from;
    h.ioParam.ioVRefNum = vref;
    h.fileParam.ioDirID = dir;
    h.ioParam.ioMisc = (Ptr)to;
    return PBHRenameSync(&h);
}

/* A file that's open, by an application that's running or one that has it open, can't be deleted: PBHDelete
   returns fBsyErr (Inside Macintosh: Files). So before anything is written, the install looks for one in its
   way, by ioFlAttrib bit 7, set while a file is open (Inside Macintosh IV, File Manager). The file it stopped
   at, and whether it's an application: */
static Str255 fail_name;
static Boolean fail_app;

static Boolean is_open(short vref, long dir, ConstStr255Param name)
{
    CInfoPBRec c;
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = (StringPtr)name;
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = dir;
    if (PBGetCatInfoSync(&c) != noErr || (c.hFileInfo.ioFlAttrib & 16) || !(c.hFileInfo.ioFlAttrib & 0x80)) return false;
    pcopy(fail_name, name);
    fail_app = c.hFileInfo.ioFlFndrInfo.fdType == 'APPL';
    return true;
}

/* The name with n tildes in front, cut to 31 characters: ~Name while the file is written, ~~Name for the old
   file while it's replaced */
static void tilde(ConstStr255Param name, short n, Str255 out)
{
    short k = name[0] > 31 - n ? 31 - n : name[0];
    out[0] = (unsigned char)(k + n);
    memset(out + 1, '~', n);
    memcpy(out + 1 + n, name + 1, k);
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
    tilde(o->name, 1, o->tmp);
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

/* Done with it: whole, it gets its type, creator, and flags and its name, replacing the old file; else it's
   deleted. The old file goes aside as ~~Name, the new one takes its name, and then the old one is deleted; if a
   step fails, the old file is put back as it was and the new one deleted, and the error returned. (An open file
   can be renamed but not deleted, so one opened since the install began is found here.) */
static OSErr end_file(Out *o, Boolean whole)
{
    HParamBlockRec h;
    Str255 old;
    OSErr err;
    short v = o->vref;
    long d = o->dir;
    if (o->df) close_fork(o->df);
    if (o->rf) close_fork(o->rf);
    o->df = o->rf = 0;
    if (!o->opened) return noErr;
    if (!whole) { remove_file(v, d, o->tmp); return noErr; }
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
    switch (what_is(v, d, o->name)) {
    case 0:
        if ((err = rename_file(v, d, o->tmp, o->name)) != noErr) remove_file(v, d, o->tmp);
        return err;
    case 2:                                         /* a folder in the way */
        remove_file(v, d, o->tmp);
        return dupFNErr;
    }
    tilde(o->name, 2, old);
    remove_file(v, d, old);
    if ((err = rename_file(v, d, o->name, old)) != noErr) {
        remove_file(v, d, o->tmp);
        return err;
    }
    if ((err = rename_file(v, d, o->tmp, o->name)) != noErr) {
        rename_file(v, d, old, o->name);
        remove_file(v, d, o->tmp);
        return err;
    }
    if ((err = remove_file(v, d, old)) != noErr) {
        rename_file(v, d, o->name, o->tmp);
        rename_file(v, d, old, o->name);
        remove_file(v, d, o->tmp);
        return err;
    }
    return noErr;
}

/* ---- windows, menus, and events ---- */

/* A window in the middle of the main screen, below the menu bar: screenBits.bounds is the main screen
   (Inside Macintosh I, QuickDraw), and System 6 has no automatic positions for 'DLOG' */
enum { MENU_BAR = 20, TITLE_BAR = 19 };

static void center(WindowPtr w)
{
    Rect s = qd.screenBits.bounds;
    short wd = w->portRect.right - w->portRect.left, ht = w->portRect.bottom - w->portRect.top;
    short v = s.top + MENU_BAR + (s.bottom - s.top - MENU_BAR - TITLE_BAR - ht) / 2 + TITLE_BAR;
    if (v < s.top + MENU_BAR + TITLE_BAR) v = s.top + MENU_BAR + TITLE_BAR;
    MoveWindow(w, s.left + (s.right - s.left - wd) / 2, v, false);
}

static void ring(ControlHandle c);

static Boolean quit_asked;          /* File > Quit: at once, or as a Cancel while installing */
static Boolean busy;                /* installing: the watch cursor */
static Boolean edit_on;

static void about(void)
{
    Str255 s;
    Handle h = Get1Resource('vers', 1);         /* the INI's get_info, after the short version */
    DialogPtr d;
    short item, type;
    Rect r;
    if (h && GetHandleSize(h) > 7) {
        HLock(h);
        pcopy(s, (unsigned char *)*h + 7 + (unsigned char)(*h)[6]);
        HUnlock(h);
    } else pcopy(s, cfg.title);
    ParamText(s, "\p", "\p", "\p");
    if (!(d = GetNewDialog(ABOUT, NULL, (WindowPtr)-1))) return;
    center(d);
    ShowWindow(d);
    SetPort(d);
    DrawDialog(d);
    GetDialogItem(d, 1, &type, &h, &r);
    ring((ControlHandle)h);
    InitCursor();
    do ModalDialog(NULL, &item); while (item != 1);
    DisposeDialog(d);
    if (busy) SetCursor(*GetCursor(watchCursor));
}

/* Edit is for a desk accessory in front */
static void adjust_menus(void)
{
    WindowPtr w = FrontWindow();
    Boolean on = w && ((WindowPeek)w)->windowKind < 0;
    if (on == edit_on) return;
    edit_on = on;
    if (on) EnableItem(GetMenuHandle(M_EDIT), 0); else DisableItem(GetMenuHandle(M_EDIT), 0);
    DrawMenuBar();
}

static void do_menu(long choice)
{
    short m = HiWord(choice), item = LoWord(choice);
    if (m == M_APPLE) {
        if (item == 1) about();
        else {
            Str255 da;
            GrafPtr save;
            GetPort(&save);
            GetMenuItemText(GetMenuHandle(M_APPLE), item, da);
            OpenDeskAcc(da);
            SetPort(save);
        }
    } else if (m == M_FILE) quit_asked = true;
    else if (m == M_EDIT) SystemEdit(item - 1);
    HiliteMenu(0);
}

static void set_menus(void)
{
    Str255 name, s;
    short ref;
    Handle h;
    MenuHandle apple;
    SetMenuBar(GetNewMBar(128));
    apple = GetMenuHandle(M_APPLE);
    GetAppParms(name, &ref, &h);
    fmt(s, STR_ABOUT, name, NULL);
    SetMenuItemText(apple, 1, s);
    AppendResMenu(apple, 'DRVR');
    DrawMenuBar();
}

typedef pascal Boolean (*Filter)(DialogPtr d, EventRecord *e, short *item);

enum { suspend_resume = 1, resume_flag = 1 };

/* One event, handled the way an application with a modeless dialog handles it (Inside Macintosh I, Dialog
   Manager: IsDialogEvent and DialogSelect), so the menus work, desk accessories and other windows are
   redrawn, and under MultiFinder and System 7 the user can switch to other applications; on a suspend or
   resume event the window is made inactive or active (Programmer's Guide to MultiFinder). True if an item
   of d was hit: *item. The filter, if any, sees d's clicks and keys first, as ModalDialog's would. */
static Boolean dialog_event(EventRecord *e, DialogPtr d, Filter filt, short *item)
{
    WindowPtr w;
    DialogPtr hit;
    switch (e->what) {
    case mouseDown:
        switch (FindWindow(e->where, &w)) {
        case inMenuBar: adjust_menus(); do_menu(MenuSelect(e->where)); return false;
        case inSysWindow: SystemClick(e, w); return false;
        case inDrag:
            if (w == (WindowPtr)d && w == FrontWindow()) {
                Rect limit = qd.screenBits.bounds;
                InsetRect(&limit, 4, 4);
                DragWindow(w, e->where, &limit);
                return false;
            }
            /* else as a click in it */
        case inContent:
            if (w != FrontWindow()) { SelectWindow(w); return false; }
            if (w != (WindowPtr)d) return false;
            break;
        default: return false;
        }
        break;
    case keyDown:
    case autoKey:
        if (e->modifiers & cmdKey) {
            long m;
            adjust_menus();
            if (HiWord(m = MenuKey(e->message & charCodeMask))) { do_menu(m); return false; }
        }
        if (FrontWindow() != (WindowPtr)d) return false;
        break;
    case osEvt:
        if (((e->message >> 24) & 0xFF) == suspend_resume) {
            Boolean on = (e->message & resume_flag) != 0;
            if ((w = FrontWindow())) HiliteWindow(w, on);
            if (on) { if (busy) SetCursor(*GetCursor(watchCursor)); else InitCursor(); }
        }
        return false;
    default:
        break;
    }
    if (filt && (e->what == mouseDown || e->what == keyDown || e->what == autoKey) && filt(d, e, item)) return true;
    return IsDialogEvent(e) && DialogSelect(e, &hit, item) && hit == d;
}

/* Events until an item of d is hit (true), or File > Quit (false) */
static Boolean next_item(DialogPtr d, Filter filt, short *item)
{
    EventRecord e;
    while (!quit_asked) {
        SystemTask();
        if (GetNextEvent(everyEvent, &e) && dialog_event(&e, d, filt, item)) {
            SetPort(d);
            return true;
        }
    }
    return false;
}

/* ---- the progress window, and Cancel: a click on the button, Escape, Command-period, or Quit ---- */

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

/* Every event while installing, so other windows and applications go on; true for a Cancel */
static Boolean poll_cancel(void)
{
    EventRecord e;
    short item;
    SystemTask();
    while (!quit_asked && GetNextEvent(everyEvent, &e)) {
        if ((e.what == keyDown || e.what == autoKey) && FrontWindow() == (WindowPtr)prog_dlg) {
            char ch = e.message & charCodeMask;
            if (ch == 27 || (ch == '.' && (e.modifiers & cmdKey))) return true;
        }
        if (prog_dlg && dialog_event(&e, prog_dlg, NULL, &item) && item == 1) return true;
    }
    return quit_asked;
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

/* A folder's Finder information (DInfo): its place in its parent's window, and its own window. The Finder
   keeps a place only with "has been inited" (0x0100) set; with it clear, it puts the item in the first empty
   space in the window and sets it (Technical Note TB 42, "Finder Icon Positioning and File Initialization").
   So the flag is set only with a place. A folder the installer made opens by Icon: frView is "set by the
   user with commands from the View menu" (Inside Macintosh: Macintosh Toolbox Essentials, Finder
   Interface, DInfo), with no values given; 0x0100 is what the Finder of System 6.0.8 and 7.0.1 writes in a
   new folder, which opens by Icon, and 0 opens by Small Icon. A folder that was there keeps its view.
   A folder the installer made also gets a window (frRect, "the rectangle of the window that the Finder displays
   when the user opens the folder"). The Finder of System 6 gives an unplaced folder a window of its own, but
   keeps a placed one's as it is: with none, it lays out the folder's icons one under another. The window is
   its parent's moved down and right, as the Finder opens one, or, if that's empty or off the screen, the
   window the Finder of 6.0.8 gives a new folder on a Mac Plus. */
enum { INITED = 0x0100, VIEW_BY_ICON = 0x0100, INVISIBLE = 0x4000, ON_DESK = 0x0001, STAGGER = 20 };

static void new_window(short vref, long parent, Rect *r)
{
    CInfoPBRec c;
    Rect s = qd.screenBits.bounds;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = parent;
    c.dirInfo.ioFDirIndex = -1;
    if (PBGetCatInfoSync(&c) == noErr) {
        *r = c.dirInfo.ioDrUsrWds.frRect;
        OffsetRect(r, STAGGER, STAGGER);
        if (r->right - r->left >= 120 && r->bottom - r->top >= 80 && r->left >= s.left && r->top >= s.top + 40 &&
            r->right <= s.right && r->bottom <= s.bottom)
            return;
    }
    SetRect(r, 24, 82, 428, 270);
}

static void put_folder(short vref, long parent, ConstStr255Param name, const Point *loc, const Rect *window, Boolean made)
{
    CInfoPBRec c;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioNamePtr = (StringPtr)name;
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = parent;
    if (PBGetCatInfoSync(&c) != noErr) return;
    if (loc) {
        c.dirInfo.ioDrUsrWds.frLocation = *loc;
        c.dirInfo.ioDrUsrWds.frFlags |= INITED;
    }
    if (window) c.dirInfo.ioDrUsrWds.frRect = *window;
    else if (made) new_window(vref, parent, &c.dirInfo.ioDrUsrWds.frRect);
    if (made) c.dirInfo.ioDrUsrWds.frView = VIEW_BY_ICON;
    c.dirInfo.ioNamePtr = (StringPtr)name;
    c.dirInfo.ioDrDirID = parent;
    PBSetCatInfoSync(&c);
}

/* A free place in a folder's window, as the Finder would find one: the first space, row by row on a 64-pixel
   grid across the window, that no placed icon is in */
enum { GRID = 64, MOST_PLACED = 200 };

static Boolean free_place(short vref, long parent, Point *loc)
{
    Point *at;
    short n = 0, i, width = 400, h, v;
    Str255 name;
    CInfoPBRec c;
    if (!(at = (Point *)NewPtr(sizeof(Point) * MOST_PLACED))) return false;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioNamePtr = name;
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = parent;
    c.dirInfo.ioFDirIndex = -1;
    if (PBGetCatInfoSync(&c) == noErr && c.dirInfo.ioDrUsrWds.frRect.right - c.dirInfo.ioDrUsrWds.frRect.left > GRID + 16)
        width = c.dirInfo.ioDrUsrWds.frRect.right - c.dirInfo.ioDrUsrWds.frRect.left - 16;      /* less the scroll bar */
    for (i = 1; n < MOST_PLACED; i++) {
        unsigned short flags;
        memset(&c, 0, sizeof c);
        c.hFileInfo.ioNamePtr = name;
        c.hFileInfo.ioVRefNum = vref;
        c.hFileInfo.ioDirID = parent;
        c.hFileInfo.ioFDirIndex = i;
        if (PBGetCatInfoSync(&c) != noErr) break;
        if (c.hFileInfo.ioFlAttrib & 16) { flags = c.dirInfo.ioDrUsrWds.frFlags; at[n] = c.dirInfo.ioDrUsrWds.frLocation; }
        else { flags = c.hFileInfo.ioFlFndrInfo.fdFlags; at[n] = c.hFileInfo.ioFlFndrInfo.fdLocation; }
        if ((flags & INITED) && !(flags & (INVISIBLE | ON_DESK))) n++;
    }
    for (v = 0; v < 100 * GRID; v += GRID)
        for (h = 0; h == 0 || h + GRID <= width; h += GRID) {
            for (i = 0; i < n; i++)
                if (at[i].h > h - GRID && at[i].h < h + GRID && at[i].v > v - GRID && at[i].v < v + GRID) break;
            if (i == n) {
                loc->h = h;
                loc->v = v;
                DisposePtr((Ptr)at);
                return true;
            }
        }
    DisposePtr((Ptr)at);
    return false;
}

/* The install folder's own window, if the INI gives one. A folder just made gets a free place too: left to
   the Finder, it would get the Finder's own window when the Finder placed it. */
static void size_window(short vref, long dir, Boolean made)
{
    Str255 name;
    CInfoPBRec c;
    Point loc;
    Rect r;
    if (!cfg.has_window) return;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioNamePtr = name;
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = dir;
    c.dirInfo.ioFDirIndex = -1;
    if (PBGetCatInfoSync(&c) != noErr) return;
    SetRect(&r, cfg.window[0], cfg.window[1], cfg.window[0] + cfg.window[2], cfg.window[1] + cfg.window[3]);
    put_folder(vref, c.dirInfo.ioDrParID, name, made && free_place(vref, c.dirInfo.ioDrParID, &loc) ? &loc : NULL, &r, false);
}

/* Into a folder, made if need be, on the way down from the install folder; a folder there gets its place */
static OSErr into(long *dir, ConstStr255Param name)
{
    long parent = *dir;
    Point loc;
    OSErr err = dir_of(place_vref, parent, name, dir);
    if (!err && parent == place_dir && place_of(name, &loc)) put_folder(place_vref, parent, name, &loc, NULL, false);
    return err;
}

/* Whether the file name in folder dir is one the INI says to keep, and it's there */
static Boolean kept(long dir, ConstStr255Param name)
{
    short i, j;
    for (i = 0; i < cfg.nkeep; i++) {
        const EupPath *kp = &cfg.keep[i];
        long kd = place_dir;
        for (j = 0; j < kp->n - 1 && find_dir(place_vref, kd, kp->names[j], &kd) == noErr; j++) ;
        if (j == kp->n - 1 && kd == dir && eup_same_name(kp->names[kp->n - 1], name) && what_is(place_vref, dir, name) == 1)
            return true;
    }
    return false;
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
    if (kept(dir, o.rec.name)) {
        prog_add((long)(o.rec.dlen + o.rec.rlen));
        return noErr;
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
    if ((err = end_file(&o, o.err == noErr)) != noErr) {
        o.err = err;
        pcopy(fail_name, o.name);
        fail_app = o.rec.type == 'APPL';
        is_open(o.vref, o.dir, o.name);         /* the old file's own type, if it's open */
    }
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
static void fit(Str255 s, ConstStr255Param t, short width)
{
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
}

static void draw_fit(ConstStr255Param t, short width)
{
    Str255 s;
    fit(s, t, width);
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

/* Save on an earlier install. Standard File would refuse it: the name of a folder that's there ("That name is
   already used by a folder" on System 7), or, in the install, the name of the application in it ("Replace
   existing…?"). So the hook takes it first, as the install folder, and ends the dialog with Cancel: a folder
   named that holds the detect file, else the folder shown if it holds the detect file. The folder shown is
   in SFSaveDisk (the volume, negated) and CurDirStore (Inside Macintosh IV, Standard File Package); the name
   is the dialog's one editText item (editField, Inside Macintosh I, Dialog Manager), item 7 on System 6 and
   10 on System 7. On System 7, CustomPutFile: SFPPutFile with a hook would give the old dialog, without New
   Folder; its hook is called for the dialogs over it too, which have their own refCon (Inside Macintosh:
   Files, Standard File Package). */
static Boolean hook_found;
static short hook_vol;
static long hook_parent, hook_dir;
static Str255 hook_name;

static pascal short choose_hook(short item, DialogPtr d)
{
    short type, vol;
    Handle h;
    Rect r;
    long dir, sub;
    if (item != putSave || !cfg.detect || ((DialogPeek)d)->editField < 0) return item;
    vol = -LMGetSFSaveDisk();
    dir = LMGetCurDirStore();
    GetDialogItem(d, ((DialogPeek)d)->editField + 1, &type, &h, &r);
    GetDialogItemText(h, hook_name);
    if (hook_name[0] && find_dir(vol, dir, hook_name, &sub) == noErr && what_is(vol, sub, cfg.detect) == 1) hook_dir = sub;
    else if (what_is(vol, dir, cfg.detect) == 1) hook_dir = dir;
    else return item;
    hook_found = true;
    hook_vol = vol;
    hook_parent = dir;
    return putCancel;
}

static pascal short choose_hook7(short item, DialogPtr d, void *data)
{
    (void)data;
    if (GetWRefCon(d) != sfMainDialogRefCon) return item;
    return choose_hook(item, d);
}

static Boolean sys7;                /* System 7 or later: its Standard File */

static void choose_place(void)
{
    SFReply reply;
    Point where;
    WDPBRec wd;
    Str255 prompt;
    long dir;
    Handle h = GetResource('DLOG', putDlgID);   /* Standard File's own dialog (Inside Macintosh I, Standard File Package) */
    Rect s = qd.screenBits.bounds, r;
    if (h) r = **(Rect **)h; else SetRect(&r, 0, 0, 304, 104);
    where.h = s.left + (s.right - s.left - (r.right - r.left)) / 2;       /* in the middle of the main screen too */
    where.v = s.top + MENU_BAR + (s.bottom - s.top - MENU_BAR - (r.bottom - r.top)) / 2;
    fmt(prompt, STR_INSTALL_IN, cfg.product, NULL);
    hook_found = false;
    if (sys7) {
        StandardFileReply r7;
        where.h = where.v = -1;     /* centered */
        CustomPutFile(prompt, place_name, &r7, sfPutDialogID, where, (DlgHookYDUPP)choose_hook7, NULL, NULL, NULL, NULL);
        reply.good = r7.sfGood && !hook_found;
        if (reply.good) {
            place_vol = r7.sfFile.vRefNum;
            place_parent = r7.sfFile.parID;
            place_found = cfg.detect && what_is(place_vol, place_parent, cfg.detect) == 1 ? place_parent : 0;
            pcopy(place_name, r7.sfFile.name);
            return;
        }
    } else SFPPutFile(where, prompt, place_name, (DlgHookUPP)choose_hook, &reply, putDlgID, NULL);
    if (hook_found) {
        place_vol = hook_vol;
        place_parent = hook_parent;
        place_found = hook_dir;
        pcopy(place_name, hook_name);
        return;
    }
    if (!reply.good) return;
    memset(&wd, 0, sizeof wd);
    wd.ioVRefNum = reply.vRefNum;
    if (PBGetWDInfoSync(&wd) != noErr) { place_vol = reply.vRefNum; dir = 2; } else { place_vol = wd.ioWDVRefNum; dir = wd.ioWDDirID; }
    place_parent = dir;
    /* the folder shown holds the detect file: an earlier install, used as it is */
    place_found = cfg.detect && what_is(place_vol, dir, cfg.detect) == 1 ? dir : 0;
    pcopy(place_name, reply.fName);
}

/* With require: whether there's an earlier install to go into, found as detect finds one: the install folder
   (place_found), or a folder of the install folder's name in the place, which is how Install Location's
   default, a folder at the top of the startup disk, is found. Checked again at Install, so a file moved or
   removed since Choose… counts. */
static Boolean found_earlier(void)
{
    long sub;
    if (place_found) return what_is(place_vol, place_found, cfg.detect) == 1;
    if (find_dir(place_vol, place_parent, place_name, &sub) != noErr || what_is(place_vol, sub, cfg.detect) != 1) return false;
    place_found = sub;
    return true;
}

static Boolean place_made;          /* the install folder is new */

/* The folder to install in, made if need be */

static OSErr resolve_place(void)
{
    place_vref = place_vol;
    place_made = false;
    if (place_found) { place_dir = place_found; return noErr; }
    place_made = what_is(place_vol, place_parent, place_name) == 0;
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
    center(prog_dlg);
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

static void prog_center(ConstStr255Param t, short v)
{
    Str255 s;
    fit(s, t, 276);
    MoveTo(12 + (276 - StringWidth(s)) / 2, v);
    DrawString(s);
}

/* The line; at the end, a return in it makes two */
static void prog_text(ConstStr255Param t)
{
    Rect r;
    GrafPtr save;
    Str255 a;
    short i;
    GetPort(&save);
    SetPort(prog_dlg);
    SetRect(&r, 12, 12, 288, prog_over ? 50 : 42);    /* above the bar, which starts at 44 */
    EraseRect(&r);
    big_font();
    for (i = 1; i <= t[0] && t[i] != '\r'; i++) ;
    if (prog_over && i <= t[0]) {
        a[0] = (unsigned char)(i - 1);
        memcpy(a + 1, t + 1, a[0]);
        prog_center(a, 30);
        a[0] = (unsigned char)(t[0] - i);
        memcpy(a + 1, t + i + 1, a[0]);
        prog_center(a, 46);
    } else prog_center(t, prog_over ? 42 : 28);
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
    busy = false;
    InitCursor();
    while (next_item(prog_dlg, prog_filter, &item) && item != 1) ;
}

static void close_progress(void)
{
    if (prog_dlg) DisposeDialog(prog_dlg);
    prog_dlg = NULL;
}

/* An old name from an earlier version, gone if it's there (a folder only if it's empty), unless the INI keeps
   it or a folder it's in */
static Boolean removing;

static OSErr retire(const EupPath *p)
{
    short k;
    long dir = place_dir;
    for (k = 0; k < cfg.nkeep; k++) if (eup_path_in(p, &cfg.keep[k])) return noErr;
    for (k = 0; k < p->n - 1; k++) if (find_dir(place_vref, dir, p->names[k], &dir) != noErr) return noErr;
    if (!what_is(place_vref, dir, p->names[p->n - 1])) return noErr;
    if (is_open(place_vref, dir, p->names[p->n - 1])) return fBsyErr;
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

/* Before anything is written: whether a file the install would replace or remove is open (is_open) */
static OSErr preflight(void)
{
    short i, j, k;
    if (cfg.types[mode].remove_old)
        for (i = 0; i < cfg.nremove; i++) {
            const EupPath *p = &cfg.remove[i];
            long dir = place_dir;
            for (k = 0; k < cfg.nkeep && !eup_path_in(p, &cfg.keep[k]); k++) ;
            if (k < cfg.nkeep) continue;
            for (j = 0; j < p->n - 1 && find_dir(place_vref, dir, p->names[j], &dir) == noErr; j++) ;
            if (j == p->n - 1 && is_open(place_vref, dir, p->names[j])) return fBsyErr;
        }
    for (i = 0; i < cfg.ncomps; i++) {
        const EupComp *m = &cfg.comps[i];
        if (!wanted(i)) continue;
        for (k = 0; k < comps[i].n; k++) {
            EupRecord r;
            const unsigned char *rest;
            int nrest;
            long dir = place_dir;
            eup_record_read(rec_at[comps[i].recs[k]], &r);
            if (r.kind == EUP_REC_FOLDER) continue;
            eup_record_under(&r, &m->source, &rest, &nrest);
            for (j = 0; j < m->destination.n && find_dir(place_vref, dir, m->destination.names[j], &dir) == noErr; j++) ;
            if (j < m->destination.n) continue;
            for (j = 0; j < nrest - 1 && find_dir(place_vref, dir, rest, &dir) == noErr; j++, rest += 1 + rest[0]) ;
            if (j < nrest - 1) continue;
            if (!kept(dir, r.name) && is_open(place_vref, dir, r.name)) return fBsyErr;
        }
    }
    return noErr;
}

static OSErr run_install(void)
{
    short i, k;
    OSErr err = noErr;
    size_window(place_vref, place_dir, place_made);
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
    if (quit_asked) return;
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
    set_menus();
    if (!(d = GetNewDialog(WIN, NULL, (WindowPtr)-1))) return 1;
    center(d);
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
    sys7 = env.systemVersion >= 0x0700;
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
        if (!next_item(d, filter, &item) || item == B_QUIT) done = true;
        else if (item == CANVAS) { if (stage == S_INSTALL) install_click(d, click_pt); }
        else if (item == B_CHOOSE) { choose_place(); note[0] = 0; redraw(d); }
        else if (item == B_PRIMARY) {
            if (stage == S_DESC) set_stage(d, cfg.license_screen.n ? S_LICENSE : S_INSTALL);
            else if (stage == S_LICENSE) set_stage(d, S_INSTALL);
            else {
                OSErr err;
                Str255 s;
                if (cfg.require && !found_earlier()) {      /* refused before anything is written */
                    HideWindow(d);
                    open_progress();
                    fmt(s, STR_REQUIRED, cfg.require, NULL);
                    prog_finish(s, STR_OK);
                    note[0] = 0;
                    back_to_choices(d);
                    continue;
                }
                busy = true;
                SetCursor(*GetCursor(watchCursor));
                HideWindow(d);
                open_progress();
                if ((err = resolve_place()) != noErr) {
                    GetIndString(s, 128, STR_FOLDER_FAILED);
                    prog_finish(s, STR_OK);
                    back_to_choices(d);
                    continue;
                }
                fail_name[0] = 0;
                if ((err = preflight()) == noErr) err = run_install();
                busy = false;
                InitCursor();
                note[0] = 0;
                if (err == userCanceledErr) {
                    GetIndString(note, 128, STR_CANCELED);
                    back_to_choices(d);
                } else if (err == fBsyErr && fail_name[0]) {
                    fmt(s, STR_IN_USE, fail_name, fail_app ? fail_name : cfg.product);
                    prog_finish(s, STR_OK);
                    back_to_choices(d);
                } else if (err) {
                    Str255 n;
                    NumToString(err, n);
                    if (fail_name[0]) fmt(s, STR_NOT_REPLACED, fail_name, n);
                    else fmt(s, STR_FAILED, n, NULL);
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
