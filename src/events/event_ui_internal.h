/*
 * The event UI module's internals shared with its test support (src/test_support/event_ui_test_support.c, Phase
 * 284). Not part of the public API (include/xeen/event_ui.h) and not installed.
 */
#ifndef XEEN_EVENT_UI_INTERNAL_H
#define XEEN_EVENT_UI_INTERNAL_H
#include "xeen/event_ui.h"
/* Nonzero when two presentation requests are identical (kind, face, mode and both texts). */
int xeen_ui_same_request(const xeen_ui_request *a, const xeen_ui_request *b);
#endif
