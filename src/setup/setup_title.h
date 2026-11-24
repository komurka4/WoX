/*
 * Phase 338: the setup tool's main window title, "WoX - Setup (VERSION)", VERSION being SETUP_VERSION of
 * versions.cmake (generated into xeen_setup_version.h; never the game's WOX_VERSION). The narrow form is GTK's, the
 * wide form Win32's (a narrow literal concatenated with a wide one is wide, C11 6.4.5). The first-run choice screens
 * and the message boxes keep their plain "WoX - Setup".
 */
#ifndef XEEN_SETUP_TITLE_H
#define XEEN_SETUP_TITLE_H
#include "xeen_setup_version.h"

#define SETUP_MAIN_TITLE "WoX - Setup (" XEEN_SETUP_VERSION ")"
#define SETUP_MAIN_TITLE_W L"WoX - Setup (" XEEN_SETUP_VERSION ")"
#endif
