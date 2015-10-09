bin_PROGRAMS = snapraid
check_PROGRAMS = mktest mkstream

snapraid_SOURCES = \
	raid/raid.c \
	raid/check.c \
	raid/module.c \
	raid/tables.c \
	raid/int.c \
	raid/x86.c \
	raid/intz.c \
	raid/x86z.c \
	raid/helper.c \
	raid/memory.c \
	raid/test.c \
	raid/tag.c \
	tommyds/tommy.c \
	cmdline/snapraid.c \
	cmdline/util.c \
	cmdline/stream.c \
	cmdline/support.c \
	cmdline/elem.c \
	cmdline/state.c \
	cmdline/scan.c \
	cmdline/sync.c \
	cmdline/check.c \
	cmdline/dry.c \
	cmdline/rehash.c \
	cmdline/scrub.c \
	cmdline/status.c \
	cmdline/dup.c \
	cmdline/list.c \
	cmdline/pool.c \
	cmdline/parity.c \
	cmdline/handle.c \
	cmdline/nano.c \
	cmdline/device.c \
	cmdline/fnmatch.c \
	cmdline/selftest.c \
	cmdline/speed.c \
	cmdline/import.c \
	cmdline/search.c \
	cmdline/mingw.c \
	cmdline/unix.c

noinst_HEADERS = \
	raid/raid.h \
	raid/helper.h \
	raid/internal.h \
	raid/cpu.h \
	raid/gf.h \
	raid/combo.h \
	raid/memory.h \
	raid/test.h \
	tommyds/tommyarray.h \
	tommyds/tommyarrayblk.h \
	tommyds/tommyarrayblkof.h \
	tommyds/tommychain.h \
	tommyds/tommyhash.h \
	tommyds/tommyhashdyn.h \
	tommyds/tommylist.h \
	tommyds/tommytree.h \
	tommyds/tommytypes.h \
	tommyds/tommyarray.c \
	tommyds/tommyarrayblk.c \
	tommyds/tommyarrayblkof.c \
	tommyds/tommyhash.c \
	tommyds/tommyhashdyn.c \
	tommyds/tommylist.c \
	tommyds/tommytree.c \
	cmdline/portable.h \
	cmdline/snapraid.h \
	cmdline/util.h \
	cmdline/stream.h \
	cmdline/support.h \
	cmdline/elem.h \
	cmdline/state.h \
	cmdline/parity.h \
	cmdline/handle.h \
	cmdline/murmur3.c \
	cmdline/murmur3test.c \
	cmdline/spooky2.c \
	cmdline/spooky2test.c \
	cmdline/fnmatch.h \
	cmdline/import.h \
	cmdline/search.h \
	cmdline/mingw.h \
	cmdline/unix.h

mktest_SOURCES = \
	cmdline/mktest.c \
	cmdline/mingw.c \
	cmdline/support.c

mkstream_SOURCES = \
	cmdline/mkstream.c \
	cmdline/mingw.c \
	cmdline/support.c \
	cmdline/util.c \
	cmdline/stream.c \
	raid/memory.c

EXTRA_DIST = \
	autogen.sh \
	README AUTHORS HISTORY INSTALL COPYING TODO CHECK INSTALL.windows \
	snapraid.d snapraid.1 snapraid.txt \
	test/test-par1.conf \
	test/test-par2.conf \
	test/test-par3.conf \
	test/test-par4.conf \
	test/test-par5.conf \
	test/test-par6.conf \
	test/test-par6-hole.conf \
	snapraid.conf.example \
	configure.windows-x86 configure.windows-x64 snapraid.conf.example.windows \
	acinclude.m4 \
	tommyds/LICENSE \
	raid/COPYING \
	raid/test/Makefile \
	raid/test/fulltest.c \
	raid/test/selftest.c \
	raid/test/speedtest.c \
	raid/test/invtest.c

man_MANS = snapraid.1

clean-local:
	rm -f valgrind.log callgrind.log cachegrind.log
	rm -rf bench
	rm -f test*.log output.log stream*.bin
	rm -f cmdline/*.gcda cmdline/*.gcno cmdline/*.gcov
	rm -f raid/*.gcda raid/*.gcno raid/*.gcov
	rm -f tommyds/*.gcda tommyds/*.gcno tommyds/*.gcov
	rm -rf cov
	rm -f lcov.info
	rm -f gmon.out

maintainer-clean-local:
	rm -f snapraid.1 snapraid.txt

# Automatic testing

# Base options
# --test-skip-device
# Is required to allow testing in the same disk
#
# --test-skip-self
# It just makes the test a little faster
#
# --test-force-order-alpha
# Ensures to process files always in the same order despites
# the inode, physical location, and dir order assigned by the OS.
CHECKFLAGS_BASE = --test-skip-device --test-skip-self --test-force-order-alpha --test-force-progress

# Physical offset options
CHECKFLAGS_PHYSICAL = --test-skip-device --test-skip-self --test-force-order-physical -q -q -q

# Verbose options
CHECKFLAGS_VERBOSE = $(CHECKFLAGS_BASE) -v -G

# Quiet options
CHECKFLAGS = $(CHECKFLAGS_BASE) -q -q -q

# Number of files to create
CHECKCOUNT = 1000

# Size of files to create
CHECKSIZE = 4000

CONF = $(srcdir)/test/test-par6.conf
HOLE = $(srcdir)/test/test-par6-hole.conf
PAR1 = $(srcdir)/test/test-par1.conf
PAR2 = $(srcdir)/test/test-par2.conf
PAR3 = $(srcdir)/test/test-par3.conf
PAR4 = $(srcdir)/test/test-par4.conf
PAR5 = $(srcdir)/test/test-par5.conf
PAR6 = $(srcdir)/test/test-par6.conf

check-local:
	rm -rf bench
	rm -f test.log
	mkdir bench
	mkdir bench/disk1
	mkdir bench/disk2
	mkdir bench/disk3
	mkdir bench/disk4
	mkdir bench/disk5
	mkdir bench/disk6
	mkdir bench/pool
	echo --- Stream
	$(TESTENV) ./mkstream$(EXEEXT)
#### COMMAND LINE ####
	echo --- Help
	$(TESTENV) ./snapraid$(EXEEXT) --test-skip-device -H
	$(TESTENV) ./snapraid$(EXEEXT) --test-skip-device -V
	$(TESTENV) ./mktest$(EXEEXT)
if HAVE_EMULATOR
# Run the self test in AVX2
	$(TESTENV_AVX2) ./snapraid$(EXEEXT) --test-skip-device -c $(CONF) status
# Run the speed test in different architectures
	$(TESTENV_SSE2) ./snapraid$(EXEEXT) --test-skip-device -T
	$(TESTENV_SSSE3) ./snapraid$(EXEEXT) --test-skip-device -T
	$(TESTENV_SSE42) ./snapraid$(EXEEXT) --test-skip-device -T
	$(TESTENV_AVX2) ./snapraid$(EXEEXT) --test-skip-device -T
else
# Run the self test natively
	$(TESTENV) ./snapraid$(EXEEXT) --test-skip-device -c $(CONF) status
# Run the speed test natively
	$(TESTENV) ./snapraid$(EXEEXT) --test-skip-device -T
endif
#### EMPTY ####
	echo --- Some commands with empty array
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) diff
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) dup
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) list > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l ">&1"
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l ">&2"
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l ">>test.log"
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l test-%D-%T.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) up
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) down
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) devices
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) smart
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) smart --test-fake-device --test-expect-failure
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) pool
endif
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -a check
#### CONTROLLED ####
	echo -- Filesystem allocation test
	head -c 8192 /dev/zero > bench/disk1/TEST
# Copy the file to trigger the copy optimization check
	cp -a bench/disk1/TEST bench/disk2/TEST
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
# Now delete the file, and do a partial sync to force a deallocation in the middle of a file
	rm bench/disk1/TEST
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -E -S 1 -B 1 sync
# Now force a deallocation at the end
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -E -S 7 -B 1 sync
	rm bench/disk2/TEST
# Have the sync to fail for empty disk
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync --test-expect-failure
# Now sync will complete with -E
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -E sync
	echo -- Filesystem fragmentation test
	head -c 8192 /dev/zero > bench/disk1/TEST1
	head -c 8192 /dev/zero > bench/disk1/TEST2
	head -c 8192 /dev/zero > bench/disk1/TEST3
	head -c 8192 /dev/zero > bench/disk1/TEST4
	head -c 8192 /dev/zero > bench/disk1/TEST5
	head -c 8192 /dev/zero > bench/disk1/TEST6
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
# Now delete some files, and create a bigger one to fill the holes
	rm bench/disk1/TEST1
	rm bench/disk1/TEST3
	rm bench/disk1/TEST5
	head -c 65536 /dev/zero > bench/disk1/TESTX
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync -l ">&1"
	rm -r bench/disk1/TEST*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -E sync
#### NOT EMPTY ####
	echo --- Create some files
	$(TESTENV) ./mktest$(EXEEXT) generate 1 6 $(CHECKCOUNT) $(CHECKSIZE)
	echo DUP > bench/disk1/DUP1
	echo DUP > bench/disk1/DUP2
	echo --- Some commands with a not empty array
# Run a sync using physical offset
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_PHYSICAL) -c $(CONF) --test-expect-need-sync diff > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_PHYSICAL) -c $(CONF) sync
# Now reset the array, as we normally test with alpha order and murmur3
	rm bench/content bench/?-content
# Now rebuild the array with alpha order and murmur3 and do some commands
# Later we will convert it to spooky2 to test both hashes
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) --test-expect-need-sync  diff > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-murmur3 --test-force-autosave-at 100 sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) dup -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) list -l test.log > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) test-rewrite
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) test-read
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l test.log
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) pool
endif
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -a check
	echo --- Dry
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) test-dry
	echo --- Copy detection
# Create a file and sync with it
	echo 123 > bench/disk1/COPY
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
# Corrupt and move it to another disk
	$(TESTENV) ./mktest$(EXEEXT) damage 1 1 1 bench/disk1/COPY
	mv bench/disk1/COPY bench/disk2/COPY
# Now sync with failure as the data won't match. We have two points of failure, pre-hash and sync
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-expect-failure -h sync
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-expect-failure sync
# Now sync with force-nocopy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --force-nocopy sync
	echo --- Nano
	touch -t 200102011234.56 bench/disk1/a/a*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) test-nano
	echo --- Check the --gen-conf command
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --gen-conf bench/content
	echo --- Filter
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) -d disk1 check > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) -f \\*a -f a/ -f /b/a\\* check > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) --test-expect-failure -f . check > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) --test-expect-failure -f ./ check > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) --test-expect-failure -f a/a check > output.log
	echo --- Delete some files and sync
	rm bench/disk4/a/9*
	rm bench/disk5/a/9*
	rm bench/disk6/a/9*
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) --test-expect-need-sync diff > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) sync -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Move some files, sync and check
	mv bench/disk1/a/9* bench/disk4/a
	mv bench/disk2/a/9* bench/disk5/a
	mv bench/disk3/a/9* bench/disk6/a
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(CONF) --test-expect-need-sync diff > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
#### MORE FILES ####
	echo --- Create some more files, hardlinks and empty directories, delete others, sync PAR1 and check
	rm bench/disk4/a/8*
	rm bench/disk5/a/8*
	rm bench/disk6/a/8*
	$(TESTENV) ./mktest$(EXEEXT) generate 2 6 $(CHECKCOUNT) $(CHECKSIZE)
	mkdir bench/disk1/empty1
	mkdir bench/disk2/empty2
	mkdir bench/disk3/empty3a
	mkdir bench/disk3/empty3a/emptyinner
	mkdir bench/disk3/empty3b
	echo 1 > bench/disk1/file_hardlink1
	ln bench/disk1/file_hardlink1 bench/disk1/file_hardlink2
	ln bench/disk1/file_hardlink1 bench/disk1/file_hardlink3
	echo 22 > bench/disk2/file_hardlink1
	ln bench/disk2/file_hardlink1 bench/disk2/file_hardlink2
	ln bench/disk2/file_hardlink1 bench/disk2/file_hardlink3
	echo 333 > bench/disk3/file_hardlink1
	ln bench/disk3/file_hardlink1 bench/disk3/file_hardlink2
	ln bench/disk3/file_hardlink1 bench/disk3/file_hardlink3
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(PAR1) --test-expect-need-sync diff > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-fallocate -c $(PAR1) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-sequential -c $(PAR1) check
	echo --- Some commands with a not empty array
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(PAR1) dup
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) -c $(PAR1) list > output.log
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) pool
endif
	echo --- Extend PAR1 to max parity with fix and check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(CONF) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
#### SCRUB ####
	echo --- Scrub some times
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-scrub-at 100 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-scrub-at 1000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-scrub-at 100000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
#### SYNC WITH RUNTIME CHANGE ####
	echo --- Modify files during a sync
	echo RUN > bench/disk1/RUN-RM
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "rm bench/disk1/RUN-RM" --test-expect-failure sync
	echo RUN > bench/disk1/RUN-CHMOD
# Doesn't run this test as root because the root user overrride permissions
	if [[ $$EUID -ne 0 ]]; then \
		$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "chmod a-r bench/disk1/RUN-CHMOD" --test-expect-failure sync; \
	fi
	echo RUN > bench/disk1/RUN-SIZE
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "echo RUNRUN > bench/disk1/RUN-SIZE" --test-expect-failure sync
	echo RUN > bench/disk1/RUN-TOUCH
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "touch -t 200001011200 bench/disk1/RUN-TOUCH" --test-expect-failure sync
	echo RUN > bench/disk1/RUN-INODE
	cp -p bench/disk1/RUN-INODE bench/disk1/RUN-INODE-COPY
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "mv bench/disk1/RUN-INODE-COPY bench/disk1/RUN-INODE" --test-expect-failure sync
	rm bench/disk1/RUN-CHMOD
	rm bench/disk1/RUN-SIZE
	rm bench/disk1/RUN-TOUCH
	rm bench/disk1/RUN-INODE
	echo HASH > bench/disk1/HASH-RM
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "rm bench/disk1/HASH-RM" --test-expect-failure -h sync
	echo HASH > bench/disk1/HASH-CHMOD
# Doesn't run this test as root because the root user overrride permissions
	if [[ $$EUID -ne 0 ]]; then \
		$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "chmod a-r bench/disk1/HASH-CHMOD" --test-expect-failure -h sync; \
	fi
	echo HASH > bench/disk1/HASH-SIZE
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "echo HASHHASH > bench/disk1/HASH-SIZE" --test-expect-failure -h sync
	echo HASH > bench/disk1/HASH-TOUCH
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "touch -t 200001011200 bench/disk1/HASH-TOUCH" --test-expect-failure -h sync
	echo HASH > bench/disk1/HASH-INODE
	cp -p bench/disk1/HASH-INODE bench/disk1/HASH-INODE-COPY
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-run "mv bench/disk1/HASH-INODE-COPY bench/disk1/HASH-INODE" --test-expect-failure -h sync
	rm bench/disk1/HASH-CHMOD
	rm bench/disk1/HASH-SIZE
	rm bench/disk1/HASH-TOUCH
	rm bench/disk1/HASH-INODE
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
#### SILENT ERRORS ####
	echo --- Silently corrupt some files, scrub and fix filtering for error. Test scrub patterns.
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./mktest$(EXEEXT) damage 1 1 1 bench/disk1/a/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-expect-recoverable --test-force-scrub-at 100000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -e
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -p bad scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -p 1 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -o 0 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -p full scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -p sync scrub
	echo --- Silently corrupt some files, and sync with error presents
	$(TESTENV) ./mktest$(EXEEXT) write 2 1 1 bench/disk1/a/*
	$(TESTENV) ./mktest$(EXEEXT) damage 3 1 1 bench/disk2/a/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-expect-recoverable sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
# Note that this fix -e command doesn't fix all the errors because
# only the blocks touched by the sync are marked bad
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -e
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check --test-expect-recoverable
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -p full scrub
	echo --- Full sync forced
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -F sync
#### SYNC PARTIAL ####
	echo --- Abort sync with additions. Delete some of them, and add others and sync again.
	echo --- This triggers files reallocation inside parity
	mkdir bench/disk1/c
	mkdir bench/disk1/d
	mkdir bench/disk1/e
	cp -pR bench/disk2/a/c* bench/disk1/c
	cp -pR bench/disk2/a/d* bench/disk1/d
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-kill-after-sync sync
	rm -r bench/disk1/c
	cp -pR bench/disk2/a/e* bench/disk1/e
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync with both deletions and additions. And delete and add some more and sync again.
# Note that here we are depending of the previous state leaving dirs "d" and "e"
	mkdir bench/disk1/f
	cp -pR bench/disk3/a/f* bench/disk1/f
	rm -r bench/disk1/d
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-kill-after-sync sync
	mkdir bench/disk1/g
	cp -pR bench/disk4/a/g* bench/disk1/g
	rm -r bench/disk1/e
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	rm -r bench/disk1/f
	rm -r bench/disk1/g
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Make a hole in the disk array and sync with --force-empty
	mv bench/disk2 bench/disk2.old
	mkdir bench/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --force-empty sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Use with the hole
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) check
	rm -r bench/disk1/a
	rm -r bench/disk3/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) test-dry
	echo --- Fill the hole with a new disk
	rm -r bench/disk2
	mv bench/disk2.old bench/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the content file
	$(TESTENV) ./mktest$(EXEEXT) write 1 100 100 bench/content
	$(FAILENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-failure -c $(CONF) sync
	rm bench/content
#### AGAIN MORE FILES ####
	echo --- Delete some files, create some new, sync and check
	rm bench/disk1/a/1*
	rm bench/disk2/a/2*
	rm bench/disk3/a/3*
	rm bench/disk4/a/4*
	$(TESTENV) ./mktest$(EXEEXT) generate 3 6 $(CHECKCOUNT) $(CHECKSIZE)
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Move some files, sync and check
	mv bench/disk1/a/7* bench/disk1/b/
	mv bench/disk2/a/7* bench/disk2/b/
	mv bench/disk3/a/7* bench/disk3/b/
	mv bench/disk4/a/7* bench/disk4/b/
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
#### RECOVER 1 ####
	echo --- Delete one disk, fix and check with PAR1
	rm -r bench/disk3
	mkdir bench/disk3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
#### RECOVER 2 ####
	echo --- Delete two disks, fix and check with PAR2
	rm -r bench/disk1
	mkdir bench/disk1
	rm -r bench/disk2
	mkdir bench/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) fix -l test-fail-strategy1.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
#### RECOVER 3 ####
	echo --- Delete three disks, fix and check with PAR3
	rm -r bench/disk1
	mkdir bench/disk1
	rm -r bench/disk2
	mkdir bench/disk2
	rm -r bench/disk3
	mkdir bench/disk3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR2) fix -l test-fail-strategy2.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR3) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR3) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
#### RECOVER 4 ####
	echo --- Delete four disks, fix and check with PAR4
	rm -r bench/disk3
	mkdir bench/disk3
	rm -r bench/disk4
	mkdir bench/disk4
	rm -r bench/disk5
	mkdir bench/disk5
	rm -r bench/disk6
	mkdir bench/disk6
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR3) fix -l test-fail-strategy3.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR4) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR4) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
#### RECOVER 5 ####
	echo --- Delete five disks, fix and check with PAR5
	rm -r bench/disk1
	mkdir bench/disk1
	rm -r bench/disk2
	mkdir bench/disk2
	rm -r bench/disk3
	mkdir bench/disk3
	rm -r bench/disk5
	mkdir bench/disk5
	rm -r bench/disk6
	mkdir bench/disk6
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR4) fix -l test-fail-strategy4.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR5) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR5) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
#### RECOVER 6 ####
	echo --- Delete six disks, fix and check with PAR6
	rm -r bench/disk1
	mkdir bench/disk1
	rm -r bench/disk2
	mkdir bench/disk2
	rm -r bench/disk3
	mkdir bench/disk3
	rm -r bench/disk4
	mkdir bench/disk4
	rm -r bench/disk5
	mkdir bench/disk5
	rm -r bench/disk6
	mkdir bench/disk6
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR5) fix -l test-fail-strategy5.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR6) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR6) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
#### MULTI STEP ####
	echo --- Delete some files and create some new, sync and check in multiple steps
	rm bench/disk1/a/4*
	rm bench/disk2/a/5*
	rm bench/disk3/a/6*
	rm bench/disk4/a/6*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -B 10 sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check -l test.log
	$(TESTENV) ./mktest$(EXEEXT) generate 4 6 $(CHECKCOUNT) $(CHECKSIZE)
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -B 100 sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -B 1000 sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Rehash to spooky2 and rehash even blocks
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-spooky2 rehash
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-scrub-even scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete files from three disks and check/fix with import by data in PAR2
	rm -r bench/disk1/a
	rm -r bench/disk2/a
	mv bench/disk3/a bench/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-import-content bench/a -c $(PAR2) fix -l test.log
	rm -r bench/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Delete files from three disks and check/fix with import by timestamp in PAR2
	rm -r bench/disk1/a
	rm -r bench/disk2/a
	mv bench/disk3/a bench/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -i bench/a -c $(PAR2) fix -l test.log
	rm -r bench/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Delete files from three disks and check/fix with automatic import in PAR2
	rm -r bench/disk1/a
	rm -r bench/disk2/a
	mv bench/disk3/a bench/disk1/a_from_disk3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	rm -r bench/disk1/a_from_disk3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
#### SYNC ABORT ####
	echo --- Abort sync late with additions, delete them and recover with PAR2
	echo --- This triggers the recovering with q using p to check the validity
	cp -pR bench/disk1/a bench/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-kill-after-sync -c $(CONF) sync
	rm -r bench/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	rm -r bench/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync early with additions, delete them and recover with PAR2
	echo --- This triggers the recovering with q using p to check the validity, but failing for new block being filled with zero
	cp -pR bench/disk1/a bench/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -B 1 --test-kill-after-sync -c $(CONF) sync
	rm -r bench/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR2) fix -l test-fail-earlysyncadd.log
	rm -r bench/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with additions with prehash and then delete the new additions and sync again
	cp -pR bench/disk1/a bench/disk1/a_copy
	cp -pR bench/disk2/a bench/disk2/a_copy
	cp -pR bench/disk3/a bench/disk3/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-kill-after-sync -c $(CONF) -h sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	rm -r bench/disk1/a_copy
	rm -r bench/disk2/a_copy
	rm -r bench/disk3/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with additions and then delete the new additions and sync again
	cp -pR bench/disk1/a bench/disk1/a_copy
	cp -pR bench/disk2/a bench/disk2/a_copy
	cp -pR bench/disk3/a bench/disk3/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-kill-after-sync -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	rm -r bench/disk1/a_copy
	rm -r bench/disk2/a_copy
	rm -r bench/disk3/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with additions and then delete some unchanged stuff and fix with PAR1
	cp -pR bench/disk1/b bench/disk1/b_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) --test-kill-after-sync sync
	rm -r bench/disk2
	mkdir bench/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	echo --- Fixes again to restore all the parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -l test.log
	rm -r bench/disk1/b_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with more deletions than additions and then delete some unchanged stuff and fix with PAR1
	cp -pR bench/disk1/a bench/disk1/a_copy
	mv bench/disk1/a bench/a
	mv bench/disk1/b bench/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-kill-after-sync sync
	rm -r bench/disk2
	mkdir bench/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	echo --- Fixes again to restore all the parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -l test.log
	-rm -r bench/disk1/a
	-rm -r bench/disk1/b
	rm -r bench/disk1/a_copy
	mv bench/a bench/disk1/a
	mv bench/b bench/disk1/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Abort sync early with more deletions than additions and then delete some unchanged stuff and fix with PAR2
	cp -pR bench/disk1/a bench/disk1/a_copy
	mv bench/disk1/a bench/a
	mv bench/disk1/b bench/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -B 1 --test-kill-after-sync sync
	rm -r bench/disk2
	mkdir bench/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) fix -l test-fail-earlysyncdel.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	-rm -r bench/disk1/a
	-rm -r bench/disk1/b
	rm -r bench/disk1/a_copy
	mv bench/a bench/disk1/a
	mv bench/b bench/disk1/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
#### RECOVER MISSING ####
	echo --- Delete some files, fix with -m and check with PAR1
	rm bench/disk1/a/8*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) -m fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete some dirs in six disk, fix with -m and check
	rm -r bench/disk1/b
	rm -r bench/disk2/b
	rm -r bench/disk3/b
	rm -r bench/disk4/b
	rm -r bench/disk5/b
	rm -r bench/disk6/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR5) fix -l test-full5.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(CONF) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -m fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
#### RECOVER BY DISK ####
	echo --- Delete some dirs in two disks, fix and check with PAR2 using the -d option for each disk
	rm -r bench/disk2/b
	rm -r bench/disk5/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) check -l test-full1.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) -d disk2 fix -l test-part1.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) -d disk5 fix -l test-part2.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
#### RECOVER PARITY ####
	echo --- Delete the parity, fix and check with PAR1
	rm bench/parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete the parity and the 2-parity, fix and check with PAR2
	rm bench/parity
	rm bench/2-parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete the parity and a disk, fix and check with PAR2
	rm bench/parity
	rm -r bench/disk2/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete the 2-parity and a disk, fix and check with PAR2
	rm bench/2-parity
	rm -r bench/disk3/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the parity, fix and check with PAR1
	$(TESTENV) ./mktest$(EXEEXT) write 1 100 10000 bench/parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) -a check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the parity and the 2-parity, fix and check with PAR2
	$(TESTENV) ./mktest$(EXEEXT) write 2 100 10000 bench/parity
	$(TESTENV) ./mktest$(EXEEXT) write 2 100 10000 bench/2-parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -a check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
#### RECOVER FILES AND PARITY ####
	echo --- Corrupt some files, fix and check with PAR1
	$(TESTENV) ./mktest$(EXEEXT) change 3 500 bench/disk2/b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) -a check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt some files, fix and check with PAR2 in verbose mode
	$(TESTENV) ./mktest$(EXEEXT) change 4 500 bench/disk2/b/*
	$(TESTENV) ./mktest$(EXEEXT) change 4 500 bench/disk3/b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) --test-expect-unrecoverable -c $(PAR1) check -l test.log > /dev/null
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) --test-expect-unrecoverable -c $(PAR1) fix -l test-fail-corrupt-verbose.log > /dev/null
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) -a check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt some files, fix and check with PAR2
	$(TESTENV) ./mktest$(EXEEXT) change 5 500 bench/disk2/b/*
	$(TESTENV) ./mktest$(EXEEXT) change 5 500 bench/disk3/b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) check -l test-fail-corrupt-data.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the parity and some files, fix and check with PAR2
	$(TESTENV) ./mktest$(EXEEXT) write 6 100 10000 bench/parity
	$(TESTENV) ./mktest$(EXEEXT) change 6 500 bench/disk1/a/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) check -l test-fail-corrupt-parity.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the 2-parity and some files, fix and check with PAR2
	$(TESTENV) ./mktest$(EXEEXT) write 7 100 10000 bench/2-parity
	$(TESTENV) ./mktest$(EXEEXT) change 7 500 bench/disk1/a/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Sync after all the fixes
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo Success!

# Manual testing

MANUAL = ./snapraid --test-skip-device -c test/test-par1.conf sync

valgrind:
	valgrind \
		--tool=memcheck \
		--track-origins=yes \
		--read-var-info=yes \
		--leak-check=full \
		-v $(MANUAL) \
		2> valgrind.log
		tail valgrind.log

callgrind:
	valgrind \
		--tool=callgrind \
		--dump-instr=yes \
		--trace-jump=yes \
		--collect-systime=yes \
		-v $(MANUAL) \
		2> callgrind.log
		tail callgrind.log

cachegrind:
	valgrind \
		--tool=cachegrind \
		-v $(MANUAL) \
		2> cachegrind.log
		tail cachegrind.log

lcov_reset:
	lcov --directory . -z
	rm -f ./lcov.info

lcov_capture:
	lcov --directory . --capture -o lcov.info

lcov_html:
	rm -rf ./cov
	mkdir cov
	genhtml -o ./cov lcov.info

# Rules for documentation

if HAVE_ADVD2
snapraid.1 : snapraid.d
	advd2 man < $(srcdir)/$< > $@

snapraid.txt : snapraid.d
	advd2 txt < $(srcdir)/$< | utod > $@

snapraid.html : snapraid.d
	advd2 html < $(srcdir)/$< > $@

snapraid.hh : snapraid.d
	advd2 frame < $(srcdir)/$< > $@
endif

# Windows distribution

DISTWIN = \
	snapraid.txt \
	snapraid.exe

distwindows-x86: $(DISTWIN)
	rm -f $(PACKAGE)-$(VERSION)-windows-x86.zip
	mkdir tmp
	cp $(DISTWIN) tmp
	cp support/smartctl.exe tmp/smartctl.exe
	utod < snapraid.conf.example.windows > tmp/snapraid.conf.example
	utod < INSTALL.windows > tmp/install.txt
	utod < README > tmp/readme.txt
	utod < AUTHORS > tmp/authors.txt
	utod < HISTORY > tmp/history.txt
	utod < COPYING > tmp/copying.txt
	utod < TODO > tmp/todo.txt
	cd tmp && zip -r ../$(PACKAGE)-$(VERSION)-windows-x86.zip *
	rm -r tmp

distwindows-x64: $(DISTWIN)
	rm -f $(PACKAGE)-$(VERSION)-windows-x64.zip
	mkdir tmp
	cp $(DISTWIN) tmp
	cp support/smartctl.exe tmp/smartctl.exe
	utod < snapraid.conf.example.windows > tmp/snapraid.conf.example
	utod < INSTALL.windows > tmp/install.txt
	utod < README > tmp/readme.txt
	utod < AUTHORS > tmp/authors.txt
	utod < HISTORY > tmp/history.txt
	utod < COPYING > tmp/copying.txt
	utod < TODO > tmp/todo.txt
	cd tmp && zip -r ../$(PACKAGE)-$(VERSION)-windows-x64.zip *
	rm -r tmp

