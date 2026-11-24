# Executable versions

`wox` (Windows `wox.exe`, the game) and `setup` (`setup.exe`, the settings tool) each have their own version number.
Both are written in one tracked file, [`versions.cmake`](../versions.cmake), and nowhere else:

```cmake
set(WOX_VERSION "1.0.0")
set(SETUP_VERSION "1.0.0")
```

## Changing a version

1. Edit the one value in `versions.cmake` (the other product is unaffected).
2. Rebuild. An existing build directory re-configures by itself (CMake tracks the included file); only the changed
   product's version-dependent objects and its executable are rebuilt:

   ```sh
   cmake --build build/dev --target wox setup          # any configured build directory
   tools/windows/package.sh                            # Windows folder: build/windows-package/dist
   tools/linux/package.sh                              # Linux folder: build/linux-package/dist
   python3 -B tools/windows/check_versions.py build/windows-package/dist   # PE version resources, icons, manifest
   ```

**Syntax** (checked at configure time by [`cmake/xeen_version.cmake`](../cmake/xeen_version.cmake); an invalid value
stops the configuration with a message): three or four decimal components `MAJOR.MINOR.PATCH[.BUILD]`, each 0..65535
(a Windows version field is 16 bits), without leading zeros, nothing else (no `v` prefix, suffix such as `-rc1`,
spaces or build metadata). No timestamp, git hash or automatic increment is added.

## Where each version appears

| Product | Surface | Text |
|---|---|---|
| wox | `wox --version` | the version and a newline on stdout, status 0; nothing else runs (no file is read or written; no data, display or audio needed). Windows: a GUI-subsystem program, so read it through a redirection or pipe (`wox.exe --version > v.txt`), see [CONFIGURATION.md](CONFIGURATION.md#messages-woxlog-and-errors-phase-272) |
| wox | `wox.log` | each window session's first entry `session start: WoX (1.0.0)` |
| wox | `wox.exe` Properties > Details | FileVersion and ProductVersion `1.0.0`, numeric `1,0,0,0` |
| setup | the main settings window's title | `WoX - Setup (1.0.0)` (Win32 and GTK; also after Initial setup...) |
| setup | `setup.exe` Properties > Details | FileVersion and ProductVersion `1.0.0`, numeric `1,0,0,0` |

## How it is built

`CMakeLists.txt` (`xeen_version_target`) generates, per product, in its own build-tree directory
`BUILD/version/wox/` or `BUILD/version/setup/`: a header (`xeen_wox_version.h`: `XEEN_WOX_VERSION`;
`xeen_setup_version.h`: `XEEN_SETUP_VERSION`) from [`cmake/xeen_version.h.in`](../cmake/xeen_version.h.in) and, for
Windows, a `VERSIONINFO` resource (`wox_version.rc`, `setup_version.rc`) from
[`cmake/xeen_version.rc.in`](../cmake/xeen_version.rc.in) (FileDescription, FileVersion, InternalName,
OriginalFilename, ProductName `WoX`, ProductVersion; no company or copyright fields). Only the product's own
sources see its header, through the interface targets `xeen_wox_version` (the host sources: `wox` and its
tests) and `xeen_setup_version` (`setup`); no engine library does. The CMake `project()` version (0.1.0) and the
synth libraries' shared-library versions are separate and unchanged.

Tests: `phase338_versions` (the source, the syntax, `--version`, `--help`, the log, the binaries' texts) and
`phase338_setup_title` (the title macros).
