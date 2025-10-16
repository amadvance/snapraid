/*
 * Copyright (C) 2025 Andrea Mazzoleni
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

#include "snapraid.h"

#define MODE_DEFAULT 0
#define MODE_SPINDOWN 1

#ifdef _WIN32
#define COMMAND_LINE_MAX 32767

static int needs_quote(const WCHAR* arg)
{
	while (*arg) {
		if (*arg == L' ' || *arg == L'\t' || *arg == L'"')
			return 1;
		++arg;
	}
	
	return 0;
}

#define charcat(c) \
	do { \
		if (pos + 1 >= size) \
			return -1; \
		cmd[pos++] = (c); \
	} while (0)

static int argcat(WCHAR* cmd, int size, int pos, const WCHAR* arg)
{
	/* space separator */
	if (pos != 0)
		charcat(L' ');

	if (!needs_quote(arg)) {
		while (*arg)
			charcat(*arg++);
		return pos;
	}

	charcat(L'"');

	while (*arg) {
		int bl = 0;
		while (*arg == L'\\') {
			++arg;
			++bl;
		}

		if (*arg == 0) {
			bl = bl * 2;
			while (bl--)
				charcat(L'\\');
		} else if (*arg == '"') {
			bl = bl * 2 + 1;
			while (bl--)
				charcat(L'\\');
			charcat(L'"');
			++arg;
		} else {
			while (bl--)
				charcat(L'\\');
			charcat(*arg);
			++arg;
		}
        }

	/* ending quote */
	charcat(L'"');

	return pos;
}

static char* argutf8(const WCHAR* arg)
{
	size_t len = wcslen(arg);
	char* utf8_arg;
	DWORD utf8_len;
	DWORD res;

	utf8_len = WideCharToMultiByte(CP_UTF8, 0, arg, len + 1 /* include final null */, 0, 0, 0, 0);

	utf8_arg = malloc(utf8_len);
	if (!utf8_arg)
		return 0;

	res = WideCharToMultiByte(CP_UTF8, 0, arg, len + 1 /* include final null */, utf8_arg, utf8_len, 0, 0);
	if (res != utf8_len)
		return 0;

	return utf8_arg;
}

int main(int argc, char* argv[]) 
{
	int wide_argc;
	WCHAR** wide_argv;
	int utf8_argc;
	char** utf8_argv;
	WCHAR app_buffer[COMMAND_LINE_MAX];
	WCHAR cmd_buffer[COMMAND_LINE_MAX];
	DWORD res;
	int i;
	int pos;
	int mode;
	int ret;
	
	(void)argc;
	(void)argv;

	res = GetModuleFileNameW(NULL, app_buffer, sizeof(app_buffer));
	if (res == 0 || res >= sizeof(app_buffer)) {
		exit(EXIT_FAILURE);
	}

	wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
	if (!wide_argv) {
		exit(EXIT_FAILURE);
	}

	utf8_argc = 0;
	utf8_argv = malloc((wide_argc + 1) * sizeof(void*));
	if (!utf8_argv) {
		exit(EXIT_FAILURE);
	}

	pos = argcat(cmd_buffer, sizeof(cmd_buffer), 0, app_buffer);
	if (pos < 0) {
		exit(EXIT_FAILURE);
	}

	utf8_argv[0] = argutf8(app_buffer);
	if (!utf8_argv[0]) {
		exit(EXIT_FAILURE);
	}
	utf8_argc = 1;

	mode = MODE_DEFAULT;
	for (i = 1; i < wide_argc; i++) {
		if (wcscmp(wide_argv[i], L"-s") == 0 || wcscmp(wide_argv[i], L"--spin-down-on-error") == 0) {
			mode = MODE_SPINDOWN;
		} else {
			pos = argcat(cmd_buffer, sizeof(cmd_buffer), 0, wide_argv[i]);
			if (pos < 0) {
				exit(EXIT_FAILURE);
			}
			utf8_argv[utf8_argc] = argutf8(wide_argv[i]);
			if (!utf8_argv[utf8_argc]) {
				exit(EXIT_FAILURE);
			}
			++utf8_argc;
		}
	}
	utf8_argv[utf8_argc] = 0;
	cmd_buffer[pos] = 0;

	LocalFree(wide_argv);

	if (mode == MODE_DEFAULT) {
		ret = snapraid_main(utf8_argc, utf8_argv);
	} else {
		STARTUPINFOW si;
		PROCESS_INFORMATION pi;

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		if (!CreateProcessW(app_buffer, cmd_buffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
			exit(EXIT_FAILURE);
		}

		WaitForSingleObject(pi.hProcess, INFINITE);

		GetExitCodeProcess(pi.hProcess, &res);

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		ret = res;

		if (ret != 0) {
			char* spindown_argv[3];
			spindown_argv[0] = utf8_argv[0];
			spindown_argv[1] = "down";
			spindown_argv[2] = 0;
			snapraid_main(2, spindown_argv);
		}
	}

	for(i = 0;i < utf8_argc; ++i) 
		free(utf8_argv[i]);
	free(utf8_argv);

	return ret;
}
#else

char full_argv0[PATH_MAX];

const char* get_argv0(const char* argv0) 
{
	ssize_t len = readlink("/proc/self/exe", full_argv0, sizeof(full_argv0) - 1);
	if (len != -1) {
		full_argv0[len] = '\0';
		return full_argv0;
	} else {
#ifdef __APPLE__
		uint32_t size = sizeof(full_argv0);
		if (_NSGetExecutablePath(full_argv0, &size) == 0)
			return full_argv0;
#endif
	}
	return argv0;
}

int main(int argc, char* argv[]) 
{
	int mode;
	int i, j;
	int ret;

	mode = MODE_DEFAULT;
	j = 1;
	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--spin-down-on-error") == 0 ) {
			mode = MODE_SPINDOWN;
		} else {
			argv[j] = argv[i];
			++j;
		}
	}
	argc = j;
	argv[argc] = 0;

	if (mode == MODE_DEFAULT) {
		ret = snapraid_main(argc, argv);
	} else {
		pid_t pid = fork();
		if (pid == -1) {
			perror("Failed to fork the SnapRAID child process");
			exit(EXIT_FAILURE);
		}

		if (pid == 0) {
			const char* argv0 = get_argv0(argv[0]);

			execvp(argv0, argv);

			perror("Failed to exec SnapRAID");

			/* here it's an error */
			exit(EXIT_FAILURE);
		} else {
			int status;

			/* parent process - wait for child */
			if (waitpid(pid, &status, 0) == -1) {
				exit(EXIT_FAILURE);
			}

			if (WIFEXITED(status)) {
				ret = WEXITSTATUS(status);
			} else if (WIFSIGNALED(status)) {
				/* terminated by signal */
				ret = 128 + WTERMSIG(status);
			} else {
				ret = -1;
			}

			if (ret != 0) {
				char* spindown_argv[3];
				spindown_argv[0] = argv[0];
				spindown_argv[1] = "down";
				spindown_argv[2] = 0;
				snapraid_main(2, spindown_argv);
			}
		}
	}

	return ret;
}
#endif
