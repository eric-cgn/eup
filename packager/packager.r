/* EUP, the packager application: its menus, windows, and words. */
#include "Processes.r"
#include "Finder.r"
#include "Menus.r"
#include "Dialogs.r"
#include "icons.r"
#include "version.h"

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    notHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    512 * 1024, 384 * 1024
};

resource 'vers' (1) {
    EUP_VERS_MAJOR, EUP_VERS_MINOR, EUP_VERS_STAGE, 0x00, verUS, EUP_VERSION,
    "EUP " EUP_VERSION ", \$A9 " EUP_YEAR " OneInAMillion Software. Freeware."
};

type 'EUPp' { pstring; };
resource 'EUPp' (0, "Owner resource") { "EUP " EUP_VERSION };
resource 'FREF' (128) { 'APPL', 0, "" };
resource 'BNDL' (128) {
    'EUPp', 0,
    { 'ICN#', { 0, 128 }; 'ics#', { 0, 128 }; 'FREF', { 0, 128 } }
};

resource 'MBAR' (128) { { 128; 129; 130 } };
resource 'MENU' (128) { 128, textMenuProc, 0x7FFFFFFD, enabled, apple, { "About EUP\311", noIcon, noKey, noMark, plain; "-", noIcon, noKey, noMark, plain } };
resource 'MENU' (129) { 129, textMenuProc, 0x7FFFFFFD, enabled, "File", {
    "Open\311", noIcon, "O", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Quit", noIcon, "Q", noMark, plain
} };
resource 'MENU' (130) { 130, textMenuProc, 0, enabled, "Edit", {
    "Undo", noIcon, "Z", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Cut", noIcon, "X", noMark, plain;
    "Copy", noIcon, "C", noMark, plain;
    "Paste", noIcon, "V", noMark, plain;
    "Clear", noIcon, noKey, noMark, plain
} };

resource 'STR#' (128) {
    {
        "The Installer Shell can't be found. Put it in the same folder as EUP.";
        "The INI file can't be read.";
        "Save the installer as:";
        "Packaging \322^0\323";
        "\322^0\323 is ready.";
        "Packaging failed: ^0.";
        "Packaging canceled.";
        "Line ^0: ^1";
        "Installer Shell";
        "There isn't enough memory. Give EUP more in its Get Info window.";
        "And more.";
        "The installer can't be written. Check that the disk isn't locked or full."
    }
};

/* Alerts: a note and a stop, each with the text given */
resource 'ALRT' (128) { {80, 76, 190, 436}, 128, { OK, visible, sound1; OK, visible, sound1; OK, visible, sound1; OK, visible, sound1 }, alertPositionMainScreen };
resource 'ALRT' (129) { {80, 76, 190, 436}, 128, { OK, visible, sound1; OK, visible, sound1; OK, visible, sound1; OK, visible, sound1 }, alertPositionMainScreen };
resource 'DITL' (128) {
    {
        {78, 278, 98, 348}, Button { enabled, "OK" },
        {10, 72, 70, 348}, StaticText { disabled, "^0" }
    }
};
resource 'ALRT' (132) { {70, 106, 190, 406}, 132, { OK, visible, silent; OK, visible, silent; OK, visible, silent; OK, visible, silent }, alertPositionMainScreen };
resource 'DITL' (132) {
    {
        {88, 210, 108, 280}, Button { enabled, "OK" },
        {12, 20, 80, 280}, StaticText { disabled, "EUP " EUP_VERSION "\rEx uno plura.\r\rMakes installers for the Macintosh.\r\$A9 " EUP_YEAR " OneInAMillion Software. Freeware." }
    }
};

/* Packaging: a line, the bar, and Cancel */
resource 'DLOG' (130) {
    {118, 106, 228, 406}, noGrowDocProc, invisible, noGoAway, 0, 130, "EUP", alertPositionMainScreen
};
resource 'DITL' (130) {
    {
        {76, 208, 98, 288}, Button { enabled, "Cancel" },
        {0, 0, 70, 300}, UserItem { disabled }
    }
};

/* The problems found, one to a line; its title is the INI's name */
resource 'DLOG' (131) {
    {50, 36, 290, 476}, noGrowDocProc, invisible, noGoAway, 0, 131, "Problems", alertPositionMainScreen
};
resource 'DITL' (131) {
    {
        {206, 348, 228, 428}, Button { enabled, "OK" },
        {12, 12, 194, 428}, UserItem { disabled }
    }
};
