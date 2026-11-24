# The released executables' version numbers (Phase 338): the only place they are written. Each is independent;
# edit one and rebuild (an existing build directory re-configures by itself). Syntax: three or four decimal
# components, each 0..65535 without leading zeros (cmake/xeen_version.cmake checks it). The Windows resources'
# numeric tuple is derived from the same value (a fourth component 0 when three are given).
set(WOX_VERSION "1.0.0")
set(SETUP_VERSION "1.0.0")
