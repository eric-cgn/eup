/* The EUP installer program's own resources. The builder (eup) adds the rest: see install.c. */
#include "Processes.r"
#include "Finder.r"
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
        "Items will go into the folder \"^0\" on the disk \"^1\".";
        "Install ^0 in:";
        "^0 is installed.";
        "Installation canceled.";
        "Installation failed (error ^0).";
        "Folder creation failed.";
        "Installing ^0";
        "Removing old files";
        "Click Agree to continue, or Disagree to quit.";
        "^0K"
    }
};

/* One window, 440 by 256, a movable modal dialog in the middle of the screen, for every stage; the program draws
   what's in it (the text boxes, the pop-up menu, the list) over a user item, around the buttons. Its title is set
   from 'Einf'. */
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

/* The small window while installing: a line, the bar, and Cancel */
resource 'DLOG' (130) {
    {118, 106, 228, 406}, noGrowDocProc, invisible, noGoAway, 0, 130, "Install", noAutoCenter
};
resource 'DITL' (130) {
    {
        {76, 208, 98, 288}, Button { enabled, "Cancel" },
        {0, 0, 110, 300}, UserItem { disabled }
    }
};
