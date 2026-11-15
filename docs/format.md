# How an installer is made

For changing EUP. Making installers needs none of this.

## The parts

- **`core/`**: portable C, built into all three programs. `ini.c` reads and checks the INI;
  `paths.c` has the path rules and Mac Roman; `format.c` has MacBinary, resource forks (read
  through a function that reads at an offset, written in memory), BinHex 4.0, and the record
  format; `deflate.c` writes gzip (an 8K window, about 70K of memory, so it runs on a Mac Plus);
  `inflate.c` reads it (from Mill, MIT); `package.c` checks the files an INI names and packs them,
  through a small file-source interface (`EupFS` in `package.h`).
- **`installer/`**: the installer program (68K, Retro68). Every installer is a copy of it.
- **`packager/`**: EUP, the packager application for the Mac (68K, Retro68).
- **`cli/`**: `eup`, the command-line packager. The installer program is built into it
  (`tools/bin2c.c` makes `build/shell.c` from `prebuilt/Installer Shell.bin`).
- **`prebuilt/`**: the two Mac programs as MacBinary, made by `build-mac.sh` with Retro68 in a
  container (the image pinned by digest), with the Finder's bundle flag set.

## An installer

A copy of the installer program, with:

| Where | What |
|---|---|
| `'TEXT'` 128, named for the INI file | The INI, in Mac Roman. The installer reads it with `core/ini.c` when it starts. |
| `'TEXT'` 129 on, each named for its path | The texts the INI names (`About.txt`, `Texts:License`), Mac Roman with CR line ends. Found by name. |
| `'Efil'` 128 | A record of each file and folder packaged (below). |
| `'vers'` 1 | From `version` and `get_info` (none without a version). |
| the creator's 0, `'BNDL'` 128, the icons | If `creator` is given: the owner resource (the title), a bundle for that creator, and the `icon` file's icons. |
| the data fork | The payload: each file's data fork then its resource fork, as one gzip stream (or as they are, for `compress = no`), one file after another. |

`'Efil'` 128 is a count (2 bytes), then a record for each item, big-endian:

| Bytes | |
|---|---|
| 1 | kind: 0 a file, 1 a folder |
| 1 | flags: 1 stored (not gzip) |
| 4, 4 | type, creator |
| 2 | Finder flags |
| 4, 4 | data fork and resource fork lengths |
| 4 | the payload's length in the data fork |
| 4 | where the payload starts in the data fork |
| 1 + n | the installed name (a Pascal string) |
| 1 | the number of names in the source path |
| each 1 + n | the names of the source path, inside the INI's folder |

The installer finds a component's items by its `source`: the record whose path is the source (a
file), or whose path is inside it (a folder's contents). It installs each at the component's
`destination`, then the record's path after the source, with the installed name last. It reads
the payload with the File Manager 4K at a time and unpacks it as it goes, so neither side ever
holds a whole file. Disk space needed is each fork rounded up to 1K; the progress bar counts
unpacked bytes. Both are summed from the records.

## Building

- `make`: `eup`. `make test`: the host tests (`tests/test_core.c`, `tests/test_cli.sh`).
- `./build-mac.sh`: the installer program and EUP, into `prebuilt/` (Docker). Then `make` again,
  so `eup` carries the new installer program.
- `./release.sh`: the release in `dist/` (Docker, for hfsutils).
