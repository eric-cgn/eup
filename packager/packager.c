/* EUP, the packager for the Mac: open an INI file; its folder holds the files it names. EUP checks
   them, asks where to save the installer, and makes it from the Installer Shell beside it: the shell's
   resources, the INI and its texts, and the payload, each file compressed as it's read. The work is
   EUP's core (core/package.c), shared with the command-line packager; this is the Mac's side of it:
   the File Manager as the core's file source, and the windows. Runs on a Mac Plus. */
#include <Multiverse.h>
#include <string.h>
#include <stdlib.h>

#include "../core/eup.h"
#include "../core/format.h"
#include "../core/package.h"

enum { M_APPLE = 128, M_FILE, M_EDIT };
enum { ALRT_NOTE = 128, ALRT_STOP = 129, DLG_PROG = 130, DLG_PROBLEMS = 131, ALRT_ABOUT = 132 };

/* EUP's words: 'STR#' 128 */
enum { STR_SHELL_MISSING = 1, STR_INI_UNREAD, STR_SAVE_AS, STR_PACKAGING, STR_READY, STR_FAILED, STR_CANCELED,
       STR_LINE, STR_SHELL_NAME, STR_NO_MEMORY, STR_MORE, STR_CANT_WRITE };

/* ---- strings ---- */

static void pcopy(Str255 out, ConstStr255Param s) { memcpy(out, s, s[0] + 1); }

static void pcat(Str255 out, ConstStr255Param s)
{
    short n = s[0];
    if (out[0] + n > 255) n = 255 - out[0];
    memcpy(out + 1 + out[0], s + 1, n);
    out[0] += n;
}

static void cstr(Str255 out, const char *s)
{
    short n = (short)strlen(s);
    if (n > 255) n = 255;
    out[0] = (unsigned char)n;
    memcpy(out + 1, s, n);
}

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

static void alert(short id, ConstStr255Param text)
{
    ParamText(text, "\p", "\p", "\p");
    InitCursor();
    if (id == ALRT_STOP) StopAlert(id, NULL); else NoteAlert(id, NULL);
}

static void alert_str(short id, short index)
{
    Str255 s;
    GetIndString(s, 128, index);
    alert(id, s);
}

/* ---- the File Manager as the core's file source: paths inside the INI's folder ---- */

static short root_vol;
static long root_dir;

/* The folder a path's names lead to, all but the last (n - 1 of them) */
static OSErr walk_to(PStr *names, int n, long *dir)
{
    CInfoPBRec c;
    int i;
    *dir = root_dir;
    for (i = 0; i < n; i++) {
        OSErr err;
        memset(&c, 0, sizeof c);
        c.dirInfo.ioNamePtr = names[i];
        c.dirInfo.ioVRefNum = root_vol;
        c.dirInfo.ioDrDirID = *dir;
        if ((err = PBGetCatInfoSync(&c)) != noErr) return err;
        if (!(c.dirInfo.ioFlAttrib & 16)) return dirNFErr;
        *dir = c.dirInfo.ioDrDirID;
    }
    return noErr;
}

static int m_stat(void *ref, PStr *names, int n, EupStat *st)
{
    CInfoPBRec c;
    long dir;
    (void)ref;
    memset(st, 0, sizeof *st);
    if (walk_to(names, n - 1, &dir) != noErr) return 0;
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = names[n - 1];
    c.hFileInfo.ioVRefNum = root_vol;
    c.hFileInfo.ioDirID = dir;
    if (PBGetCatInfoSync(&c) != noErr) return 0;
    if (c.hFileInfo.ioFlAttrib & 16) { st->kind = EUP_FOLDER; return 0; }
    st->kind = EUP_FILE;
    st->dlen = c.hFileInfo.ioFlLgLen;
    st->rlen = c.hFileInfo.ioFlRLgLen;
    st->type = c.hFileInfo.ioFlFndrInfo.fdType;
    st->creator = c.hFileInfo.ioFlFndrInfo.fdCreator;
    st->flags = (unsigned short)c.hFileInfo.ioFlFndrInfo.fdFlags;
    st->has_info = 1;
    return 0;
}

/* A folder's index'th item that the Finder shows (invisible ones, like a custom icon's file, are left out) */
static int m_entry(void *ref, PStr *names, int n, int index, unsigned char *name, char *why)
{
    CInfoPBRec c;
    long dir;
    short i, seen = -1;
    (void)ref;
    (void)why;
    if (walk_to(names, n, &dir) != noErr) return 1;
    for (i = 1; ; i++) {
        memset(&c, 0, sizeof c);
        c.hFileInfo.ioNamePtr = name;
        c.hFileInfo.ioVRefNum = root_vol;
        c.hFileInfo.ioDirID = dir;
        c.hFileInfo.ioFDirIndex = i;
        if (PBGetCatInfoSync(&c) != noErr) return 1;
        if (c.hFileInfo.ioFlAttrib & 16 ? c.dirInfo.ioDrUsrWds.frFlags & 0x4000 : c.hFileInfo.ioFlFndrInfo.fdFlags & 0x4000) continue;
        if (++seen == index) return 0;
    }
}

typedef struct { short ref; } MFile;

static void *m_open(void *ref, PStr *names, int n, int fork)
{
    HParamBlockRec h;
    long dir;
    MFile *f;
    (void)ref;
    if (walk_to(names, n - 1, &dir) != noErr || !(f = (MFile *)malloc(sizeof *f))) return NULL;
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = names[n - 1];
    h.fileParam.ioVRefNum = root_vol;
    h.fileParam.ioDirID = dir;
    h.ioParam.ioPermssn = fsRdPerm;
    if ((fork ? PBHOpenRFSync(&h) : PBHOpenSync(&h)) != noErr) { free(f); return NULL; }
    f->ref = h.ioParam.ioRefNum;
    return f;
}

static OSErr read_at(short ref, unsigned long at, void *buf, long n)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ref;
    pb.ioParam.ioBuffer = (Ptr)buf;
    pb.ioParam.ioReqCount = n;
    pb.ioParam.ioPosMode = fsFromStart;
    pb.ioParam.ioPosOffset = (long)at;
    if (PBReadSync(&pb) != noErr || pb.ioParam.ioActCount != n) return ioErr;
    return noErr;
}

static int m_read_at(void *file, unsigned long at, void *buf, long n)
{
    return read_at(((MFile *)file)->ref, at, buf, n) != noErr;
}

static void close_ref(short ref)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ref;
    PBCloseSync(&pb);
}

static void m_close(void *file)
{
    close_ref(((MFile *)file)->ref);
    free(file);
}

/* ---- the Installer Shell, beside EUP: its resource fork ---- */

static short shell_ref;

static int shell_read(void *ref, unsigned long at, void *buf, long n)
{
    (void)ref;
    return read_at(shell_ref, at, buf, n) != noErr;
}

static OSErr open_shell(unsigned long *len)
{
    FCBPBRec f;
    HParamBlockRec h;
    Str255 me, name;
    OSErr err;
    memset(&f, 0, sizeof f);
    f.ioNamePtr = me;
    f.ioRefNum = CurResFile();
    if ((err = PBGetFCBInfoSync(&f)) != noErr) return err;
    GetIndString(name, 128, STR_SHELL_NAME);
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = name;
    h.fileParam.ioVRefNum = f.ioFCBVRefNum;
    h.fileParam.ioDirID = f.ioFCBParID;
    if ((err = PBHGetFInfoSync(&h)) != noErr) return err;
    *len = (unsigned long)h.fileParam.ioFlRLgLen;
    h.fileParam.ioDirID = f.ioFCBParID;
    h.ioParam.ioPermssn = fsRdPerm;
    if ((err = PBHOpenRFSync(&h)) != noErr) return err;
    shell_ref = h.ioParam.ioRefNum;
    return noErr;
}

/* ---- the progress window: a line, a bar, and Cancel ---- */

static DialogPtr prog;
static Rect prog_bar;
static short prog_drawn;
static Str255 prog_line;

static pascal void prog_draw(DialogPtr d, short item)
{
    Rect r = prog_bar;
    (void)item;
    (void)d;
    TextFont(systemFont);
    TextSize(12);
    MoveTo(12 + (276 - StringWidth(prog_line)) / 2, 28);
    DrawString(prog_line);
    FrameRect(&r);
    InsetRect(&r, 2, 2);
    r.right = r.left + prog_drawn;
    PaintRect(&r);
}

static void open_progress(ConstStr255Param line)
{
    short type;
    Handle h;
    Rect r;
    pcopy(prog_line, line);
    prog_drawn = 0;
    if (!(prog = GetNewDialog(DLG_PROG, NULL, (WindowPtr)-1))) return;
    GetDialogItem(prog, 2, &type, &h, &r);
    SetDialogItem(prog, 2, type, (Handle)(UserItemUPP)prog_draw, &r);
    SetRect(&prog_bar, 12, 44, 288, 60);
    ShowWindow(prog);
    SetPort(prog);
    DrawDialog(prog);
}

/* The core's progress: the bar, and Cancel (the button, Escape, or Command-period) */
static int progress(void *ref, unsigned long done, unsigned long total)
{
    EventRecord e;
    (void)ref;
    if (prog && total) {
        short w = prog_bar.right - prog_bar.left - 4;
        short x = (short)((double)w * (done > total ? total : done) / total);
        if (x > prog_drawn) {
            Rect r;
            SetPort(prog);
            SetRect(&r, prog_bar.left + 2 + prog_drawn, prog_bar.top + 2, prog_bar.left + 2 + x, prog_bar.bottom - 2);
            PaintRect(&r);
            prog_drawn = x;
        }
    }
    while (GetNextEvent(mDownMask | keyDownMask | autoKeyMask | updateMask, &e)) {
        if (e.what == updateEvt) {
            if ((DialogPtr)e.message == prog) { BeginUpdate(prog); DrawDialog(prog); EndUpdate(prog); }
        } else if (e.what == mouseDown) {
            WindowPtr w;
            ControlHandle c;
            Point p = e.where;
            if (prog && FindWindow(p, &w) == inContent && w == prog) {
                SetPort(prog);
                GlobalToLocal(&p);
                if (FindControl(p, prog, &c) && TrackControl(c, p, NULL)) return 1;
            }
        } else {
            char ch = e.message & charCodeMask;
            if (ch == 27 || (ch == '.' && (e.modifiers & cmdKey))) return 1;
        }
    }
    return 0;
}

static void close_progress(void)
{
    if (prog) DisposeDialog(prog);
    prog = NULL;
}

/* ---- the problems: a scrolling list, line by line ---- */

static TEHandle prob_te;
static ControlHandle prob_bar;
static Rect prob_view;
static short prob_line, prob_top;

static pascal void prob_draw(DialogPtr d, short item)
{
    Rect r = prob_view;
    (void)d;
    (void)item;
    InsetRect(&r, -5, -4);
    r.right += 16;
    FrameRect(&r);
    TEUpdate(&prob_view, prob_te);
}

static void prob_scroll(short to)
{
    short max = GetControlMaximum(prob_bar);
    if (to < 0) to = 0;
    if (to > max) to = max;
    TEScroll(0, (prob_top - to) * prob_line, prob_te);
    prob_top = to;
    SetControlValue(prob_bar, to);
}

static pascal void prob_track(ControlHandle c, short part)
{
    short page = (prob_view.bottom - prob_view.top) / prob_line - 1;
    (void)c;
    if (part == inUpButton) prob_scroll(prob_top - 1);
    else if (part == inDownButton) prob_scroll(prob_top + 1);
    else if (part == inPageUp) prob_scroll(prob_top - page);
    else if (part == inPageDown) prob_scroll(prob_top + page);
}

static pascal Boolean prob_filter(DialogPtr d, EventRecord *e, short *item)
{
    if (e->what == keyDown || e->what == autoKey) {
        char c = e->message & charCodeMask;
        if (c == 13 || c == 3 || c == 27) { *item = 1; return true; }
    }
    if (e->what == mouseDown) {
        Point p = e->where;
        ControlHandle c;
        short part;
        SetPort(d);
        GlobalToLocal(&p);
        part = FindControl(p, d, &c);
        if (part && c == prob_bar) {
            if (part == inThumb) { TrackControl(c, p, NULL); prob_scroll(GetControlValue(c)); }
            else TrackControl(c, p, (ControlActionUPP)prob_track);
            *item = 0;
            return true;
        }
    }
    return false;
}

static void show_problems(const EupConfig *c, ConstStr255Param ini_name)
{
    DialogPtr d;
    short type, item, i, lines;
    Handle h;
    Rect r;
    Str255 s, num, text;
    if (!(d = GetNewDialog(DLG_PROBLEMS, NULL, (WindowPtr)-1))) return;
    SetWTitle(d, ini_name);
    SetPort(d);
    TextFont(kFontIDGeneva);
    TextSize(9);
    GetDialogItem(d, 2, &type, &h, &r);
    SetDialogItem(d, 2, type, (Handle)(UserItemUPP)prob_draw, &r);
    prob_view = r;
    prob_view.right -= 16;
    InsetRect(&prob_view, 5, 4);
    prob_te = TENew(&prob_view, &prob_view);
    prob_line = (*prob_te)->lineHeight;
    prob_view.bottom = prob_view.top + (prob_view.bottom - prob_view.top) / prob_line * prob_line;
    (*prob_te)->viewRect = prob_view;
    for (i = 0; i < c->nerrors; i++) {
        cstr(text, c->errors[i].text);
        if (text[0] && text[1] >= 'a' && text[1] <= 'z') text[1] -= 32;      /* a sentence on the Mac */
        if (c->errors[i].line) {
            NumToString(c->errors[i].line, num);
            fmt(s, STR_LINE, num, text);
        } else pcopy(s, text);
        if (i) TEInsert("\r", 1, prob_te);
        TEInsert((Ptr)s + 1, s[0], prob_te);
    }
    if (c->more_errors) {
        GetIndString(s, 128, STR_MORE);
        TEInsert("\r", 1, prob_te);
        TEInsert((Ptr)s + 1, s[0], prob_te);
    }
    lines = (prob_view.bottom - prob_view.top) / prob_line;
    SetRect(&r, prob_view.right + 5, prob_view.top - 4, prob_view.right + 21, prob_view.bottom + 4);
    prob_top = 0;
    prob_bar = NewControl(d, &r, "\p", true, 0, 0, (*prob_te)->nLines > lines ? (*prob_te)->nLines - lines : 0, scrollBarProc, 0);
    if ((*prob_te)->nLines <= lines) HiliteControl(prob_bar, 255);
    ShowWindow(d);
    InitCursor();
    do ModalDialog((ModalFilterUPP)prob_filter, &item); while (item != 1);
    TEDispose(prob_te);
    DisposeDialog(d);
}

/* ---- packaging ---- */

typedef struct { short ref; } Out;

static int data_out(void *ref, const unsigned char *p, long n)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ((Out *)ref)->ref;
    pb.ioParam.ioBuffer = (Ptr)p;
    pb.ioParam.ioReqCount = n;
    pb.ioParam.ioPosMode = fsAtMark;
    return PBWriteSync(&pb) != noErr;
}

static void package(void)
{
    SFTypeList types;
    SFReply in_reply, out_reply;
    Point where;
    WDPBRec wd;
    HParamBlockRec h;
    static EupConfig cfg;           /* not on the stack: its messages are 8K */
    EupFS fs;
    EupPackIn pin;
    EupPackOut pout;
    EupPackage *pack;
    Str255 s, name;
    char why[200];
    char *text = NULL;
    long len;
    unsigned long shell_len;
    short ini_ref = 0, out_vol = 0;
    Out out;
    OSErr err;
    int failed;
    where.h = 80;
    where.v = 60;
    types[0] = 'TEXT';
    SFGetFile(where, "\p", NULL, 1, types, NULL, &in_reply);
    if (!in_reply.good) return;
    /* the INI's folder: a working directory to a volume and a folder */
    memset(&wd, 0, sizeof wd);
    wd.ioVRefNum = in_reply.vRefNum;
    if (PBGetWDInfoSync(&wd) != noErr) { root_vol = in_reply.vRefNum; root_dir = 2; }
    else { root_vol = wd.ioWDVRefNum; root_dir = wd.ioWDDirID; }
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = in_reply.fName;
    h.fileParam.ioVRefNum = root_vol;
    h.fileParam.ioDirID = root_dir;
    h.ioParam.ioPermssn = fsRdPerm;
    if (PBHOpenSync(&h) != noErr) { alert_str(ALRT_STOP, STR_INI_UNREAD); return; }
    ini_ref = h.ioParam.ioRefNum;
    if (GetEOF(ini_ref, &len) != noErr || len > 200000L || !(text = (char *)malloc(len + 1)) || read_at(ini_ref, 0, text, len) != noErr) {
        close_ref(ini_ref);
        free(text);
        alert_str(ALRT_STOP, STR_INI_UNREAD);
        return;
    }
    close_ref(ini_ref);
    SetCursor(*GetCursor(watchCursor));
    if (eup_parse(&cfg, text, len) < 0) { free(text); alert_str(ALRT_STOP, STR_NO_MEMORY); return; }
    if (cfg.nerrors) { show_problems(&cfg, in_reply.fName); eup_free(&cfg); free(text); return; }
    if (open_shell(&shell_len) != noErr) { eup_free(&cfg); free(text); alert_str(ALRT_STOP, STR_SHELL_MISSING); return; }
    memset(&fs, 0, sizeof fs);
    fs.stat = m_stat;
    fs.entry = m_entry;
    fs.open = m_open;
    fs.read_at = m_read_at;
    fs.close = m_close;
    memset(&out, 0, sizeof out);
    memset(&pin, 0, sizeof pin);
    pin.fs = &fs;
    pin.config = &cfg;
    pin.ini = text;
    pin.ini_len = len;
    pin.ini_name = in_reply.fName;
    pin.shell_read = shell_read;
    pin.shell_len = shell_len;
    pin.data_out = data_out;
    pin.data_ref = &out;
    pin.progress = progress;
    if (eup_pack_check(&pin, &pack)) {
        close_ref(shell_ref);
        show_problems(&cfg, in_reply.fName);
        eup_free(&cfg);
        free(text);
        return;
    }
    /* where it goes */
    InitCursor();
    GetIndString(s, 128, STR_SAVE_AS);
    SFPutFile(where, s, cfg.file, NULL, &out_reply);
    if (!out_reply.good) { eup_pack_free(pack); close_ref(shell_ref); eup_free(&cfg); free(text); return; }
    pcopy(name, out_reply.fName);
    memset(&wd, 0, sizeof wd);
    wd.ioVRefNum = out_reply.vRefNum;
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = name;
    h.fileParam.ioVRefNum = out_reply.vRefNum;
    out_vol = out_reply.vRefNum;
    PBHDeleteSync(&h);              /* Standard File asked about replacing it */
    memset(&h, 0, sizeof h);
    h.fileParam.ioNamePtr = name;
    h.fileParam.ioVRefNum = out_vol;
    if ((err = PBHCreateSync(&h)) == noErr) {
        h.ioParam.ioPermssn = fsWrPerm;
        err = PBHOpenSync(&h);
        out.ref = h.ioParam.ioRefNum;
    }
    if (err != noErr) {
        eup_pack_free(pack);
        close_ref(shell_ref);
        eup_free(&cfg);
        free(text);
        alert_str(ALRT_STOP, STR_CANT_WRITE);
        return;
    }
    fmt(s, STR_PACKAGING, name, NULL);
    open_progress(s);
    SetCursor(*GetCursor(watchCursor));
    failed = eup_pack(pack, &pout, why);        /* frees pack */
    close_ref(out.ref);
    close_ref(shell_ref);
    if (!failed) {                  /* the resource fork, and the Finder's information: an application with a bundle */
        memset(&h, 0, sizeof h);
        h.fileParam.ioNamePtr = name;
        h.fileParam.ioVRefNum = out_vol;
        h.ioParam.ioPermssn = fsWrPerm;
        if (PBHOpenRFSync(&h) != noErr) failed = 1;
        else {
            out.ref = h.ioParam.ioRefNum;
            if (data_out(&out, pout.rsrc, (long)pout.rsrc_len)) failed = 1;
            close_ref(out.ref);
        }
        memset(&h, 0, sizeof h);
        h.fileParam.ioNamePtr = name;
        h.fileParam.ioVRefNum = out_vol;
        if (!failed && PBHGetFInfoSync(&h) == noErr) {
            h.fileParam.ioDirID = 0;    /* GetFInfo leaves the file's number here */
            h.fileParam.ioFlFndrInfo.fdType = 'APPL';
            h.fileParam.ioFlFndrInfo.fdCreator = pout.creator;
            h.fileParam.ioFlFndrInfo.fdFlags = 0x2000;
            PBHSetFInfoSync(&h);
        }
        if (failed) strcpy(why, "the installer can't be written");
        free(pout.rsrc);
    }
    close_progress();
    if (failed) {
        memset(&h, 0, sizeof h);
        h.fileParam.ioNamePtr = name;
        h.fileParam.ioVRefNum = out_vol;
        PBHDeleteSync(&h);
        if (!strcmp(why, "stopped")) alert_str(ALRT_NOTE, STR_CANCELED);
        else {
            Str255 w;
            cstr(w, why[0] ? why : "");
            fmt(s, STR_FAILED, w, NULL);
            alert(ALRT_STOP, s);
        }
    } else {
        FlushVol(NULL, out_vol);
        fmt(s, STR_READY, name, NULL);
        alert(ALRT_NOTE, s);
    }
    eup_free(&cfg);
    free(text);
}

/* ---- menus ---- */

static Boolean menu(long choice)
{
    short m = HiWord(choice), item = LoWord(choice);
    HiliteMenu(0);
    if (m == M_APPLE) {
        if (item == 1) { InitCursor(); Alert(ALRT_ABOUT, NULL); }
        else {
            Str255 da;
            GetMenuItemText(GetMenuHandle(M_APPLE), item, da);
            OpenDeskAcc(da);
        }
    } else if (m == M_FILE) {
        if (item == 1) package();
        else if (item == 3) return true;
    } else if (m == M_EDIT) SystemEdit(item - 1);
    HiliteMenu(0);
    return false;
}

int main(void)
{
    Boolean quit = false;
    SetApplLimit(LMGetApplLimit() - 24 * 1024L);   /* ApplLimit, less 24K: a bigger stack than a Plus's 8K */
    MaxApplZone();
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
    SetMenuBar(GetNewMBar(128));
    AppendResMenu(GetMenuHandle(M_APPLE), 'DRVR');
    DrawMenuBar();
    package();                      /* straight to the Open dialog */
    while (!quit) {
        EventRecord e;
        WindowPtr w;
        SystemTask();
        if (!GetNextEvent(everyEvent, &e)) continue;
        switch (e.what) {
        case mouseDown:
            switch (FindWindow(e.where, &w)) {
            case inMenuBar: quit = menu(MenuSelect(e.where)); break;
            case inSysWindow: SystemClick(&e, w); break;
            default: break;
            }
            break;
        case keyDown:
        case autoKey:
            if (e.modifiers & cmdKey) quit = menu(MenuKey(e.message & charCodeMask));
            break;
        default:
            break;
        }
    }
    return 0;
}
