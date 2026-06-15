// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#ifndef __OS_H
#define __OS_H

/****************************************************************************/
/* signal */

/**
 * Enable or disable signal handling.
 * @param enable 1 to enable signals, 0 to disable
 */
void os_signal_set(int enable);

/**
 * Initialize signal handling.
 */
void os_signal_init(void (*handler_term)(int sig), void (*handler_hup)(int sig));

/**
 * Restore signal handlers after fork in child process.
 * This resets signals to default handling.
 */
void os_signal_restore_after_fork(void);

/****************************************************************************/
/* os */

/**
 * Initializes the system.
 */
void os_init(int opt);

/**
 * Deinitializes the system.
 */
void os_done(void);

/**
 * Get the os_tick counter value.
 *
 * Note that the frequency is unspecified, because the time measure
 * is meant to be used to compare the ratio between usage times.
 */
uint64_t os_tick(void);

/**
 * Get the os_tick counter value in millisecond.
 */
uint64_t os_tick_ms(void);

/**
 * Abort the process with a stacktrace.
 */
void os_abort(void) __noreturn;

/**
 * Clear the screen.
 */
void os_clear(void);

/**
 * Opaque handle tracking a process execution stream and its process ID.
 *
 * This structure encapsulates a standard buffered I/O stream tied to the
 * redirected standard output of a child process spawned via os_popen().
 */
#ifdef __MINGW32__
#define OS_FILE FILE
#else
typedef struct OS_FILE {
	FILE* fp; /**< Standard I/O buffered stream wrapper. */
	pid_t pid; /**< Process ID of the child process. */
} OS_FILE;
#endif

/**
 * Spawns a child process and opens a buffered stream to read its stdout.
 *
 * This function acts as a safer alternative to standard popen(), accepting an
 * explicit argument vector rather than evaluating a shell command string.
 *
 * The child process is completely isolated in its own process group (via setpgid)
 * with its stdin and stderr redirected to /dev/null. The underlying pipe is
 * created with O_CLOEXEC set to prevent leakage across concurrent forks.
 *
 * @param argv  A NULL-terminated array of strings representing the argument vector.
 *              argv[0] must contain the absolute path to the verified executable.
 * @return A pointer to an initialized OS_FILE structure on success, or NULL on failure.
 *         On failure, errno is set appropriately.
 */
OS_FILE* os_popen(const char** argv);

/**
 * Reads a line from an OS execution stream.
 *
 * Reads characters from the specified stream into the buffer string until
 * (size - 1) characters are read, a newline character is read and transferred,
 * or an end-of-file (EOF) condition is encountered. A terminating null
 * character is appended.
 *
 * @param s Pointer to an array of chars where the string read is stored.
 * @param size Maximum number of characters to be read (including the null character).
 * @param stream Pointer to the OS_FILE tracking context.
 *
 * @return On success, returns the buffer pointer @p s. If EOF is reached or a read
 *         error occurs before any characters are read, returns NULL.
 */
char* os_fgets(char* s, int size, OS_FILE* stream);

/**
 * Closes an execution stream and reaps the associated child process.
 *
 * Closes the underlying buffered standard I/O stream, releases the allocated
 * tracking context container, and blocks until the associated child process
 * terminates to prevent the creation of zombie processes.
 *
 * @param stream  Pointer to the active OS_FILE context to close and free.
 *
 * @return The termination status of the child process on success, or -1 on failure.
 *         On failure, errno is set appropriately.
 */
int os_pclose(OS_FILE* stream);

/*
 * Fork and execute a verified executable, discarding all I/O.
 *
 * Spawns @argv[0] in a new process with stdin, stdout and stderr all
 * redirected to /dev/null. Use this for fire-and-forget tasks where the
 * child's output is not needed.
 *
 * The child is placed in its own process group (setpgid) to isolate it
 * from signals sent to the daemon's process group.
 *
 * @argv  NULL-terminated argument vector. argv[0] must be the absolute path
 *        to the executable.
 *
 * Returns the child execit status on success, or -1 on failure.
 */
int os_spawn_and_wait(const char** argv);

/**
 * Get the optimal CPU for the speed test
 */
int os_get_optimal_cpu(void);

/**
 * Fill memory with pseudo-random values
 */
int os_randomize(void* ptr, size_t size);

/**
 * Validates a string for exec.
 * Returns -1 if dangerous characters are detected, 0 otherwise.
 */
int os_validate_exec_input(const char* str);

#endif

