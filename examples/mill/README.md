# Install Mill, made with EUP

`mill.ini` makes Install Mill 2.0.1 as Mill's own installer does, with a Minimal Install added:
Easy, Minimal, and Custom Install, the Plug-Ins as a selectable group, the upgrade from earlier
versions, and the folder's layout. Mill's files aren't in this repository: copy them in from a
build of Mill (its repository, after `./build.sh`, `./build.sh disk`, and `./build.sh installer`):

| Here | From Mill's build |
|---|---|
| `Mill.bin` | `build/Mill.bin` |
| `Internet Services.bin` | `plugins/internet/build/Internet Services.bin` |
| `Web Fonts.bin` | `build/fonts/Web Fonts.bin` |
| `Install icon.bin` | `build/installer/Install.bin` (for its icon) |
| `Mill Plug-In SDK/` | the SDK as its disk has it: each of `tools/kit.py`'s `entries()` written as `NAME.bin` in its folder |

For the SDK, from Mill's repository:

```sh
cd /path/to/eup/examples/mill && mkdir -p "Mill Plug-In SDK" && cd "Mill Plug-In SDK" &&
python3 -B -c '
import os, sys; sys.path.insert(0, "/path/to/mill/tools"); import kit
for parts, name, mb in kit.entries():
    d = os.path.join(*parts) if parts else "."
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, name + ".bin"), "wb").write(mb)'
```

Then `eup build examples/mill -o dist`. These files are ignored by git (`.gitignore`).
