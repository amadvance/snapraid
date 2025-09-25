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

#ifndef __SUPPORT_H
#define __SUPPORT_H

/****************************************************************************/
/* lock */

/**
 * Initialize and destroy the locks.
 */
void lock_init(void);
void lock_done(void);

/**
 * Lock used for printf.
 *
 * In Windows printf() is not atomic, and multiple threads
 * will have output interleaved.
 *
 * Note that even defining __USE_MINGW_ANSI_STDIO the problem persists.
 *
 * See for example:
 *
 * Weird output when I use pthread and printf.
 * http://stackoverflow.com/questions/13190254/weird-output-when-i-use-pthread-and-printf
 *
 * This is also required in other OS because we split output in stdlog in
 * two fprintf calls.
 */
void lock_msg(void);
void unlock_msg(void);

/**
 * Lock used for memory counter.
 */
void lock_memory(void);
void unlock_memory(void);

/****************************************************************************/
/* log */

/**
 * Fatal error messages.
 *
 * Messages printed before an early termination.
 *
 * These messages go in the log file and in stderr unconditionally.
 */
void log_fatal(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Unexpected error messages.
 *
 * Messages reporting error conditions that don't prevent the program to run.
 *
 * Some of them could be also serious errors, like "silent errors".
 * In such case, the summary result is always printed as error,
 * and we are sure to notify the user in some way.
 *
 * These messages go in the log file if specified, otherwise they go in stderr.
 */
void log_error(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Expected error messages, without fallback to stderr.
 *
 * These errors are "someway" expected, and then they never go to screen.
 * For example, when undeleting missing files, the messages for missing files
 * are not shown.
 *
 * These messages go in the log file if specified, otherwise they are lost.
 */
void log_expected(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Tag messages.
 *
 * Messages are in tag format, like "tag:entry:...".
 *
 * These messages never go on the screen, but only in the log file if specified.
 *
 * Note that this function, allows not \n terminated strings.
 *
 * These messages are buffered. Use msg_flush() to flush them.
 */
void log_tag(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Flush the log.
 */
void log_flush(void);

/**
 * Pointer to log function.
 */
typedef void fptr(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/****************************************************************************/
/* message */

/**
 * Message levels.
 *
 * The levels control the amount of information printed on the screen.
 * Note that log_fatal(), log_error(), log_expected() and log_tag() are not affected by this option.
 *
 * From the most quiet to the most verbose.
 */
#define MSG_STATUS -3
#define MSG_INFO -2
#define MSG_PROGRESS -1
#define MSG_BAR 0
#define MSG_VERBOSE 1

/**
 * Selected message level.
 */
extern int msg_level;

/**
 * State messages.
 *
 * Messages that tell what the program is doing or did, but limited to few lines.
 * They are status information, and summary results.
 */
void msg_status(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Info messages.
 *
 * Messages that tell what was done.
 * Potentially a lot of messages are possible. They can still be on the screen,
 * as losing them we don't lose information.
 *
 * These messages never go in the log file, because there is always a corresponding log_tag().
 */
void msg_info(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Progress messages.
 *
 * Message that tell the progress of program.
 *
 * These messages also go in the log file.
 */
void msg_progress(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Progress bar messages.
 *
 * Message that show the percentage of the progress.
 *
 * These messages never go in the log file.
 *
 * These messages are buffered. Use msg_flush() to flush them.
 */
void msg_bar(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Verbose messages.
 *
 * Message that tell what is already expected.
 *
 * These messages also go in the log file.
 */
void msg_verbose(const char* format, ...) __attribute__((format(attribute_printf, 1, 2)));

/**
 * Flush the output.
 */
void msg_flush(void);

/****************************************************************************/
/* print */

/**
 * Print a repeated char.
 */
void printc(char c, size_t pad);

/**
 * Print a string with right space padding.
 */
void printr(const char* str, size_t pad);

/**
 * Print a string with left space padding.
 */
void printl(const char* str, size_t pad);

/**
 * Print a probability with space padding.
 */
void printp(double v, size_t pad);

/****************************************************************************/
/* string */

#define ESC_MAX (PATH_MAX*2 + 1)

/**
 * Escape a string for the log.
 *
 * \param buffer Preallocated buffer of ESC_MAX size.
 *
 * Chars ':', '\n', '\r' and '\' are escaped to '\d', '\\n', '\\r' and '\\'.
 */
const char* esc_tag(const char* str, char* buffer);

/**
 * Escape a string for the shell.
 *
 * \param buffer Preallocated buffer of ESC_MAX size.
 */
const char* esc_shell_multi(const char** str_map, unsigned str_max, char* buffer);
static inline const char* esc_shell(const char* str, char* buffer)
{
	return esc_shell_multi(&str, 1, buffer);
}

/**
 * Polish a string.
 *
 * Not printable chars are replaced by spaces.
 *
 * Note that the passed string is modified.
 */
char* strpolish(char* s);

/**
 * Split a string in multiple tokens separated by delimiters.
 *
 * Multiple delimiters are grouped together.
 */
unsigned strsplit(char** split_map, unsigned split_max, char* line, const char* delimiters);

/****************************************************************************/
/* path */

/**
 * Copy a path limiting the size.
 * Abort if too long.
 */
void pathcpy(char* dst, size_t size, const char* src);

/**
 * Concatenate a path limiting the size.
 * Abort if too long.
 */
void pathcat(char* dst, size_t size, const char* src);

/**
 * Concatenate a path limiting the size knowing the length.
 * Abort if too long.
 */
void pathcatl(char* dst, size_t dst_len, size_t size, const char* src);

/**
 * Concatenate a path limiting the size.
 * Abort if too long.
 */
void pathcatc(char* dst, size_t size, char c);

/**
 * Import a path limiting the size.
 * In Windows all the backslash are converted to the C standard of forward slash.
 * Abort if too long.
 */
void pathimport(char* dst, size_t size, const char* src);

/**
 * Export a path limiting the size.
 * In Windows all the C slashes are converted to the Windows backslash.
 * Abort if too long.
 */
void pathexport(char* dst, size_t size, const char* src);

/**
 * Print a path.
 * Abort if too long.
 */
void pathprint(char* dst, size_t size, const char* format, ...) __attribute__((format(attribute_printf, 3, 4)));

/**
 * Ensure the presence of a terminating slash, if it isn't empty.
 * Abort if too long.
 */
void pathslash(char* dst, size_t size);

/**
 * Cut everything after the latest slash.
 *
 * If the string doesn't contain any slash, it returns the empty string.
 */
void pathcut(char* dst);

/**
 * Compare two paths.
 * In Windows it's case insensitive and assumes \ equal at /.
 */
int pathcmp(const char* a, const char* b);

/****************************************************************************/
/* file-system */

/**
 * Create all the ancestor directories if missing.
 * The file name, after the last /, is ignored.
 */
int mkancestor(const char* file);

/**
 * Change the modification time of an open file.
 */
int fmtime(int f, int64_t mtime_sec, int mtime_nsec);

/**
 * Change the modification time of a file or link.
 * Note that links are NOT deferenced.
 */
int lmtime(const char* path, int64_t mtime_sec, int mtime_nsec);

/****************************************************************************/
/* advise */

/**
 * Advise modes.
 */
#define ADVISE_DEFAULT 0 /**< Default mode. */
#define ADVISE_NONE 1 /**< Bare read/write mode. */
#define ADVISE_SEQUENTIAL 2 /**< Sequential mode. */
#define ADVISE_FLUSH 3 /**< Flush mode. */
#define ADVISE_FLUSH_WINDOW 4 /**< Flush mode with a window of 8MB. */
#define ADVISE_DISCARD 5 /**< Discard the cache after every operation. */
#define ADVISE_DISCARD_WINDOW 6 /**< Discard the cache with a window of 8MB. */
#define ADVISE_DIRECT 7 /**< Direct mode. */

#define ADVISE_WINDOW_SIZE (8 * 1024 * 1024) /**< Window size. */

struct advise_struct {
	int mode;
	data_off_t dirty_begin;
	data_off_t dirty_end;
};

void advise_init(struct advise_struct* advise, int mode);
int advise_flags(struct advise_struct* advise);
int advise_open(struct advise_struct* advise, int f);
int advise_write(struct advise_struct* advise, int f, data_off_t offset, data_off_t size);
int advise_read(struct advise_struct* advise, int f, data_off_t offset, data_off_t size);

/****************************************************************************/
/* memory */

/**
 * Return the size of the allocated memory.
 */
size_t malloc_counter_get(void);

/**
 * Safe malloc.
 * If no memory is available, it aborts.
 */
void* malloc_nofail(size_t size);

/**
 * Safe cmalloc.
 * If no memory is available, it aborts.
 */
void* calloc_nofail(size_t count, size_t size);

/**
 * Safe strdup.
 * If no memory is available, it aborts.
 */
char* strdup_nofail(const char* str);

/**
 * Helper for printing an error about a failed allocation.
 */
void malloc_fail(size_t size);

/****************************************************************************/
/* smartctl */

/**
 * Read smartctl attributes from a stream.
 * Return 0 on success.
 */
int smartctl_attribute(FILE* f, const char* file, const char* name, uint64_t* smart, char* serial, char* vendor, char* model);

/**
 * Flush smartctl output from a stream.
 */
int smartctl_flush(FILE* f, const char* file, const char* name);

/****************************************************************************/
/* thread */

#if HAVE_THREAD
/**
 * Control when to signal the condition variables.
 *
 * Default is inside the mutex.
 *
 * Ensure to change that before starting any thread.
 */
extern int thread_cond_signal_outside;

/**
 * Thread wrappers to handle error conditions.
 */
void thread_mutex_init(thread_mutex_t* mutex);
void thread_mutex_destroy(thread_mutex_t* mutex);
void thread_mutex_lock(thread_mutex_t* mutex);
void thread_mutex_unlock(thread_mutex_t* mutex);
void thread_cond_init(thread_cond_t* cond);
void thread_cond_destroy(thread_cond_t* cond);
void thread_cond_signal(thread_cond_t* cond);
void thread_cond_broadcast(thread_cond_t* cond);
void thread_cond_wait(thread_cond_t* cond, thread_mutex_t* mutex);
void thread_cond_signal_and_unlock(thread_cond_t* cond, thread_mutex_t* mutex);
void thread_cond_broadcast_and_unlock(thread_cond_t* cond, thread_mutex_t* mutex);
void thread_create(thread_id_t* thread, void* (* func)(void *), void *arg);
void thread_join(thread_id_t thread, void** retval);
void thread_yield(void);
#endif

#endif

