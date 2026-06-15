// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "portable.h"

#ifndef __MINGW32__ /* Only for Unix */

#include "os.h"
#include "support.h"

/****************************************************************************/
/* signal */

void os_signal_restore_after_fork(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGHUP, &sa, 0);
	/* do not restore SIGPIPE */

	/* ensure signals are unblocked */
	sigset_t mask;
	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL); /* cannot use pthread_sigmask after fork */
}

void os_signal_init(void (*handler_term)(int sig), void (*handler_hup)(int sig))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler_term;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; /* use the SA_RESTART to automatically restart interrupted system calls */

	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);

	sa.sa_handler = handler_hup;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; /* use the SA_RESTART to automatically restart interrupted system calls */

	sigaction(SIGHUP, &sa, 0);

	sa.sa_handler = SIG_IGN; /* ignore the signal */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGPIPE, &sa, 0);
}

/****************************************************************************/
/* unix */

#if !HAVE_EACCESS
int eaccess(const char* pathname, int mode)
{
	return faccessat(AT_FDCWD, pathname, mode, AT_EACCESS);
}
#endif

#if HAVE_POSIX_FADVISE
int posix_fadvise_wrapper(int fd, off_t offset, off_t len, int advice)
{
	int ret = posix_fadvise(fd, offset, len, advice);

	if (ret == ENOENT)
		return ENOSYS;

	return ret;
}
#endif

int open_noatime(const char* file, int flags)
{
#ifdef O_NOATIME
	int f = open(file, flags | O_NOATIME);

	/* only root is allowed to use O_NOATIME, in case retry without it */
	if (f == -1 && errno == EPERM)
		f = open(file, flags);
	return f;
#else
	return open(file, flags);
#endif
}

int dirent_hidden(struct dirent* dd)
{
	return dd->d_name[0] == '.';
}

size_t direct_size(void)
{
	long size;

	size = sysconf(_SC_PAGESIZE);

	if (size == -1) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "No page size\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	return size;
}

const char* stat_desc(struct stat* st)
{
	if (S_ISREG(st->st_mode))
		return "regular";
	if (S_ISDIR(st->st_mode))
		return "directory";
	if (S_ISCHR(st->st_mode))
		return "character";
	if (S_ISBLK(st->st_mode))
		return "block-device";
	if (S_ISFIFO(st->st_mode))
		return "fifo";
	if (S_ISLNK(st->st_mode))
		return "symlink";
	if (S_ISSOCK(st->st_mode))
		return "socket";
	return "unknown";
}

/****************************************************************************/
/* exec */

/*
 * Securely verify and open an executable file.
 *
 * Performs a series of security checks on the file at @exec_path before
 * returning an open file descriptor suitable for use with fexecve(2), or
 * falling back to execve(2) on systems that lack it.
 *
 * Security model:
 *   - @exec_path must be absolute.
 *   - The path is resolved via realpath(3) to canonicalize it and eliminate
 *     symlinks before any further checks are performed.
 *   - The parent directory is opened with O_PATH | O_NOFOLLOW to pin its
 *     inode, and the file is opened with openat(2) relative to that pinned
 *     descriptor. This closes the TOCTOU window between path resolution
 *     and file open.
 *   - Both the parent directory and the file must be owned by root or the
 *     daemon's real/effective UID, must not be world-writable, and must not
 *     be group-writable by a group other than the daemon's real/effective GID.
 *   - The file must be a regular file with at least one execute bit set.
 *   - The setuid and setgid bits must not be set.
 *   - On systems with fexecve(2) support, the returned fd is opened without
 *     O_CLOEXEC so it can be passed directly to fexecve(2). On other systems
 *     O_CLOEXEC is set and execve(2) must be used with @resolved_path.
 *
 * @exec_path     Absolute path to the executable to verify.
 * @resolved_path Caller-allocated buffer of at least PATH_MAX bytes. On
 *                success, filled with the canonicalized path from realpath(3).
 *
 * Returns an open file descriptor (>= 0) on success. The caller is
 * responsible for closing it. Returns -1 on any verification failure;
 * the specific reason is emitted via log_error(...).
 */
static int verify_executable(const char* exec_path, char* resolved_path)
{
	struct stat st;
	uid_t process_uid, process_euid;
	gid_t process_gid, process_egid;

	process_uid = getuid();
	process_euid = geteuid();
	process_gid = getgid();
	process_egid = getegid();

	/* verify path is absolute */
	if (exec_path[0] != '/') {
		log_error(EINVAL, "Path %s must be absolute", exec_path);
		return -1;
	}

	/* resolve the path to prevent symlink attacks */
	if (!realpath(exec_path, resolved_path)) {
		log_error(errno, "Failed to resolve %s. %s.", exec_path, strerror(errno));
		return -1;
	}

	char* last_slash = strrchr(resolved_path, '/');
	if (last_slash == 0) {
		log_error(EINVAL, "Relative execution of %s not allowed", resolved_path);
		return -1;
	}
	if (last_slash == resolved_path) {
		log_error(EINVAL, "Root dir execution of %s not allowed", resolved_path);
		return -1;
	}

	const char* exec_name = last_slash + 1;
	if (exec_name[0] == 0) {
		log_error(EINVAL, "No executable name in %s", exec_path);
		return -1;
	}

	char dir_path[PATH_MAX];
	size_t dir_len = last_slash - resolved_path;
	memcpy(dir_path, resolved_path, dir_len);
	dir_path[dir_len] = 0;

	int dir_fd = open(dir_path, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (dir_fd < 0) {
		log_error(errno, "Failed to open directory %s. %s.", dir_path, strerror(errno));
		return -1;
	}

	if (fstat(dir_fd, &st) != 0) {
		log_error(errno, "Failed to stat directory %s. %s.", dir_path, strerror(errno));
		close(dir_fd);
		return -1;
	}

	/* directory must be owned by root or the daemon's real user */
	if (st.st_uid != process_uid && st.st_uid != process_euid && st.st_uid != 0) {
		log_error(EINVAL, "Directory %s owner must match the daemon owner or be root", dir_path);
		close(dir_fd);
		return -1;
	}

	/* directory must be not group-writable unless group matches daemon */
	if ((st.st_mode & S_IWGRP) && st.st_gid != process_gid && st.st_gid != process_egid && st.st_gid != 0) {
		log_error(EINVAL, "Directory %s must be not group-writable unless group matches daemon owner or root", dir_path);
		close(dir_fd);
		return -1;
	}

	/* directory must be not world-writable */
	if (st.st_mode & S_IWOTH) {
		log_error(EINVAL, "Directory %s must be not world-writable", dir_path);
		close(dir_fd);
		return -1;
	}

	/*
	 * Open the executable
	 */
	int fd = openat(dir_fd, exec_name, O_RDONLY
#if !HAVE_FEXECVE
			| O_CLOEXEC /* with fexecve cannot use O_CLOEXEC (Close on Exec) */
#endif
	);
	if (fd < 0) {
		log_error(errno, "Failed to open %s. %s.", resolved_path, strerror(errno));
		close(dir_fd);
		return -1;
	}

	close(dir_fd);

	/* get the file handle (TOCTOU Protection) */
	if (fstat(fd, &st) == -1) {
		log_error(errno, "Failed to stat %s. %s.", resolved_path, strerror(errno));
		close(fd);
		return -1;
	}

	/* ensure it's a regular file */
	if (!S_ISREG(st.st_mode)) {
		log_error(EINVAL, "File %s is not a regular file", resolved_path);
		close(fd);
		return -1;
	}

	/* ensure it has execute permissions */
	if (!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
		log_error(EINVAL, "File %s is not an executable", resolved_path);
		close(fd);
		return -1;
	}

	/* must be owned by root or the daemon's real user */
	if (st.st_uid != process_uid && st.st_uid != process_euid && st.st_uid != 0) {
		log_error(EINVAL, "File %s owner must match the daemon owner or be root", resolved_path);
		close(fd);
		return -1;
	}

	/* must be not group-writable unless group matches daemon */
	if ((st.st_mode & S_IWGRP) && st.st_gid != process_gid && st.st_gid != process_egid && st.st_gid != 0) {
		log_error(EINVAL, "File %s must be not group-writable unless group matches daemon owner or root", resolved_path);
		close(fd);
		return -1;
	}

	/* must be not world-writable */
	if (st.st_mode & S_IWOTH) {
		log_error(EINVAL, "File %s must be not world-writable", resolved_path);
		close(fd);
		return -1;
	}

	/* must be not setuid / setgid */
	if (st.st_mode & (S_ISUID | S_ISGID)) {
		log_error(EINVAL, "File %s has setuid/setgid bits set", resolved_path);
		close(fd);
		return -1;
	}

	return fd;
}

int os_validate_exec_input(const char* str)
{
	/* reject paths trying to go up levels */
	if (strstr(str, "..") != 0)
		return -1;

	/* reject inputs starting with '-' to prevent Flag Injection */
	if (str[0] == '-')
		return -1;

	return 0;
}

/*
 * Scrubbed environment
 * Only provide the bare essentials.
 */
static char* const envp_scrubbed[] = {
	"PATH="
#ifdef __APPLE__
	"/opt/homebrew/bin:"
#endif
	"/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
	"TERM=dumb",
	"LANG=C",
	"IFS= \t\n",
	NULL
};

static int pipe_cloexec(int pipefd[2])
{
#ifdef HAVE_PIPE2
	return pipe2(pipefd, O_CLOEXEC);
#else
	if (pipe(pipefd) < 0)
		return -1;

	for (int i = 0; i < 2; i++) {
		int flags = fcntl(pipefd[i], F_GETFD);
		if (flags < 0)
			goto bail;

		if (fcntl(pipefd[i], F_SETFD, flags | FD_CLOEXEC) < 0)
			goto bail;
	}

	return 0;

bail:
	close(pipefd[0]);
	close(pipefd[1]);
	return -1;
#endif
}

/*
 * Fork and execute a verified executable, capturing stdout.
 *
 * Spawns @argv[0] in a new process with stdout connected to a pipe whose
 * read end is returned in @stdout_fd. stdin and stderr are redirected to
 * /dev/null. Use this when the child's error output needs to be read and
 * processed by the daemon.
 *
 * The child is placed in its own process group (setpgid) to isolate it
 * from signals sent to the daemon's process group. The daemon can terminate
 * the child and all its descendants with kill(-pid, SIGTERM).
 *
 * The pipe is created with O_CLOEXEC on both ends. The write end is closed
 * in the parent after fork. The read end's buffer is reduced to 4096 bytes
 * to improve read responsiveness on low-volume output.
 *
 * @argv       NULL-terminated argument vector. argv[0] must be the absolute
 *             path to the executable.
 * @stdout_fd  On success, set to the read end of the stdout pipe. The caller
 *             is responsible for closing it when done.
 *
 * Returns the child PID on success, or -1 on failure.
 */
static pid_t os_spawn_stdout(const char** argv, int* stdout_fd)
{
	char resolved_path[PATH_MAX];
	int out_pipe[2];
	pid_t pid;

	int fd = verify_executable(argv[0], resolved_path);
	if (fd < 0) {
		return -1;
	}

	if (pipe_cloexec(out_pipe) < 0) {
		close(fd);
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		close(out_pipe[0]);
		close(out_pipe[1]);
		close(fd);
		return -1;
	}

	if (pid == 0) {
		/* child process */

		/*
		 * Create a new process group for the child.
		 * This isolates the child from signals sent to the daemon's process group
		 * and allows the daemon to kill this process and all its future children
		 * (the entire group) using kill(-pid, SIGTERM).
		 */
		setpgid(0, 0);

		/* io sandboxing */
		int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
		if (null_fd < 0)
			_exit(126);

		/* stdin  -> /dev/null */
		/* stdout -> pipe      */
		/* stderr -> /dev/null */
		if (dup2(null_fd, STDIN_FILENO) < 0
			|| dup2(out_pipe[1], STDOUT_FILENO) < 0
			|| dup2(null_fd, STDERR_FILENO) < 0)
			_exit(126);

		close(out_pipe[0]);
		close(out_pipe[1]);

		/* if the fd we opened is not one of the standard ones, close it */
		if (null_fd > STDERR_FILENO)
			close(null_fd);

#if defined(CLOSE_RANGE_CLOEXEC) && defined(HAVE_CLOSE_RANGE)
		/*
		 * Set all fd to be closed on exec as extra safety measure
		 *
		 * fallback: if it fails, we assume to be still safe, as all fds and
		 * sockets should be already created with CLOEXEC.
		 */
		close_range(3, fd - 1, CLOSE_RANGE_CLOEXEC);
		close_range(fd + 1, ~0U, CLOSE_RANGE_CLOEXEC);
#endif

		/* restore and unblock signals */
		os_signal_restore_after_fork();

		/* use the resolved path for execution */
		argv[0] = resolved_path;

		/*
		 * Direct Execution via File Descriptor
		 * The kernel uses the shebang in the FD to find the interpreter.
		 */
#if HAVE_FEXECVE
		fexecve(fd, (char**)argv, envp_scrubbed);
#else
		/* fallback: unfortunately must use the path */
		execve(resolved_path, (char**)argv, envp_scrubbed);
#endif
		_exit(127);
	}

	/* parent */
	close(fd);

#ifdef F_SETPIPE_SZ /* not available on macOS */
	/* set the pipe buffer to the minimum to improve responsiveness */
	if (fcntl(out_pipe[0], F_SETPIPE_SZ, 4096) == -1) {
		log_error(errno, "Failed to set pipe size. %s.", strerror(errno));
		/* log non-fatal error or ignore */
	}
#endif

	close(out_pipe[1]);

	*stdout_fd = out_pipe[0];
	return pid;
}

static int os_wait(pid_t pid, int* status)
{
	int ret;

	do {
		ret = waitpid(pid, status, 0);
	} while (ret == -1 && errno == EINTR);

	return ret;
}

int os_spawn_and_wait(const char** argv)
{
	char resolved_path[PATH_MAX];
	pid_t pid;

	int fd = verify_executable(argv[0], resolved_path);
	if (fd < 0)
		return -1;

	pid = fork();
	if (pid < 0) {
		log_error(errno, "Fork failed. %s.", strerror(errno));
		close(fd);
		return -1;
	}

	if (pid == 0) {
		/* child process */
		/*
		 * Create a new process group for the child.
		 * This isolates the child from signals sent to the daemon's process group
		 * and allows the daemon to kill this process and all its future children
		 * (the entire group) using kill(-pid, SIGTERM).
		 */
		setpgid(0, 0);

		/* io sandboxing */
		int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
		if (null_fd < 0)
			_exit(126);

		/* stdin  -> /dev/null */
		/* stdout -> /dev/null */
		/* stderr -> /dev/null */
		if (dup2(null_fd, STDIN_FILENO) < 0
			|| dup2(null_fd, STDOUT_FILENO) < 0
			|| dup2(null_fd, STDERR_FILENO) < 0)
			_exit(126);

		/* if the fd we opened is not one of the standard ones, close it */
		if (null_fd > STDERR_FILENO)
			close(null_fd);

#if defined(CLOSE_RANGE_CLOEXEC) && defined(HAVE_CLOSE_RANGE)
		/*
		 * Mark all remaining open fds (>= 3) as close-on-exec as an
		 * extra safety measure against leaking inherited fds into the
		 * child executable.
		 * Note: if fd == 3, the first range (3, fd-1) = (3, 2) is
		 * intentionally invalid and will fail harmlessly; fd itself
		 * is preserved for fexecve.
		 */
		close_range(3, fd - 1, CLOSE_RANGE_CLOEXEC);
		close_range(fd + 1, ~0U, CLOSE_RANGE_CLOEXEC);
#endif

		/* restore and unblock signals */
		os_signal_restore_after_fork();

		/* use the resolved path for execution */
		argv[0] = resolved_path;

		/*
		 * Direct Execution via File Descriptor
		 * The kernel uses the shebang in the FD to find the interpreter.
		 */
#if HAVE_FEXECVE
		fexecve(fd, (char**)argv, envp_scrubbed);
#else
		/* fallback: unfortunately must use the path */
		execve(resolved_path, (char**)argv, envp_scrubbed);
#endif
		_exit(127);
	}

	/* parent */
	close(fd);

	int status;
	int ret = os_wait(pid, &status);
	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_error(errno, "Wait %s failed. %s.", resolved_path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		return 128 + WTERMSIG(status);
	}

	return -1;
}

OS_FILE* os_popen(const char** argv)
{
	int stdout_fd = -1;
	pid_t pid = os_spawn_stdout(argv, &stdout_fd);
	if (pid < 0) {
		return 0;
	}

	FILE* fp = fdopen(stdout_fd, "r");
	if (!fp) {
		/* LCOV_EXCL_START */
		int saved_errno = errno;
		close(stdout_fd);
		int status;
		os_wait(pid, &status);
		errno = saved_errno;
		return 0;
		/* LCOV_EXCL_STOP */
	}

	OS_FILE* os_file = malloc_nofail(sizeof(OS_FILE));

	os_file->fp = fp;
	os_file->pid = pid;

	return os_file;
}

char* os_fgets(char* s, int size, OS_FILE* stream)
{
	return fgets(s, size, stream->fp);
}

int os_pclose(OS_FILE* stream)
{
	/* leep handles locally to free container block immediately */
	FILE* fp = stream->fp;
	pid_t pid = stream->pid;
	free(stream);

	/* close buffered stream, releasing the underlying fd */
	fclose(fp);

	int status = 0;
	if (os_wait(pid, &status) < 0) {
		return -1;
	}

	return status;
}

/****************************************************************************/
/* os */

uint64_t os_tick(void)
{
#if HAVE_MACH_ABSOLUTE_TIME
	/* for Mac OS X */
	return mach_absolute_time();
#elif HAVE_CLOCK_GETTIME && (defined(CLOCK_MONOTONIC) || defined(CLOCK_MONOTONIC_RAW))
	/* for Linux */
	struct timespec tv;

	/* nanosecond precision with clock_gettime() */
#if defined(CLOCK_MONOTONIC_RAW)
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &tv) != 0) {
#else
	if (clock_gettime(CLOCK_MONOTONIC, &tv) != 0) {
#endif
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	return tv.tv_sec * 1000000000ULL + tv.tv_nsec;
#else
	/* other platforms */
	struct timeval tv;

	/* microsecond precision with gettimeofday() */
	if (gettimeofday(&tv, 0) != 0) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	return tv.tv_sec * 1000000ULL + tv.tv_usec;
#endif
}

uint64_t os_tick_ms(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, 0) != 0) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	return tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

int os_randomize(void* ptr, size_t size)
{
	int f;
	ssize_t ret;

	f = open("/dev/urandom", O_RDONLY);
	if (f == -1) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = read(f, ptr, size);
	if (ret < 0 || (size_t)ret != size) {
		/* LCOV_EXCL_START */
		close(f);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (close(f) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

void os_clear(void)
{
	/* ANSI codes */
	printf("\033[H"); /* cursor at topleft */
	printf("\033[2J"); /* clear screen */
}

/* LCOV_EXCL_START */
void os_abort(void)
{
#if HAVE_BACKTRACE && HAVE_BACKTRACE_SYMBOLS
	void* stack[32];
	char** messages;
	unsigned size;
	unsigned i;
#endif

	printf("Stacktrace of " PACKAGE " v" VERSION);
#ifdef _linux
	printf(", linux");
#endif
#ifdef __GNUC__
	printf(", gcc " __VERSION__);
#endif
	printf(", %d-bit", (int)sizeof(void*) * 8);
	printf(", PATH_MAX=%d", PATH_MAX);
	printf("\n");

#if HAVE_BACKTRACE && HAVE_BACKTRACE_SYMBOLS
	size = backtrace(stack, 32);

	messages = backtrace_symbols(stack, size);

	for (i = 1; i < size; ++i) {
		const char* msg;

		if (messages)
			msg = messages[i];
		else
			msg = "<unknown>";

		printf("[bt] %02u: %s\n", i, msg);

		if (messages) {
			int ret;
			char addr2line[1024];
			size_t j = 0;
			while (msg[j] != '(' && msg[j] != ' ' && msg[j] != 0)
				++j;

			snprintf(addr2line, sizeof(addr2line), "addr2line %p -e %.*s", stack[i], (unsigned)j, msg);

			ret = system(addr2line);
			if (WIFEXITED(ret) && WEXITSTATUS(ret) != 0)
				printf("exit:%d\n", WEXITSTATUS(ret));
			if (WIFSIGNALED(ret))
				printf("signal:%d\n", WTERMSIG(ret));
		}
	}
#endif

	printf("Please report this error to the SnapRAID Issues:\n");
	printf("https://github.com/amadvance/snapraid/issues\n");

	abort();
}
/* LCOV_EXCL_STOP */

void os_init(int opt)
{
	(void)opt;
}

void os_done(void)
{
}


#endif

