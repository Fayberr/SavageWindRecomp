# Converting Wii U / Cemu saves

`tools/botw_save_convert.py` turns a Breath of the Wild save from the Wii U (for example one played in Cemu) into a
save Savage Wind can load. It also converts between Switch revisions, and back to the Wii U layout (`--to wiiu`).
It needs Python 3 and nothing else. It reads only your own files and ships no game data.

## Use it

1. Close the game and Savage Wind Configuration, and keep a copy of the save you convert.
2. Find the Wii U profile folder. In Cemu it is
   `mlc01/usr/save/00050000/<title id>/user/80000001`, the folder with the numbered slots `0`, `1`, ... and
   `option.sav`. The tool also finds it when you give it a folder above.
3. Run it from the Savage Wind folder (the one with `extracted/`), writing into a save folder:

   ```
   python tools/botw_save_convert.py --src <Cemu profile folder> --dst save/save_0
   ```

   `--bootup` picks another `Pack/Bootup.pack` (default `extracted/romfs/Pack/Bootup.pack`), `--slots 0,2` converts
   only some slots, and `--dry-run` converts in memory and only reports.
4. Start Savage Wind and continue from the title screen.

It never overwrites: files that already exist in the destination are skipped. `--overwrite` renames them to
`<name>.bak-<time>` first.

## What it does

A save is a handful of small files per slot. `game_data.sav` holds every game flag, `caption.sav` the metadata the
load screen shows, `caption.jpg` its picture; `option.sav` holds the settings, `tracker/` the Hero's Path, and `album/` and `pict_book/` the photos.

The flag files are the same on both consoles apart from three things, and the tool handles each:

- **Byte order.** The Wii U is big endian, the Switch little endian. Every 32-bit value is swapped. Text flags are
  stored as plain bytes and are not swapped.
- **Revision.** The first word of the file is the game revision (`0x471B` for the Wii U's 1.5.0, `0x4730` for
  Switch 1.9.0). Flags are matched by the CRC32 of their name, so a flag that only exists in the target revision gets
  its default value, and one that no longer exists is dropped. The report counts both.
- **Types and sizes.** Which flags exist, their types, array lengths and defaults are read from the game's own
  `GameData/savedataformat` and `gamedata` tables inside `Pack/Bootup.pack`. Nothing is hard coded, so another game
  revision works with its own `Bootup.pack`.

`option.sav` is converted the same way, but the two consoles have different options, so settings that do not exist
on both sides take the Switch default.

`tracker/` (the Hero's Path) is byte-swapped in 32-bit words, except its 8 byte header, which holds four plain bytes
and two 16-bit fields. `album/` and `pict_book/` are plain JPEG files and are copied. `--tracker skip` leaves the
Hero's Path out (the game starts a new one) and `--tracker copy` copies it unchanged.

Slots keep their number. The game decides which slot "Continue" loads.

## Tests

`python tools/test_botw_save_convert.py` runs the tests, which build their flag table by hand and need no game files.
Against real files the tool reproduces a Switch save byte for byte when converting Switch to Switch, and a published
100% save in both Wii U and Switch versions agrees flag for flag after conversion, apart from flags that exist in
only one revision and text flags (the other file stores those word-swapped; a native 1.9.0 save keeps text as plain bytes, so
the tool does too).
