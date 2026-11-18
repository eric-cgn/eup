# Changes

## 1.4

- StuffIt Expander no longer leaves a `.ungz` file beside an installer it decodes from `.hqx` or `.bin`. The installer's data fork now starts with `EUP `, not a gzip stream.
- The release disk's Read Me carries the part number, #1E6-DE01-V14.

## 1.3

- Reworded messages in the installer, EUP, and `eup`. Check messages start with a capital letter.
- The installed folder goes in a free place in its window, not over another icon.
- Folders the installer makes open by Icon.
- The installer's windows and Choose… are centered on the main screen.
- The installer has a menu bar (About, desk accessories, Quit) and works alongside other applications under MultiFinder and System 7, also while installing.
- Folders the installer makes get a window, so System 6 no longer stacks their icons in one column.
- `version_string`: a version to show, such as `2.0.1-dev-46a0304`.
- An upgrade never removes what `keep` names. Naming an item in both is a warning, not an error.
- The CI checks that `prebuilt/` is what the source builds.
- Choose… takes an earlier install as the place: Save with its folder's name, or with the folder open. Standard File refused the name before. On System 7 it has New Folder.
- If a file the installer would replace or remove is open, such as the application, it stops before writing anything, naming the file and the application to quit. 1.0 went on without a word and left `~Name` beside the file.
- A file that can't be replaced is put back as it was, and the installer says which.
- `require` in `[upgrade]`: software that must be installed first, found as `detect` finds it. Without it, the installer refuses to install and writes nothing. A `require` without `detect` is an error.
- The release disk's Read Me carries the part number, #1E6-DE01-V13.
- The release disk is named EUP Program Disk.
- The stated minimum is System 6.0.3, where an EUP installer was tested.

## 1.0

First release.
