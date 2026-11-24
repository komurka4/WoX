# Program data (`src/data/`) — generated locally, not distributed

The game engine needs the **initialized data segment of the original `XEEN.DAT`** (35,916 bytes: the game's
texts, format strings, rule and view tables, structures and initial variable values) and the three initial words of
its random number generator. That data is part of the original game, so this source tree does **not** contain it.
Each user generates it once from **their own legally obtained copy of the game**; the build then turns it into C
(`BUILD/generated/program_data.c`). The game itself never reads `XEEN.DAT` at run time.

Until the data is generated, CMake stops with "Program data missing".

## Generating it

Requirements: Python 3 with the [Capstone](https://www.capstone-engine.org/) disassembler module
(`python3 -m pip install capstone`, or your distribution's `python3-capstone` package), and the **unpacked**
`XEEN.DAT` of World of Xeen. The game ships it compressed with PKLITE; unpack it first (next section):

| File | Size | SHA-256 |
|---|---|---|
| unpacked `XEEN.DAT` (the only input the extractor accepts) | 382,608 bytes | `88704c229ada806289dc4a513f77c88e1966fb8e3cf1d23652c748a7a7cd1687` |

From the source root:

```sh
mkdir -p build && tools/data/unpack_xeen_dat.sh /path/to/XEEN.DAT build/XEEN.unpacked.DAT
python3 -B tools/data/extract_program_data.py --xeen-dat build/XEEN.unpacked.DAT \
    --out src/data --meta build/program-data-meta
```

The extractor only reads the file you name. It writes the sources here (`manifest.json`, `pointers.json`,
`tables.json`, `structs.json`, `opaque.json`, `texts/*.tsv`, `records/*.csv`) and descriptive metadata into
`--meta` (not used by the build; keep it outside the source tree). These generated files are listed in
`.gitignore`: **do not commit or redistribute them.**

The build (`tools/data/gen_program_data.py`) checks the result against fixed SHA-256 digests and refuses anything
else:

- data segment image: `76d410cd42a35764532a07b3bf2ea6b67725339af272d797b9d00cacf64355d4`
- random number generator seed words (C91Fh 4BA3h 50D2h): `72063370f3f84b024d4f297ec996f4154e9e398c58c8f9aa6cb885f00ad07795`

To keep the generated data elsewhere, generate it into any directory and configure with
`-DXEEN_PROGRAM_DATA_DIR=/that/directory`.

## The packed `XEEN.DAT`

The game's `XEEN.DAT` is compressed with PKLITE 1.15 (a file of about 156 KB that begins with `MZ` and contains the
text `PKLITE`). `tools/data/unpack_xeen_dat.sh` downloads and builds [deark](https://github.com/jsummers/deark) 1.7.3
(Jason Summers, MIT licence; a build-time tool, not part of WoX), unpacks the file and checks that the result is
exactly the unpacked `XEEN.DAT` above. Verified on the CD-ROM release's `XEEN.DAT` (156,361 bytes, SHA-256
`9a902120ae23885b76d3a51a755d2c6308087730a609efdc8b3adc64328f5a5a`). If your copy is refused, it is a release that
has not been checked: please report it.

**GOG.com release:** use `WORLD\XEEN.DAT` from the installation folder (353,511 bytes, SHA-256
`8bf78b4850d5973cbb063c7dd96b9aaef4e94791d748334187eef74acf528d27`). It is the CD-ROM release's program, already
unpacked by GOG, with an added start stub, a sorted relocation table, the zero tail left out and one text changed
for DOSBox (the CD-swap prompt). `unpack_xeen_dat.sh` recognises it and `tools/data/gog_xeen_dat.py` restores the CD
program exactly (relocations in sorted order, which the extractor accepts: SHA-256
`ef7db634a74059e94ec07bbf6396abcdce7fb7c36c1baa265b170ff7903c1523`); no download is needed. The generated program
data is byte-identical to the CD's. The other `XEEN.DAT` in the GOG installation's main folder (154,658 bytes,
`5762f2c7...`) is a different build without the CD-ROM features and is refused; the disc images `GAME1.GOG` and
`GAME2.GOG` do not contain `XEEN.DAT`.

## What the files describe

The image is the game's live, mutable program data: at start the engine copies it into its 64 KB data segment and
then reads and writes it by offset, exactly as the original program did. Regions alias each other, tables are also
variables and far pointers stay unrelocated, so every byte must stay exactly as extracted. The files only *describe*
the bytes readably (texts as TSV, typed tables, structures, record lists, far-pointer tables and the remaining
opaque bytes as hex); the generator rebuilds the identical image from them.

Structure and table layouts come from `tools/data/ds_structs_p186.py` and the `TABLES` list in
`tools/data/extract_program_data.py` (offsets, element types, counts and the native code that reads them); the
values always come from your `XEEN.DAT`.
