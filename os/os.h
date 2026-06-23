// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#ifndef __OS_H
#define __OS_H

/****************************************************************************/
/* syslog */

/**
 * Log an informational syslog message.
 * The message should start with a lowercase letter and must not terminate with \n.
 * \param level One of OS_LVL_*.
 * \param format Printf-like format string.
 */
void os_syslog(int level, const char* format, ...) __attribute__((format(attribute_printf, 2, 3)));

/****************************************************************************/
/* signal */

/**
 * Enable or disable signal handling.
 * \param enable 1 to enable signals, 0 to disable.
 */
void os_signal_set(int enable);

/**
 * Initialize signal handling.
 * \param handler_term Signal handler callback for termination signals (SIGTERM, SIGINT).
 * \param handler_hup Signal handler callback for hangup signal (SIGHUP).
 */
void os_signal_init(void (*handler_term)(int sig), void (*handler_hup)(int sig));

/**
 * Restore signal handlers after fork in child process.
 * This resets signals to default handling.
 */
void os_signal_restore_after_fork(void);

/**
 * Get string representation of signal number.
 * \param sig Signal number.
 * \return Signal name string.
 */
const char* os_signal_name(int sig);

/**
 * Global variable to identify if Ctrl+C is pressed.
 * \return 1 if Ctrl+C was pressed/interrupted, 0 otherwise.
 */
int os_signal_interrupt(void);

/****************************************************************************/
/* exec */

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
 * \param argv A NULL-terminated array of strings representing the argument vector.
 *             argv[0] must contain the absolute path to the verified executable.
 * \return A pointer to an initialized OS_FILE structure on success, or NULL on failure.
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
 * \param s Pointer to an array of chars where the string read is stored.
 * \param size Maximum number of characters to be read (including the null character).
 * \param stream Pointer to the OS_FILE tracking context.
 * \return On success, returns the buffer pointer s. If EOF is reached or a read
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
 * \param stream Pointer to the active OS_FILE context to close and free.
 * \return The termination status of the child process on success, or -1 on failure.
 */
int os_pclose(OS_FILE* stream);

/**
 * Spawn a new process with the specified argument vector, optionally capturing stdout and/or stderr.
 * \param argv Array of command line arguments.
 * \param stdout_read_fd Pointer to store file descriptor for stdout, or NULL to redirect to /dev/null.
 * \param stderr_read_fd Pointer to store file descriptor for stderr, or NULL to redirect to /dev/null.
 * \param run_as_user User to run script as (NULL for current user).
 * \return Process ID of spawned process, or -1 on failure.
 */
pid_t os_spawn(char** argv, int* stdout_read_fd, int* stderr_read_fd, const char* run_as_user);

/**
 * Wait for the child process to terminate.
 * \param pid Process ID of the child process.
 * \param status Pointer to store the exit status.
 * \return 0 on success (or child PID on Unix), -1 on failure.
 */
int os_wait(pid_t pid, int* status);

/**
 * Terminate gracefully a process.
 * \param pid Process ID of the process to terminate.
 * \return 0 on success, -1 on failure.
 */
int os_term(pid_t pid);

/**
 * Fork and execute a verified executable, discarding all I/O.
 *
 * Spawns @argv[0] in a new process with stdin, stdout and stderr all
 * redirected to /dev/null. Use this for fire-and-forget tasks where the
 * child's output is not needed.
 *
 * The child is placed in its own process group (setpgid) to isolate it
 * from signals sent to the daemon's process group.
 *
 * \param argv NULL-terminated argument vector. argv[0] must be the absolute path
 *             to the executable.
 * \return The child exit status on success, or -1 on failure.
 */
int os_spawn_and_wait(const char** argv);

/**
 * Execute a system command with optional user context and input.
 * \param command Command to execute.
 * \param run_as_user User to run command as (NULL for current user).
 * \param stdin_text Text to provide as stdin (NULL for no input).
 * \return Exit status of command.
 */
int os_command(const char* command, const char* run_as_user, const char* stdin_text);

/**
 * Execute a script file with specified user context.
 * \param argv Array of command line arguments.
 * \param envp Environment variables (NULL-terminated list of strings).
 * \param run_as_user User to run script as (NULL for current user).
 * \return Exit status of script.
 */
int os_script(char** argv, char** envp, const char* run_as_user);

/**
 * Validates a string for exec.
 * \param str The input string to validate.
 * \return -1 if dangerous characters are detected, 0 otherwise.
 */
int os_validate_exec_input(const char* str);

/****************************************************************************/
/* fs */

/**
 * Physical offset not yet read.
 */
#define FILEPHY_UNREAD_OFFSET 0

/**
 * Special value returned when the file-system doesn't report any offset for unknown reason.
 */
#define FILEPHY_UNREPORTED_OFFSET 1

/**
 * Special value returned when the file doesn't have a real offset.
 * For example, because it's stored in the NTFS MFT.
 */
#define FILEPHY_WITHOUT_OFFSET 2

/**
 * Value indicating real offsets. All offsets greater or equal at this one are real.
 */
#define FILEPHY_REAL_OFFSET 3

/**
 * Get the physical address of the specified file.
 * This is expected to be just a hint and not necessarily correct or unique.
 * \param path The path of the file.
 * \param size The size of the file.
 * \param physical Pointer to store the physical offset.
 * \return 0 on success, -1 on failure.
 */
int filephy(const char* path, uint64_t size, uint64_t* physical);

/****************************************************************************/
/* privileges */

/**
 * Bracketed Privileges System:
 * These functions allow the daemon to run with dropped effective privileges
 * by default (euid/egid set to an unprivileged user like "nobody"), while
 * temporarily escalating to root privileges for specific operations that require
 * administrative permissions (e.g., config changes, executing SnapRAID commands,
 * managing log files).
 */

/**
 * Temporarily acquire root privileges.
 * Restores the effective user and group ID of the calling thread to root (0).
 */
void os_privileges_acquire(void);

/**
 * Release root privileges.
 * Reverts the effective user and group ID of the calling thread to the unprivileged credentials.
 */
void os_privileges_release(void);

/**
 * Drop effective privileges permanently to an unprivileged user (e.g., "nobody").
 * Called after startup/initialization is complete to transition the daemon into
 * the Bracketed Privileges execution mode.
 */
void os_privileges_drop(void);

/****************************************************************************/
/* os */

#define OS_INIT_OPT_AVOID_SLEEP 1 /**< Prevent the system to go to sleep */
#define OS_INIT_OPT_WINFIND 2 /**< Use the legacy Windows FindFirstFile interface instead of the new stream interface */

/**
 * Initializes the system.
 * \param opt System initialization options.
 */
void os_init(unsigned opt);

/**
 * Deinitializes the system.
 */
void os_done(void);

/**
 * Get the os_tick counter value.
 *
 * Note that the frequency is unspecified, because the time measure
 * is meant to be used to compare the ratio between usage times.
 * \return Monotonic clock tick value.
 */
uint64_t os_tick(void);

/**
 * Get the os_tick counter value in millisecond.
 * \return Monotonic clock value in milliseconds.
 */
uint64_t os_tick_ms(void);

/**
 * Get the os_tick counter value in seconds.
 * \return Monotonic clock value in seconds.
 */
uint64_t os_tick_sec(void);

/**
 * Abort the process with a stacktrace.
 */
void os_abort(void) __noreturn;

/**
 * Exit the process with failure.
 */
void os_exit(void) __noreturn;

/**
 * Shutdown the system.
 * \return 0 on success, -1 on failure.
 */
int os_shutdown(void);

/**
 * Clear the screen.
 */
void os_clear(void);

/**
 * Fill memory with pseudo-random values
 * \param ptr Pointer to the memory buffer.
 * \param size Size of the memory buffer.
 * \return 0 on success, -1 on failure.
 */
int os_randomize(void* ptr, size_t size);

/**
 * Get the optimal CPU for the speed test
 * \return Optimal CPU index.
 */
int os_get_optimal_cpu(void);

#endif
