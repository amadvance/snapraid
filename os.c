/*
 * Copyright (C) 2011 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "portable.h"

#include "os.h"

/****************************************************************************/
/* os */

#ifdef __MINGW32__
#define WIN32_ES_SYSTEM_REQUIRED      0x00000001L
#define WIN32_ES_DISPLAY_REQUIRED     0x00000002L
#define WIN32_ES_USER_PRESENT         0x00000004L
#define WIN32_ES_AWAYMODE_REQUIRED    0x00000040L
#define WIN32_ES_CONTINUOUS           0x80000000L
#endif

void os_init(void)
{
#ifdef __MINGW32__
	HMODULE kernel32 = GetModuleHandle("KERNEL32.DLL");
	WORD (WINAPI* WIN32_SetThreadExecutionState)(DWORD);

	if (kernel32) {
		WIN32_SetThreadExecutionState = (void*)GetProcAddress(kernel32, "SetThreadExecutionState");
		if (WIN32_SetThreadExecutionState) {
			/* set the thread execution level to avoid sleep */
			if (WIN32_SetThreadExecutionState(WIN32_ES_CONTINUOUS | WIN32_ES_SYSTEM_REQUIRED | WIN32_ES_AWAYMODE_REQUIRED) == 0) {
				/* retry with the XP variant */
				WIN32_SetThreadExecutionState(WIN32_ES_CONTINUOUS | WIN32_ES_SYSTEM_REQUIRED);
			}
		}
	}
#endif
}

void os_done(void)
{
#ifdef __MINGW32__
	HMODULE kernel32 = GetModuleHandle("KERNEL32.DLL");
	WORD (WINAPI* WIN32_SetThreadExecutionState)(DWORD);

	if (kernel32) {
		WIN32_SetThreadExecutionState = (void*)GetProcAddress(kernel32, "SetThreadExecutionState");
		if (WIN32_SetThreadExecutionState) {
			/* restore the normal execution level */
			WIN32_SetThreadExecutionState(WIN32_ES_CONTINUOUS);
		}
	}
#endif
}

