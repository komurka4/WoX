# Contributing to WoX

Thank you for your interest. Bug reports, ideas and code are all welcome.

## Reporting a bug

Open an issue using the **Bug report** form. The most useful reports include:

- the version (`wox --version`; on Windows `wox.exe --version > version.txt`) and your operating system;
- which release of the game you use (CD-ROM, GOG.com, ...);
- what you did, what you expected (if you know how the original behaves, say so) and what WoX did;
- if the game stopped: both lines of the message, including `Technical details: ...`;
- your `wox.cfg` (enabled add-ons and bug fixes matter) and the relevant part of `wox.log`.

Please **never attach original game files, saves you do not own, or ROM images**.

## Proposing a change

For anything larger than a small fix, open an issue first to discuss the idea. Then:

1. Build and test as described in [docs/BUILDING.md](docs/BUILDING.md) (`ctest --test-dir build/dev`).
2. Keep the original game's behaviour as the default. A deliberate difference belongs in an **add-on**
   (`src/addon/`) or a **bug fix** (`src/bugfix/`), switched off by default. See
   [docs/DEVELOPING.md](docs/DEVELOPING.md).
3. Follow [docs/CODING_STYLE.md](docs/CODING_STYLE.md) and keep the original-routine references in comments.
4. Open a pull request. The CI checks (build on Linux and for Windows, tests) must pass. CI builds with
   placeholder program data, so it cannot test everything that needs the real game data: describe how you tested.

## What must never be committed

The program data generated from `XEEN.DAT` (`src/data/`), original game files, saves, disc images, CD audio, ROM
images, or code, data, texts and graphics copied from the original game beyond what the engine already contains.
`.gitignore` covers the usual files, but please check `git status` before committing.

## Licence

By contributing, you agree that your contribution is licensed under the project's licence, the GNU General Public
License version 3 or later ([LICENSE](LICENSE)). Code from other projects must come with a compatible licence and
its notices; mention it in the pull request so [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) can be updated.
