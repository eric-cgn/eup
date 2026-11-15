# EUP

EUP (*Ex uno plura*, "out of one, many"; said "youp") makes installers for classic Macs. Put an
INI file and the files to install in a folder; EUP makes an installer application from it, with
the same window and steps for every installer: install types in a pop-up (Easy, Minimal, Custom,
or your own), a custom list with checkboxes and sizes, the disk space, Install Location with
Choose…, a progress window with Cancel, upgrades that remove old names, and, if you want them, a
screen about the software and a license to agree to.

An installer made with EUP runs on a Mac Plus or later with System 6 or later. It is one
application: the installer program (about 41K) with the INI and the files, compressed, inside it.

From OneInAMillion Software, makers of Mill. Freeware, MIT.

## Two packagers

- **EUP**, an application for the Mac: open the INI file, see any problems by line, save the
  installer. It runs on a Mac Plus. It's on the release disk, `EUP-1.0.image`, with the
  Installer Shell it needs, a Read Me, and an example.
- **`eup`**, a command for Linux (and macOS), for making installers as part of a build: it reads
  MacBinary files (as Retro68 writes them), plain files, and folders, and writes the installer as
  MacBinary (`.bin`) and BinHex (`.hqx`).

Both are the same code (`core/`), and make the same installer from the same files.

## Quick start (`eup`)

```sh
make                                   # builds eup (a C compiler, nothing else)
./eup check examples/hello             # checks the INI and the files it names
./eup build examples/hello -o dist     # dist/Install-Hello-1.0.bin and .hqx
```

`eup check INI` and `eup build INI [-o DIR]` take the INI file or the folder that holds it (if it
holds just one). Errors are listed with their line numbers, and nothing is written:

```
examples/hello/hello.ini:31: types: there is no [type custm]
```

Set `SOURCE_DATE_EPOCH` for an installer that comes out the same on every build.

## The workflow

1. Make a folder.
2. Put in it the INI file and everything to install: Retro68's `.bin` files, plain files,
   folders. Name them in the INI by their paths inside the folder, Mac style
   (`Plug-Ins:Web Fonts.bin`).
3. Run the packager. Out comes the installer.

```ini
[installer]
product = Hello
version = 1.0
license_screen = License.txt

[type easy]
label = Easy Install
description = Hello and its samples.

[type custom]
label = Custom Install
custom = yes

[component app]
label = Hello
source = Hello.bin
flags = bundle
types = easy, custom

[component samples]
label = Samples
source = Samples
destination = %chosen%:Samples
text = yes
types = easy, custom
```

The INI's keys, all of them: [docs/ini.md](docs/ini.md).

## Examples

- `examples/hello`: both screens, a selectable group, a folder of text files.
- `examples/mill`: Install Mill 2.0.1, from an INI. Mill's files aren't in this repository; see
  its README.

## Building and testing

- `make test`: the host tests for the core and `eup` (macOS and Linux).
- `./build-mac.sh`: the installer program and EUP, with Retro68 in Docker (the image pinned by
  digest), into `prebuilt/`. Only for changing them: `eup` uses the prebuilt installer program, so
  making installers never needs Retro68.
- `./release.sh`: `dist/EUP-1.0.image` (the Mac disk), EUP and the Installer Shell alone as `.bin`
  and `.hqx`, and `dist/eup-1.0.tar.gz` and `.zip` (the source, for `eup`).

The version is set in one place, `core/version.h`; what each version changed is in
[CHANGES.md](CHANGES.md).

How it works inside: [docs/format.md](docs/format.md).

## Licence

MIT (`LICENSE`), from OneInAMillion Software. The installer program and EUP contain parts of
newlib, Multiversal Interfaces, and the Retro68 and GCC runtimes, and the gzip decoder from Mill;
their notices are in `licences/` and, for Mac users, in "Licenses" on the release disk. An
installer you make carries the installer program, so carry those notices with it too (a line in
your Read Me pointing to them is enough for most; check the terms in `licences/`).
