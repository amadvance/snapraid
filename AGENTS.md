# SnapRAID Development Guide

This document provides guidance for agentic coding agents working on the SnapRAID codebase.

## Project Structure

### Core Source Files (`cmdline/`)

The CLI is organized into focused C modules:

| Modules | Purpose |
|---|---|
| `main.c`, `snapraid.h` / `snapraid.c` | Program entry points, option parsing, and CLI command dispatch |
| `state.h` / `state.c` | Global state (`snapraid_state`), config file parsing, and content file read/write |
| `elem.h` / `elem.c` | Core data model (files, links, dirs, disks, parity, filters) and filter logic |
| `scan.c`, `import.h` / `import.c` | Multi-threaded directory scanner and file-move hash importer |
| `search.h` / `search.c`, `locate.h` / `locate.c` | Path pattern matching and file lookup / path location commands |
| `io.h` / `io.c` | Parallel read-ahead and write-behind I/O queue management |
| `parity.h` / `parity.c` | Parity file operations supporting split parity files |
| `handle.h` / `handle.c` | File handles abstraction for data disk I/O with POSIX fadvise settings |
| `bw.h` / `bw.c` | Bandwidth token-bucket rate limiter |
| `stream.h` / `stream.c` | Buffered stream abstraction with CRC32 integrity checks |
| `sync.c`, `scrub.c`, `check.c`, `status.c` | CLI commands for core operations (synchronize, integrity scrub, check, health status) |
| `pool.c`, `dup.c`, `list.c`, `touch.c`, `rehash.c` | CLI commands for pooling layouts, finding duplicates, listing files, updating tracking/hashes |
| `device.c`, `dry.c` | Disk device geometry/UUID querying, and dry-run execution simulation |
| `murmur3.c`, `spooky2.c`, `museair.c` (and tests) | Non-cryptographic hash implementations (MurmurHash3, SpookyHash, MuseAir) and tests |
| `support.h` / `support.c`, `app.h` | Locks, logging, thread wrappers, global constants |
| `str.h` / `str.c` | Path/string utilities |
| `memory.h` / `memory.c` | Safe memory allocation and tracking helpers |
| `util.h` / `util.c` | Low-level utilities (CRC32 calculations, numeric formatting, hash helpers) |
| `unixapp.c`, `mingwapp.c` | Platform-specific implementations of system info, process management, and services |
| `thermal.h` / `thermal.c` | Temperature monitoring to prevent hardware overheating |
| `selftest.c`, `speed.c` | Self-test command (`test`) and CPU RAID parity performance benchmark (`-T`) |
| `mktest.c`, `mkstream.c` | Standalone test utility tools for recovery and stream validation |

### OS Abstraction Files (`os/`)

The platform abstraction layer is separated into `os/`. Note that `os/` acts as a library layer, so having unused functions (e.g., platform helper functions not currently invoked by the main CLI application) is expected.

| Module | Purpose |
|--------|---------|
| `os.h` | Core OS-independent interface declarations (syslog, signals, execution, threads, mutexes) |
| `portable.h` | Platform detection macros and compatibility definitions |
| `unix.c/h` | UNIX implementation of OS abstraction layer (fork, exec, signals, thread primitives) |
| `mingw.c/h` | Windows implementation of OS abstraction layer (process creation, registry, thread primitives) |

### Key Data Structures

| Struct | Defined in | Description |
|---|---|---|
| `snapraid_state` | `state.h` | Global program state: all disks, parity levels, filter list, options, hash kind, progress |
| `snapraid_option` | `state.h` | Runtime options (force flags, sort order, IO advise mode, bandwidth limit, etc.) |
| `snapraid_disk` | `elem.h` | One data disk: name, mount dir, device id, UUID, file/link/dir hash tables and lists, parity extent tree |
| `snapraid_file` | `elem.h` | A tracked file: sub-path, size, mtime, inode, physical offset, block vector |
| `snapraid_block` | `elem.h` | One parity block: state (`BLK/CHG/REP/DELETED/EMPTY`) + hash |
| `snapraid_extent` | `elem.h` | Contiguous run of file blocks mapped to contiguous parity positions |
| `snapraid_filter` | `elem.h` | One include/exclude rule: pattern, root scope, `is_disk`, `is_abs`, `is_dir`, direction |
| `snapraid_parity` | `elem.h` | One parity level: up to `SPLIT_MAX=8` split files, total/free blocks |
| `snapraid_io` | `io.h` | Parallel I/O controller: buffer pool, worker threads for each disk |
| `snapraid_worker` | `io.h` | One I/O thread: reads or writes one disk at a time using a task ring buffer |

### Block States

| Constant | Value | Meaning |
|---|---|---|
| `BLOCK_STATE_EMPTY` | 0 | No file mapped here; parity position is zero-filled |
| `BLOCK_STATE_BLK` | 1 | File block with valid hash and up-to-date parity |
| `BLOCK_STATE_CHG` | 2 | File block changed; hash may be old (pre-sync); parity not yet updated |
| `BLOCK_STATE_REP` | 3 | File block hashed (from copy heuristic); parity not yet updated |
| `BLOCK_STATE_DELETED` | 4 | File deleted; block retains old hash for parity recovery; parity not yet zeroed |

### Crash-Recovery Invariants

SnapRAID is designed to recover from an interruption, including `SIGKILL`, at any point. It achieves this by reading recovery data from multiple snapshots, checking file metadata when reading, and validating data with hashes. Account for this design before treating a non-atomic transition or mixed snapshot lifecycle as an integrity defect.

### Build System

- `configure.ac`: Autoconf script (detects systemd vs BSD init)
- `Makefile.am`: Source file lists, dependencies, install hooks (including rules for generating documentation)
- `uncrustify.cfg`: Code formatting rules for main application source files
- `linux.cfg`: Code formatting rules for `raid/` directory and its subdirectories
- Run `make doc` to regenerate all manual pages (`*.1`) and text manuals (`*.txt`)
- Always use parallel compilation with `make -j$(nproc)` instead of plain `make`
- To cross-compile for Windows x64: run `make clean && ./configure.windows-x64 && make -j$(nproc)`
- The Windows build needs to be tested only when modifying Windows-specific code (e.g., `os/mingw.*`, `cmdline/mingwapp.c`, or Windows-specific `#ifdef` paths)
- After configuring for Windows (`./configure.windows-x64`), it is not necessary to reconfigure back for Linux; you can leave it configured for Windows
- Always run `make clean` when switching build configurations or targets between Linux (`./configure`) and Windows (`./configure.windows-x64`)

## Code Style Guidelines

#### Code Style

- **Language**: The codebase uses **C99 standard** (not C11/C++)
- **Format**: Enforce via `uncrustify -c uncrustify.cfg --no-backup *.c *.h` (or `uncrustify -c linux.cfg --no-backup *.c *.h` for `raid/` directory and subdirectories)
- **Naming**: Snake_case for functions, UPPER_CASE for macros/constants
- **Indentation**: Tabs for indentation, no alignment (existing codebase style)
- **Comments**: C-style `/** */` for multiline comments; C `/* first letter lowercase */` for single-line inline notes
- **Critical Comments**: Always add a comment at non-obvious critical points, especially around data-integrity invariants, crash recovery, fallback behavior, concurrency, and fatal versus best-effort error handling. Explain why the logic is required, not merely what the code does.
- **Headers**: All `.h` files have include guards (`#ifndef __NAME_H`)
- **Preferences**: Use 0 instead of NULL and '\0'
- **Preferences**: Use prefix ++variable and --variable instead of postfix variable++ and variable-- where both are equivalent
- **Safety Checks**: Avoid adding safety checks for conditions that never happen
- **Simplicity First**: Always choose the simplest design with the minimal number of state variables, branches, and lines of code.
- **Stack Over Heap**: Prefer stack-allocated fixed-size buffers over dynamic memory allocation (`malloc`/`free`) whenever the upper bound is small, fixed, and known at compile time.
- **Commit Messages**: Every time a change is done, a single line commit description should be provided for that change
- **Git Commits**: Never commit changes to git.

#### Error Handling & Memory Management

- **Return codes**: `0` means no-error; `< 0` means error; positive for specific results
- **Logging**: Use `log_fatal()`, `log_tag()`, `msg_info()`, `msg_progress()`, `msg_verbose()` from `support.h`
- **Tag Escaping**: When emitting structured tags with `log_tag()`, always wrap file paths, directory paths, link targets, disk names, filter patterns, and arbitrary string fields with `esc_tag()` to prevent colons (e.g., Windows drive letters `C:`), backslashes, or newlines from breaking colon-delimited tag parsing.
- **Allocation**: Always use custom `malloc_nofail()`, `calloc_nofail()`, `nalloc_nofail()`, or `strdup_nofail()` wrappers (which handle OOM by aborting)
- **Path Manipulation**: Never use standard unsafe string functions (`strcpy`, `strcat`, `strcmp`) on paths. Always use the platform-correct, size-bounded wrappers in `support.h` (e.g., `pathcpy()`, `pathcat()`, `pathcmp()`, `pathslash()`).
- **TommyDS Iteration**: For `tommy_list`, iterate using:
  `tommy_node* node = list.head; while (node) { void* obj = node->data; ... node = node->next; }`
  Or use callback iteration helpers: `tommy_list_foreach()`, `tommy_list_foreach_arg()`, `tommy_hashdyn_foreach()`, etc.
- **Cleanup**: Use `goto` pattern for cleanup in complex functions when appropriate
- **Fatal paths**: Use `exit(EXIT_FAILURE)` for unrecoverable errors; `os_abort()` for internal inconsistencies

#### Threading

- **Compile-time guard**: `#if HAVE_THREAD` wraps all thread-specific code
- **Mutexes**: Always use the custom `thread_mutex_init/destroy/lock/unlock()` wrappers
- **IO workers**: The `snapraid_io` / `snapraid_worker` infrastructure handles parallel disk I/O via a pool of reader and writer threads

#### Testing Changes

- Never run `make check` because it's too expansive
- Run `./snapraid test` for the regression test
- The Windows build needs to be tested only when modifying Windows-specific code
- Verify error paths and recovery scenarios (the test suite covers aborted sync, UUID changes, disk failures)
