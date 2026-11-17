/* The EUP installer program's own resources. The builder (eup) adds the rest: see install.c. */
#include "Processes.r"
#include "Finder.r"
#include "Menus.r"
#include "Dialogs.r"
#include "icons.r"
#include "version.h"

/* The builder writes its own, from what the installer needs to unpack */
resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    notHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    400 * 1024, 300 * 1024
};

/* The builder replaces it with the software's */
resource 'vers' (1) {
    EUP_VERS_MAJOR, EUP_VERS_MINOR, EUP_VERS_STAGE, 0x00, verUS, EUP_VERSION,
    "EUP " EUP_VERSION ", \$A9 " EUP_YEAR " OneInAMillion Software. Freeware."
};

type 'EUPi' { pstring; };
resource 'EUPi' (0, "Owner resource") { "EUP " EUP_VERSION };
resource 'FREF' (128) { 'APPL', 0, "" };
resource 'BNDL' (128) {
    'EUPi', 0,
    { 'ICN#', { 0, 128 }; 'ics#', { 0, 128 }; 'FREF', { 0, 128 } }
};

/* EUP's own words; ^0 and ^1 are filled in */
resource 'STR#' (128) {
    {
        "Install";
        "Quit";
        "Choose\311";
        "OK";
        "Continue";
        "Agree";
        "Disagree";
        "Disk space available: ^0      Needed: ^1";
        "Install Location";
        "Items will go into the folder \322^0\323 on the disk \322^1\323.";
        "Install ^0 in:";
        "^0 is installed.";
        "Installation canceled.";
        "Installation failed (error ^0).";
        "The folder could not be created.";
        "Installing ^0";
        "Removing old files";
        "Click Agree to continue, or Disagree to quit.";
        "^0K";
        "About ^0\311";
        "\322^0\323 is in use.\015Quit ^1 and click Install again.";
        "\322^0\323 could not be replaced.\015Installation failed (error ^1).";
        "Install ^0 first, then\015open this installer again."
    }
};

/* The menus: the Apple menu (About, and the desk accessories), File, and Edit for the desk accessories */
resource 'MBAR' (128) { { 128; 129; 130 } };
resource 'MENU' (128) { 128, textMenuProc, 0x7FFFFFFD, enabled, apple, { "About\311", noIcon, noKey, noMark, plain; "-", noIcon, noKey, noMark, plain } };
resource 'MENU' (129) { 129, textMenuProc, allEnabled, enabled, "File", { "Quit", noIcon, "Q", noMark, plain } };
resource 'MENU' (130) { 130, textMenuProc, 0x7FFFFFFD, disabled, "Edit", {
    "Undo", noIcon, "Z", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Cut", noIcon, "X", noMark, plain;
    "Copy", noIcon, "C", noMark, plain;
    "Paste", noIcon, "V", noMark, plain;
    "Clear", noIcon, noKey, noMark, plain
} };

/* About: the software's Get Info line (or the title), and EUP's */
resource 'DLOG' (132) {
    {0, 0, 100, 300}, dBoxProc, invisible, noGoAway, 0, 132, "", noAutoCenter
};
resource 'DITL' (132) {
    {
        {68, 210, 88, 280}, Button { enabled, "OK" },
        {12, 20, 60, 280}, StaticText { disabled, "^0\r\rMade with EUP " EUP_VERSION "." }
    }
};

/* One window, 440 by 256, for every stage, moved to the middle of the main screen when it opens (install.c); the program draws
   what's in it (the text boxes, the pop-up menu, the list) over a user item, around the buttons. Its title is the
   INI's. */
resource 'DLOG' (128) {
    {62, 36, 318, 476}, noGrowDocProc, invisible, noGoAway, 0, 128, "Install", noAutoCenter
};
resource 'DITL' (128) {
    {
        {222, 348, 244, 428}, Button { enabled, "Continue" },
        {222, 258, 244, 338}, Button { enabled, "Quit" },
        {220, 342, 240, 422}, Button { enabled, "Choose" },
        {0, 0, 256, 440}, UserItem { enabled }
    }
};

/* The small window while installing: a line, the bar, and Cancel; moved to the middle too */
resource 'DLOG' (130) {
    {118, 106, 228, 406}, noGrowDocProc, invisible, noGoAway, 0, 130, "Install", noAutoCenter
};
resource 'DITL' (130) {
    {
        {76, 208, 98, 288}, Button { enabled, "Cancel" },
        {0, 0, 110, 300}, UserItem { disabled }
    }
};
