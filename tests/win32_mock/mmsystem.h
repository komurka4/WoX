/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Phase 298: <mmsystem.h>'s timer resolution calls for the Win32 window test (tests/win32_mock/windows.h). */
#ifndef PHASE298_MOCK_MMSYSTEM_H
#define PHASE298_MOCK_MMSYSTEM_H
UINT timeBeginPeriod(UINT);
UINT timeEndPeriod(UINT);
#endif
