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

/* Global variable to store child process handle */
static HANDLE child_process = NULL;

/* Console control handler - forwards Ctrl+C, Ctrl+Break to child */
static BOOL WINAPI console_handler(DWORD ctrl_type) 
{
	/* if no child, default behavior */
	if (child_process == NULL) 
		return FALSE;

	switch (ctrl_type) {
	case CTRL_C_EVENT :
	case CTRL_BREAK_EVENT :
		/* forward the event to child process */
		GenerateConsoleCtrlEvent(ctrl_type, GetProcessId(child_process));
		return TRUE; /* signal handled, don't terminate parent */
	case CTRL_CLOSE_EVENT :
	case CTRL_LOGOFF_EVENT :
	case CTRL_SHUTDOWN_EVENT :
		/* these can't be easily forwarded, but we can terminate child */
		TerminateProcess(child_process, 1);
		return TRUE; /* signal handled, proceed with termination */
	default:
		return FALSE;
	}
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
		DWORD wait;

		/* install console control handler */
		if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
			fprintf(stderr, "Failed to set console handler\n");
			exit(EXIT_FAILURE);
		}

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		if (!CreateProcessW(app_buffer, cmd_buffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
			fprintf(stderr, "Failed to exec SnapRAID\n");
			exit(EXIT_FAILURE);
		}

		/* store child process handle for signal handler */
		child_process = pi.hProcess;

		wait = WaitForSingleObject(pi.hProcess, INFINITE);
		if (wait != WAIT_OBJECT_0) {
			fprintf(stderr, "WaitForSingleObject failed: %lu\n", (unsigned long)GetLastError());
			TerminateProcess(pi.hProcess, 1);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			return 1;
		}

		/* clear child process handle */
		child_process = NULL;

		if (!GetExitCodeProcess(pi.hProcess, &res)) {
			fprintf(stderr, "GetExitCodeProcess failed: %lu\n", (unsigned long)GetLastError());
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			return 1;
		}

		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);

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

/* global variable to store child PID for signal handler */
static volatile pid_t child_pid = 0;

/* signal handler that forwards signals to child */
static void forward_signal(int sig) 
{
	if (child_pid > 0) {
		kill(child_pid, sig);
	}
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
		struct sigaction sa;

		/* set up signal handler to ignore signals */
		sa.sa_handler = forward_signal;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART; /* restart interrupted system calls */

		/* install forwarding handler for signals */
		sigaction(SIGINT, &sa, NULL); /* Ctrl+C */
		sigaction(SIGQUIT, &sa, NULL); /* Ctrl+\ */
		sigaction(SIGTERM, &sa, NULL); /* termination */
		sigaction(SIGHUP, &sa, NULL); /* hangup */
		sigaction(SIGUSR1, &sa, NULL); /* user-defined 1 */
		sigaction(SIGUSR2, &sa, NULL); /* user-defined 2 */

		pid_t pid = fork();
		if (pid == -1) {
			perror("Failed to fork SnapRAID");
			exit(EXIT_FAILURE);
		}

		if (pid == 0) {
			/* child process */

			/* restore default signal handlers */
			signal(SIGINT, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);
			signal(SIGTERM, SIG_DFL);
			signal(SIGHUP, SIG_DFL);
			signal(SIGUSR1, SIG_DFL);
			signal(SIGUSR2, SIG_DFL);

			execvp(get_argv0(argv[0]), argv);

			/* here it's an error */
			perror("Failed to exec SnapRAID");
			exit(EXIT_FAILURE);
		} else {
			/* parent process */
			int status;

			/* store child PID so signal handler can forward signals */
			child_pid = pid;

			do {
				ret = waitpid(pid, &status, 0);
			} while (ret == -1 && errno == EINTR); /* retry if interrupted by signal */

			if (ret == -1) {
				perror("Failed to wait for SnapRAID");
				exit(EXIT_FAILURE);
			}

			/* clear child PID */
			child_pid = 0;

			/* restore default signal handlers */
			signal(SIGINT, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);
			signal(SIGTERM, SIG_DFL);
			signal(SIGHUP, SIG_DFL);
			signal(SIGUSR1, SIG_DFL);
			signal(SIGUSR2, SIG_DFL);

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

				/* ignore sigpipe to allow spindown even if the terminal is closed */
				signal(SIGPIPE, SIG_IGN);

				snapraid_main(2, spindown_argv);
			}
		}
	}

	return ret;
}
#endif
