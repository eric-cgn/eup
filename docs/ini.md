# The INI file

One INI file describes an installer: its name, its install types, what it installs and where,
what an upgrade removes, and where the Finder shows the installed icons. The packager (EUP on a
Mac, `eup` on another system) reads it, checks it, and copies it into the installer, which reads
it again when it runs. The files it names are in the same folder as the INI file, or in folders
inside that folder.

- [The format](#the-format)
- [Paths and names](#paths-and-names)
- [The input files](#the-input-files)
- [installer](#installer)
- [type](#type)
- [group](#group)
- [component](#component)
- [upgrade](#upgrade)
- [layout](#layout)
- [What an installer does](#what-an-installer-does)
- [Limits](#limits)
- [Examples](#examples)

## The format

```ini
; a comment
[installer]
product = Hello            ; a comment after a value
version = 1.0

[type easy]
label = Easy Install
```

- **Sections** are `[installer]`, `[upgrade]`, and `[layout]` (each at most once), and
  `[type ID]`, `[group ID]`, and `[component ID]` (as many as needed). An ID is lower-case
  letters, digits, `-`, and `_`, and is unique among sections of its kind. Sections and keys may
  be written in any case; they are read in lower case.
- **Keys** are `key = value`, one to a line. Spaces around the `=` and at the ends of the value
  are dropped. An empty value is the same as leaving the key out.
- **Comments** are lines that start with `;` or `#`, and anything after a `;` that follows a space
  or a tab. (So a value can't hold ` ;`.)
- **Lists** (`types`) are separated by commas.
- **Repeated keys**: `remove` and `keep` in `[upgrade]` and `place` in `[layout]` may be given
  more than once, one item each. Any other key given twice is an error.
- **Text**: the file is UTF-8 on Linux and macOS (`eup` converts it to Mac Roman, the Mac's
  character set, and refuses a character Mac Roman doesn't have) and Mac Roman on a Mac. Line ends
  may be LF, CR, or CR LF.
- **Errors** are reported with the line they're on, every one at once:

  ```
  hello.ini:31: types: there is no [type custm]
  hello.ini:33: text: yes or no
  ```

## Paths and names

Paths are written the Mac way, on every system:

- The colon separates names: `Plug-Ins:Web Fonts.bin`. A name can't contain a colon (a Mac name
  can't either).
- **Source paths** (the files to install, and the texts) are always inside the INI's folder,
  written with or without the Mac's leading colon for a relative path: `:Plug-Ins:Web Fonts.bin`
  and `Plug-Ins:Web Fonts.bin` are the same. A path to a folder may end in a colon.
- **Installed paths** (`destination`, `remove`, `keep`) are inside the folder the user picks,
  `%chosen%`. Write `%chosen%:Mill Plug-Ins`, `:Mill Plug-Ins`, or (one name) `Mill Plug-Ins`. A
  path that starts with a name and has a colon after it, like `Macintosh HD:System Folder`, names
  a disk, and isn't allowed in EUP 1.0; nor are `%system_folder%` and the other substitutions of
  later versions.
- **`%%`** is a `%` in a name: `50%% off`. A `%` on its own is an error.
- **A name** is 1 to 31 characters, without control characters, and doesn't start with a period
  (the Mac keeps those for drivers). Names are compared as the Mac compares them, without regard
  to case.
- A name with a slash ("TCP/IP") is legal on a Mac but can't be a file name on Linux. Give the
  host file another name and set the installed name with `as =`, or let it come from the MacBinary
  header.

## The input files

On every system:

- **MacBinary** files (`.bin`, as Retro68 writes them, MacBinary I or II) are read as the Mac file
  they hold: its forks, type, creator, Finder flags, and name. A file is MacBinary if its header
  says so (the bytes that must be zero are zero, the name is 1 to 63 characters, the fork lengths
  match the file's size, and a MacBinary II header's CRC is right); the name's extension doesn't
  matter.
- **Plain files** are a data fork. Their type and creator are `????` unless the INI gives them
  (`type`, `creator`), or `text = yes` makes them `TEXT` and `ttxt`. The installed name is the
  file's own.
- **Folders** are installed whole: every file and folder inside, in name order. Items whose names
  start with a period are left out.

On a Mac, files are also read as they are: both forks, the type, creator, and Finder flags. Items
the Finder doesn't show (invisible, such as a folder's custom icon file) are left out.

## [installer]

| Key | Value | Default |
|---|---|---|
| `product` | The software's name, as in "Mill is installed." and "Install Mill in:". Up to 63 characters. **Needed.** | |
| `version` | Its version: `1.0`, `2.0.1`, or with a stage, `1.1d1`, `1.1a2`, `1.1b3`. Goes in the window's title and Get Info's version. | none |
| `title` | The installer's window title. Up to 63 characters. | `Install PRODUCT VERSION` |
| `file` | The installer's name on the Mac. A name (31 characters at most). | the title |
| `output` | The base name of `eup`'s output files, `OUTPUT.bin` and `OUTPUT.hqx`. | the file name with spaces as `-` |
| `folder` | The name of the folder the installer offers to make. A name. | the product |
| `get_info` | The installer's long version string, shown in the Finder's Get Info: `Install Mill 2.0.1, © 2026 OneInAMillion Software. Freeware.` Up to 255 characters. Only with `version`. | the title |
| `creator` | The installer's own creator code (four characters), for an installer with its own icon. | `EUPi`, EUP's |
| `icon` | A file whose resource fork has the installer's icon: `ICN#` (needed), and `ics#`, `icl4`, `icl8`, `ics4`, `ics8` if it has them, ID 128 (or the only one). Needs `creator`, since the Finder keeps one icon to a creator. | EUP's |
| `description_screen` | A text file: a screen shown first, about the software, with **Continue** and **Quit**. | none |
| `license_screen` | A text file: a license to agree to, with **Agree** and **Disagree** (which quits). After the description screen. | none |

Texts (the two screens, and `description_file` below) are at most 32,000 characters. Each line
of the file is a line on the screen: write a paragraph as one line, and leave a blank line between
paragraphs. The text wraps to the box, which scrolls if it's longer.

## [type ID]

The install types, in the pop-up menu in the order they're written. The first is shown first.
There must be at least one.

| Key | Value | Default |
|---|---|---|
| `label` | Its name in the pop-up: `Easy Install`. Up to 63 characters. **Needed.** | |
| `description` | One line for the box under the pop-up. | none |
| `description_file` | Or a text file for the box (it scrolls if it's long). Not with `description`. | none |
| `custom` | `yes`: the box is a list of the type's components, each with a checkbox, its size, and (when selected) its one-line description; the user picks. | `no` |
| `remove_old` | `no`: the type leaves what's there alone, and doesn't remove the `[upgrade]` names. | `yes` |

A type installs the components that name it in their `types`. A custom type shows those
components; the user installs the ones checked. Every type must have at least one component.

## [group ID]

Groups make headings in a custom type's list. A component joins one with `group = ID`.

| Key | Value | Default |
|---|---|---|
| `label` | The heading: `Plug-Ins`. Up to 63 characters. **Needed.** | |
| `selectable` | `yes`: the heading has a checkbox for the whole group: checked (an X) when all its components are, a dash when some are, empty when none are. Clicking it checks them all, or, if all were checked, unchecks them all. | `no` |

A group's components must be written together (in a custom type's list, one group's components
can't be split by another's). Components without a group have no heading.

## [component ID]

What's installed, in the order written. Each component is one file or one folder.

| Key | Value | Default |
|---|---|---|
| `label` | Its name, in the list and in the progress window ("Installing Mill"). Up to 63 characters. **Needed.** | |
| `source` | The file or folder to install, a source path. **Needed.** | |
| `types` | The install types it's in, by ID: `easy, minimal, custom`. **Needed.** | |
| `destination` | The folder it goes into, an installed path. Made if it isn't there. A folder's contents go into it. | `%chosen%` |
| `group` | Its group in a custom type's list, by ID. | none |
| `description` | One line, shown when it's selected in a custom type's list. | none |
| `checked` | Whether it's checked in a custom type's list at first. | `yes` if a type without the list installs it, else `no` |
| `as` | The installed name, for a file. | the file's own (or its MacBinary name) |
| `type`, `creator` | The file's type and creator, four characters each. For a folder, they apply to the plain files in it (which have none of their own). | the file's own |
| `flags` | The file's Finder flags: a number (`0x2000`) or names separated by commas: `bundle` (0x2000, which an application with its own icon needs), `invisible`, `alias`, `stationery`, `custom_icon`, `name_locked`, `no_init`, `shared`. For a file only. | the file's own (zero for Retro68's `.bin` files) |
| `text` | `yes`: plain files are text. They become `TEXT`/`ttxt` (unless `type`, `creator` say otherwise), and on Linux and macOS are converted from UTF-8 to Mac Roman, with CR line ends. | `no` |
| `compress` | `no`: stored as they are, not compressed: a bigger installer that installs faster. | `yes` |

A file that two components name is packaged once (if they name it the same way). Two things
can't be installed in the same place.

## [upgrade]

| Key | Value |
|---|---|
| `detect` | A file name that marks an earlier install. When the user chooses a folder that holds a file of this name, the installer installs into that folder as it is, instead of making a new folder in it. |
| `remove` | An installed path to remove before installing: a name from an earlier version. A folder is removed only if it's empty. Repeated, one to a line. Not done by a type with `remove_old = no`. |
| `keep` | An installed path not to replace: if the file is there, the installer leaves it and skips the one it carries (a settings file, say). Repeated. A path can't be both removed and kept. |

## [layout]

Where the Finder shows the installed items. Both are optional.

| Key | Value |
|---|---|
| `window` | The installed folder's window: `left, top, width, height` in pixels, on the screen. |
| `place` | `NAME: across, down`: where the icon of an item in the installed folder goes in that window, in pixels from the window's top left. Repeated, one to a line. A file gets its place when it's installed; a folder whenever an install passes through it. |

On System 6 an icon's middle is about 32 pixels across and 25 down from its place: Install Mill
places Mill at `18, 9` and its folders a row down at `108, 79` and `198, 79`.

## What an installer does

1. If the INI names them, the description screen, then the license screen.
2. The install window: the type pop-up, the box (the type's description, or a custom type's
   list), the disk space available and needed, and **Install Location**, which names the folder and
   disk. **Choose…** picks another place (and recognizes an earlier install by `detect`).
   **Install** is dim if nothing is picked or there isn't room.
3. Installing, in a small window: "Removing old files" if there are any, then "Installing LABEL"
   for each component, and a bar that moves with the bytes unpacked. **Cancel** (or Escape, or
   Command-period) stops at once. Each file is written under a temporary name and given its name
   only when it's whole, so a Cancel, a full disk, or an error leaves no partial file and keeps the
   file it would have replaced.
4. The end, in the same small window: "PRODUCT is installed." with **Quit**, or the error with
   **OK**, back to the choices. After a Cancel, the choices say "Installation canceled."

Space needed is counted as each fork rounded up to a whole K.

## Limits

- Everything goes inside the folder the user picks: nothing in the System Folder yet.
- Up to 2,000 files, and folders 30 deep. At most 40 errors are listed at once.
- Texts up to 32,000 characters.
- An installer's resource fork (the INI, texts, and record of each file) is under 16 MB; the
  payload is in its data fork.

## Examples

`examples/hello/hello.ini` uses both screens, a selectable group, a folder, and text conversion.
`examples/mill/mill.ini` makes Install Mill 2.0.1.

```ini
[installer]
product = Mill
version = 2.0.1
get_info = Install Mill 2.0.1, © 2026 OneInAMillion Software. Freeware.
creator = MiLI
icon = Install icon.bin

[type easy]
label = Easy Install
description_file = Easy Install.txt

[type minimal]
label = Minimal Install
description = Mill alone. Plug-Ins can be added later with Custom Install.
remove_old = no

[type custom]
label = Custom Install
custom = yes

[group plugins]
label = Plug-Ins
selectable = yes

[component mill]
label = Mill
description = The web browser.
source = Mill.bin
flags = bundle
types = easy, minimal, custom

[component internet-services]
group = plugins
label = Internet Services
description = Finger, Whois, Dict, Time, FTP, and Gopher.
source = Internet Services.bin
destination = %chosen%:Mill Plug-Ins
types = easy, custom

[upgrade]
detect = Mill
remove = %chosen%:Mill Fonts
keep = Mill Settings

[layout]
window = 40, 60, 300, 190
place = Mill: 18, 9
place = Mill Plug-Ins: 108, 79
```
