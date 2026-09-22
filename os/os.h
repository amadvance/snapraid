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
typedef struct OS_FILE {
	FILE* fp; /**< Standard I/O buffered stream wrapper. */
	pid_t pid; /**< Process reference of the child process. PID on Unix, process HANDLE on Windows. */
} OS_FILE;

/**
 * Spawns a child process and opens a buffered stream to read its stdout.
 *
 * This function acts as a safer alternative to standard popen(), accepting an
 * explicit argument vector rather than evaluating a shell command string.
 *
 * Standard input and standard error are discarded. Standard output is connected
 * to a pipe exposed through the returned buffered stream. Process creation and
 * argument serialization are delegated to the platform os_spawn() implementation.
 *
 * The stream is opened in text mode on all platforms (setting O_TEXT on Windows),
 * ensuring that CRLF (\r\n) line endings emitted by Windows processes are automatically
 * translated to LF (\n). Callers reading with os_fgets() can assume \n across platforms.
 *
 * The caller is expected to call os_privileges_acquire() before this operation if permission is needed.
 *
 * \param argv A 0-terminated array of strings representing the argument vector.
 *             argv[0] must contain the absolute path to the verified executable.
 * \return A pointer to an initialized OS_FILE structure on success, or 0 on failure.
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
 * Because os_popen() configures the stream in text mode, lines are uniformly
 * terminated with \n (or end at EOF) without trailing \r carriage returns,
 * even on Windows.
 *
 * \param s Pointer to an array of chars where the string read is stored.
 * \param size Maximum number of characters to be read (including the null character).
 * \param stream Pointer to the OS_FILE tracking context.
 * \return On success, returns the buffer pointer s. If EOF is reached or a read
 *         error occurs before any characters are read, returns 0.
 */
char* os_fgets(char* s, int size, OS_FILE* stream);

/**
 * Closes an execution stream and reaps the associated child process.
 *
 * Closes the underlying buffered standard I/O stream, releases the allocated
 * tracking context container, and waits until the associated child process
 * terminates to collect its exit status and release its process resources.
 *
 * \param stream Pointer to the active OS_FILE context to close and free.
 * \return The termination status of the child process on success, or -1 on failure.
 */
int os_pclose(OS_FILE* stream);

/**
 * Spawn a new process with the specified argument vector, optionally capturing stdout and/or stderr.
 *
 * The caller is expected to call os_privileges_acquire() before this operation if permission is needed.
 *
 * Line Ending Semantics:
 * When stdout_read_fd or stderr_read_fd are captured, the resulting file descriptors are opened in raw
 * binary mode (O_BINARY on Windows) without CRLF-to-LF translation. Callers that read directly from
 * these descriptors must explicitly handle \r\n line terminators on Windows (or switch mode via _setmode()).
 * For automatic \r\n to \n translation, use os_popen() and os_fgets().
 *
 * Process Reference Slots (pid_slot):
 * If pid_slot is not 0, process creation and the resulting process reference are published through it
 * to enable safe, race-free process tracking and termination (os_term, os_kill) across concurrent threads.
 * If os_kill() was called on pid_slot, process creation is rejected immediately and this function
 * returns -1 with errno set to ECANCELED without spawning a child process. In contrast, os_term() on an
 * idle slot is a no-op and does not prevent subsequent process creation.
 * The same pid_slot pointer should be passed to os_wait() to unpublish it when the process terminates.
 *
 * \param argv Array of command line arguments.
 * \param stdout_read_fd Pointer to store file descriptor for stdout, or 0 to redirect to /dev/null.
 * \param stderr_read_fd Pointer to store file descriptor for stderr, or 0 to redirect to /dev/null.
 * \param run_as_user User to run script as (0 for current user).
 * \param pid_slot Optional pointer used to publish the process reference, or 0 if concurrent termination is not required.
 * \return Process reference of spawned process, or -1 on failure.
 */
pid_t os_spawn(char** argv, int* stdout_read_fd, int* stderr_read_fd, const char* run_as_user, pid_t* pid_slot);

/**
 * Get the operating system process ID suitable for display or logging.
 * This function is intended for display purposes only and not as a process reference.
 * On Unix the process reference is already the PID.
 * On Windows the process reference is a HANDLE and the actual PID is obtained with GetProcessId().
 * \param pid Process reference returned by os_spawn().
 * \return Operating system process ID, or 0 if unavailable.
 */
uint64_t os_pid(pid_t pid);

/**
 * Get the operating system process ID of a published process slot suitable for display or logging.
 * This function is intended for display purposes only and not as a process reference.
 * Internal synchronization states (including creation in progress or persistent kill) always return 0.
 * \param pid_slot Pointer to a published process reference slot.
 * \return Operating system process ID, or 0 if no process is currently published.
 */
uint64_t os_slot_pid(const pid_t* pid_slot);

/**
 * Wait for the child process to terminate.
 * If pid_slot is not 0, the process reference is unpublished before this function returns.
 * This function does not release the process reference returned by os_spawn().
 * The caller must eventually call os_dispose().
 * \param pid Process reference returned by os_spawn().
 * \param status Pointer to store the exit status.
 * \param pid_slot Optional pointer previously passed to os_spawn(), or 0 if the process was not published.
 * \return Child process reference on success, -1 on failure.
 */
pid_t os_wait(pid_t pid, int* status, pid_t* pid_slot);

/**
 * Release the process reference returned by os_spawn().
 * On Unix this is a no-op. On Windows this closes the process HANDLE.
 * \param pid Process reference returned by os_spawn().
 */
void os_dispose(pid_t pid);

/**
 * Gracefully terminate a published process reference.
 *
 * If a process is currently running in the slot, it is gracefully requested to terminate.
 * If process creation is in progress, the termination request is recorded and applied when the process is published.
 * If no process is active (slot is 0/idle), there is nothing to terminate: this function is a harmless no-op,
 * returning 0 without modifying the slot state or preventing subsequent process creation.
 *
 * The caller retains ownership for spawned processes and must eventually call both os_wait() and os_dispose()
 * as appropriate.
 * \param pid_slot Pointer to the published process reference slot.
 * \return 0 on success or if no process is active, -1 on failure.
 */
int os_term(pid_t* pid_slot);

/**
 * Forcibly terminate a published process reference.
 *
 * If a process is currently running in the slot, it is terminated immediately.
 * If process creation is in progress, the kill request is recorded and the child is killed upon publication.
 * If no process is active (slot is idle), the kill request is recorded persistently in the slot so that
 * any future process creation attempted with this slot is rejected immediately without spawning.
 *
 * The slot retains the persistent kill state until explicitly reset by the caller (e.g. *pid_slot = 0).
 * The caller retains ownership for spawned processes and must eventually call both os_wait() and os_dispose()
 * as appropriate.
 *
 * \param pid_slot Pointer to the published process reference slot.
 * \return 0 on success or if recorded in the slot, -1 on failure.
 */
int os_kill(pid_t* pid_slot);

/**
 * Fork and execute a verified executable, discarding all I/O.
 *
 * Spawns @argv[0] via os_spawn() in a new process with stdin, stdout and stderr all
 * redirected to /dev/null. Use this for fire-and-forget tasks where the
 * child's output is not needed. Because all I/O streams are discarded, no stream
 * or line-ending translation is performed.
 *
 * The child is placed in its own process group (setpgid) to isolate it
 * from signals sent to the daemon's process group.
 *
 * The caller is expected to call os_privileges_acquire() before this operation if permission is needed.
 *
 * \param argv 0-terminated argument vector. argv[0] must be the absolute path
 *             to the executable.
 * \return The child exit status on success, or -1 on failure.
 */
int os_spawn_and_wait(const char** argv);

/**
 * Execute a system command with optional user context and input.
 * The caller is expected to call os_privileges_acquire() before this operation if permission is needed.
 * If pid_slot is not 0, the child process is published for concurrent termination while blocked.
 * If os_kill() was called on pid_slot, process creation is rejected immediately and this function
 * returns -1 with errno set to ECANCELED without executing the command.
 * \param command Command to execute.
 * \param run_as_user User to run command as (0 for current user).
 * \param stdin_text Text to provide as stdin (0 for no input).
 * \param timeout_sec Execution timeout in seconds (0 for no timeout).
 * \param pid_slot Optional pointer to publish the process reference while active, or 0.
 * \return Exit status of command, or -1 on failure.
 */
int os_command(const char* command, const char* run_as_user, const char* stdin_text, uint64_t timeout_sec, pid_t* pid_slot);

/**
 * Execute a script file with specified user context.
 * The caller is expected to call os_privileges_acquire() before this operation if permission is needed.
 * If pid_slot is not 0, the child process is published for concurrent termination while blocked.
 * If os_kill() was called on pid_slot, process creation is rejected immediately and this function
 * returns -1 with errno set to ECANCELED without executing the script.
 * \param argv Array of command line arguments.
 * \param envp Environment variables (0-terminated list of strings).
 * \param run_as_user User to run script as (0 for current user).
 * \param timeout_sec Execution timeout in seconds (0 for no timeout).
 * \param pid_slot Optional pointer to publish the process reference while active, or 0.
 * \return Exit status of script, or -1 on failure.
 */
int os_script(char** argv, char** envp, const char* run_as_user, uint64_t timeout_sec, pid_t* pid_slot);

/**
 * Validates a string for exec.
 * \param str The input string to validate.
 * \return -1 if dangerous characters are detected, 0 otherwise.
 */
int os_validate_exec_input(const char* str);

/****************************************************************************/
/* fs */

/**
 * Make a pathname absolute using the current working directory.
 *
 * The pathname is not required to exist and symbolic links are not resolved.
 * The resolved_path buffer must be at least PATH_MAX bytes and must not overlap path.
 *
 * \param path Input pathname.
 * \param resolved_path Destination buffer.
 * \return resolved_path on success, 0 on failure with errno set.
 */
char* absolutepath(const char* restrict path, char* restrict resolved_path);

/**
 * Invalid inode number.
 *
 * Value 0 is used to represent an invalid or unavailable inode number.
 * While 0 is not strictly guaranteed to be invalid by standards, in practice
 * it is never used for valid files. And even if a file happens to have inode 0,
 * the program will simply process that specific file without using its inode.
 */
#define INODE_INVALID 0

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
 * Drop effective privileges permanently to an unprivileged user
 * (e.g., "nobody").
 * Called after startup/initialization is complete to transition the daemon into
 * the Bracketed Privileges execution mode.
 *
 * On failure the process credentials may have been partially changed and the
 * caller must terminate the process.
 *
 * \return 0 on success, -1 on failure.
 */
int os_privileges_drop(void);

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
 * Sleep for the specified number of microseconds.
 *
 * Loops in bounded intervals and returns on the first error or signal interruption.
 * \param usec Number of microseconds to sleep.
 * \return 0 on success, -1 on error.
 */
int os_usleep(uint64_t usec);

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

