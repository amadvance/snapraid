bin_PROGRAMS = snapraid
check_PROGRAMS = mktest

snapraid_SOURCES = \
	raid/raid.c \
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
	snapraid.c \
	tommy.c \
	util.c \
	elem.c \
	state.c \
	scan.c \
	sync.c \
	check.c \
	dry.c \
	rehash.c \
	scrub.c \
	status.c \
	dup.c \
	list.c \
	pool.c \
	parity.c \
	handle.c \
	fnmatch.c \
	selftest.c \
	speed.c \
	import.c \
	mingw.c \
	unix.c

noinst_HEADERS = \
	raid/raid.h \
	raid/helper.h \
	raid/internal.h \
	raid/cpu.h \
	raid/gf.h \
	raid/combo.h \
	raid/memory.h \
	raid/test.h \
	portable.h \
	snapraid.h \
	util.h \
	elem.h \
	state.h \
	parity.h \
	handle.h \
	murmur3.c \
	murmur3test.c \
	spooky2.c \
	spooky2test.c \
	tommyarray.h \
	tommyarrayblk.h \
	tommyarrayblkof.h \
	tommychain.h \
	tommyhash.h \
	tommyhashdyn.h \
	tommylist.h \
	tommytypes.h \
	tommyarray.c \
	tommyarrayblk.c \
	tommyarrayblkof.c \
	tommyhash.c \
	tommyhashdyn.c \
	tommylist.c \
	fnmatch.h \
	import.h \
	mingw.h \
	unix.h

mktest_SOURCES = \
	mktest.c \
	mingw.c

EXTRA_DIST = \
	autogen.sh \
	noautomake.sh \
	README AUTHORS HISTORY INSTALL COPYING TODO TEST INSTALL.windows \
	snapraid.d snapraid.1 snapraid.txt \
	test-par1.conf test-par2.conf test-par3.conf test-par4.conf test-par5.conf test-par6.conf test-par6-hole.conf \
	snapraid.conf.example \
	configure.windows-x86 configure.windows-x64 snapraid.conf.example.windows \
	acinclude.m4 \
	raid/COPYING \
	raid/test/Makefile \
	raid/test/fulltest.c \
	raid/test/selftest.c \
	raid/test/speedtest.c \
	raid/test/invtest.c

man_MANS = snapraid.1

clean-local:
	rm -f valgrind.log callgrind.log cachegrind.log
	rm -rf test
	rm -f test*.log output.log
	rm -f *.gcda *.gcno *.gcov
	rm -rf cov
	rm -f lcov.info
	rm -f gmon.out

maintainer-clean-local:
	rm -f snapraid.1 snapraid.txt

# Automatic testing

CHECKFLAGS_BASE = --test-skip-device --test-skip-self --test-force-order-alpha
CHECKFLAGS_VERBOSE = $(CHECKFLAGS_BASE) -v
CHECKFLAGS = $(CHECKFLAGS_BASE) -q
CHECKCOUNT = 1000
CHECKSIZE = 4000
CONF = $(srcdir)/test-par6.conf
HOLE = $(srcdir)/test-par6-hole.conf
PAR1 = $(srcdir)/test-par1.conf
PAR2 = $(srcdir)/test-par2.conf
PAR3 = $(srcdir)/test-par3.conf
PAR4 = $(srcdir)/test-par4.conf
PAR5 = $(srcdir)/test-par5.conf
PAR6 = $(srcdir)/test-par6.conf

check-local:
	rm -rf test
	rm -f test.log
	mkdir test
	mkdir test/disk1
	mkdir test/disk2
	mkdir test/disk3
	mkdir test/disk4
	mkdir test/disk5
	mkdir test/disk6
	mkdir test/pool
	echo --- Help
	$(TESTENV) ./snapraid$(EXEEXT) --test-skip-device -H
	$(TESTENV) ./mktest$(EXEEXT)
	echo --- Run the self test only this time
	$(TESTENV) ./snapraid$(EXEEXT) --test-skip-device -c $(CONF) status
	echo --- Run the speed test
	$(TESTENV) ./snapraid$(EXEEXT) --test-skip-device -T
	echo --- Some commands with empty array
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(CONF) diff
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(CONF) dup
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(CONF) list > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-murmur3 sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l ">&1"
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l ">&2"
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l ">>test.log"
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l test-%D-%T.log
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) pool
endif
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -a check
	echo --- Create some files, sync and check
	$(TESTENV) ./mktest$(EXEEXT) generate 1 6 $(CHECKCOUNT) $(CHECKSIZE)
	echo DUP > test/disk1/DUP1
	echo DUP > test/disk1/DUP2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(CONF) diff > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(CONF) diff -G > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-order-physical sync
	echo --- Some commands with a not empty array
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(CONF) dup -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(CONF) list -l test.log > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status -l test.log
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) pool
endif
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -a check
	echo --- Check the --gen-conf command
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --gen-conf test/content
	echo --- Delete some files and sync
	rm test/disk4/a/9*
	rm test/disk5/a/9*
	rm test/disk6/a/9*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Create some more files, hardlinks and empty directories, delete others, sync PAR1 and check
	rm test/disk4/a/8*
	rm test/disk5/a/8*
	rm test/disk6/a/8*
	$(TESTENV) ./mktest$(EXEEXT) generate 2 6 $(CHECKCOUNT) $(CHECKSIZE)
	mkdir test/disk1/empty1
	mkdir test/disk2/empty2
	mkdir test/disk3/empty3a
	mkdir test/disk3/empty3a/emptyinner
	mkdir test/disk3/empty3b
	echo 1 > test/disk1/file_hardlink1
	ln test/disk1/file_hardlink1 test/disk1/file_hardlink2
	ln test/disk1/file_hardlink1 test/disk1/file_hardlink3
	echo 2 > test/disk2/file_hardlink1
	ln test/disk2/file_hardlink1 test/disk2/file_hardlink2
	ln test/disk2/file_hardlink1 test/disk2/file_hardlink3
	echo 3 > test/disk3/file_hardlink1
	ln test/disk3/file_hardlink1 test/disk3/file_hardlink2
	ln test/disk3/file_hardlink1 test/disk3/file_hardlink3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(PAR1) diff > output.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-fallocate -c $(PAR1) --test-force-order-inode sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-sequential -c $(PAR1) check
	echo --- Some commands with a not empty array
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(PAR1) dup
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -v -c $(PAR1) list > output.log
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) pool
endif
	echo --- Extend PAR1 to max parity with fix and check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(CONF) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Dry
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) dry
	echo --- Scrub some times
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-scrub 100 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-scrub 1000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-scrub 100000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	echo --- Silently corrupt some files, scrub and fix filtering for error
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./mktest$(EXEEXT) damage 1 10 1 test/disk1/a/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-expect-recoverable --test-force-scrub 100000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -e
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -p 0 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -o 0 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -p 100 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Silently corrupt some files, and sync with error presents
	$(TESTENV) ./mktest$(EXEEXT) change 1 500 test/disk1/a/*
	$(TESTENV) ./mktest$(EXEEXT) damage 1 1 500 test/disk2/a/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-expect-recoverable  sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -e
	echo --- Make a hole in the disk array and sync with --force-empty
	mv test/disk2 test/disk2.old
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --force-empty sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Use with the hole
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) check
	rm -r test/disk1/a
	rm -r test/disk3/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(HOLE) dry
	echo --- Fill the hole with a new disk
	rm -r test/disk2
	mv test/disk2.old test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete some files, create some new, sync and check
	rm test/disk1/a/1*
	rm test/disk2/a/2*
	rm test/disk3/a/3*
	rm test/disk4/a/4*
	$(TESTENV) ./mktest$(EXEEXT) generate 3 6 $(CHECKCOUNT) $(CHECKSIZE)
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Move some files, sync and check
	mv test/disk1/a/7* test/disk1/b/
	mv test/disk2/a/7* test/disk2/b/
	mv test/disk3/a/7* test/disk3/b/
	mv test/disk4/a/7* test/disk4/b/
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete one disk, fix and check with PAR1
	rm -r test/disk3
	mkdir test/disk3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Delete two disks, fix and check with PAR2
	rm -r test/disk1
	mkdir test/disk1
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Delete three disks, fix and check with PAR3
	rm -r test/disk1
	mkdir test/disk1
	rm -r test/disk2
	mkdir test/disk2
	rm -r test/disk3
	mkdir test/disk3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR3) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR3) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Delete four disks, fix and check with PAR4
	rm -r test/disk3
	mkdir test/disk3
	rm -r test/disk4
	mkdir test/disk4
	rm -r test/disk5
	mkdir test/disk5
	rm -r test/disk6
	mkdir test/disk6
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR3) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR4) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR4) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Delete five disks, fix and check with PAR5
	rm -r test/disk1
	mkdir test/disk1
	rm -r test/disk2
	mkdir test/disk2
	rm -r test/disk3
	mkdir test/disk3
	rm -r test/disk5
	mkdir test/disk5
	rm -r test/disk6
	mkdir test/disk6
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR4) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR5) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR5) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Delete six disks, fix and check with PAR6
	rm -r test/disk1
	mkdir test/disk1
	rm -r test/disk2
	mkdir test/disk2
	rm -r test/disk3
	mkdir test/disk3
	rm -r test/disk4
	mkdir test/disk4
	rm -r test/disk5
	mkdir test/disk5
	rm -r test/disk6
	mkdir test/disk6
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR5) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR6) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR6) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Delete some files and create some new, sync and check in multiple steps using text content
	rm test/disk1/a/4*
	rm test/disk2/a/5*
	rm test/disk3/a/6*
	rm test/disk4/a/6*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -t 10 sync --test-force-content-text
	cp test/content test/content.txt
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) test-rewrite
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-content-text test-rewrite
	cmp test/content test/content.txt
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check -l test.log
	$(TESTENV) ./mktest$(EXEEXT) generate 4 6 $(CHECKCOUNT) $(CHECKSIZE)
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -t 100 sync --test-force-content-text
	cp test/content test/content.txt
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) test-rewrite
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-content-text test-rewrite
	cmp test/content test/content.txt
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -t 1000 sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Rehash to spooky2 and rehash even blocks
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-spooky2 rehash
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-force-scrub-even scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete files from three disks and check/fix with import in PAR2
	rm -r test/disk1/a
	rm -r test/disk2/a
	mv test/disk3/a test/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -i test/a -c $(PAR2) fix -l test.log
	rm -r test/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with additions, delete them and recover with PAR2
	echo --- This triggers the recovering with q using p to check the validity
	cp -pR test/disk1/a test/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-kill-after-sync -c $(CONF) sync
	rm -r test/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	rm -r test/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync early with additions, delete them and recover with PAR2
	echo --- This triggers the recovering with q using p to check the validity, but failing for new block being filled with zero
	cp -pR test/disk1/a test/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -t 1 --test-kill-after-sync -c $(CONF) sync
	rm -r test/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR2) fix -l test.log
	rm -r test/disk1/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with additions with prehash and then delete the new additions and sync again
	cp -pR test/disk1/a test/disk1/a_copy
	cp -pR test/disk2/a test/disk2/a_copy
	cp -pR test/disk3/a test/disk3/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-kill-after-sync -c $(CONF) -h sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	rm -r test/disk1/a_copy
	rm -r test/disk2/a_copy
	rm -r test/disk3/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with additions and then delete the new additions and sync again
	cp -pR test/disk1/a test/disk1/a_copy
	cp -pR test/disk2/a test/disk2/a_copy
	cp -pR test/disk3/a test/disk3/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-kill-after-sync -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	rm -r test/disk1/a_copy
	rm -r test/disk2/a_copy
	rm -r test/disk3/a_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with additions and then delete some unchanged stuff and fix with PAR1
	cp -pR test/disk1/b test/disk1/b_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) --test-kill-after-sync sync
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	echo --- Fixes again to restore all the parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -l test.log
	rm -r test/disk1/b_copy
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	echo --- Abort sync late with more deletions than additions and then delete some unchanged stuff and fix with PAR1
	cp -pR test/disk1/a test/disk1/a_copy
	mv test/disk1/a test/a
	mv test/disk1/b test/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) --test-kill-after-sync sync
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	echo --- Fixes again to restore all the parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) fix -l test.log
	-rm -r test/disk1/a
	-rm -r test/disk1/b
	rm -r test/disk1/a_copy
	mv test/a test/disk1/a
	mv test/b test/disk1/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Abort sync early with more deletions than additions and then delete some unchanged stuff and fix with PAR2
	cp -pR test/disk1/a test/disk1/a_copy
	mv test/disk1/a test/a
	mv test/disk1/b test/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -t 1 --test-kill-after-sync sync
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	-rm -r test/disk1/a
	-rm -r test/disk1/b
	rm -r test/disk1/a_copy
	mv test/a test/disk1/a
	mv test/b test/disk1/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete some files, fix with -m and check with PAR1
	rm test/disk1/a/8*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) -m fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete some dirs in six disk, fix with -m and check
	rm -r test/disk1/b
	rm -r test/disk2/b
	rm -r test/disk3/b
	rm -r test/disk4/b
	rm -r test/disk5/b
	rm -r test/disk6/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR5) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(CONF) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -m fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete some dirs in two disks, fix and check with PAR2 using the -d option for each disk
	rm -r test/disk2/b
	rm -r test/disk5/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) -d disk2 fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) -d disk5 fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete the parity, fix and check with PAR1
	rm test/parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete the parity and the 2-parity, fix and check with PAR2
	rm test/parity
	rm test/2-parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete the parity and a disk, fix and check with PAR2
	rm test/parity
	rm -r test/disk2/b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Delete the 2-parity and a disk, fix and check with PAR2
	rm test/2-parity
	rm -r test/disk3/a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the parity, fix and check with PAR1
	$(TESTENV) ./mktest$(EXEEXT) write 1 100 10000 test/parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) -a check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the parity and the 2-parity, fix and check with PAR2
	$(TESTENV) ./mktest$(EXEEXT) write 2 100 10000 test/parity
	$(TESTENV) ./mktest$(EXEEXT) write 2 100 10000 test/2-parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) -a check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt some files, fix and check with PAR1
	$(TESTENV) ./mktest$(EXEEXT) change 3 500 test/disk2/b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) -a check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR1) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt some files, fix and check with PAR2 in verbose mode
	$(TESTENV) ./mktest$(EXEEXT) change 4 500 test/disk2/b/*
	$(TESTENV) ./mktest$(EXEEXT) change 4 500 test/disk3/b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) --test-expect-unrecoverable -c $(PAR1) check -l test.log > /dev/null
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS_VERBOSE) --test-expect-unrecoverable -c $(PAR1) fix -l test.log > /dev/null
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) -a check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt some files, fix and check with PAR2
	$(TESTENV) ./mktest$(EXEEXT) change 5 500 test/disk2/b/*
	$(TESTENV) ./mktest$(EXEEXT) change 5 500 test/disk3/b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the parity and some files, fix and check with PAR2
	$(TESTENV) ./mktest$(EXEEXT) write 6 100 10000 test/parity
	$(TESTENV) ./mktest$(EXEEXT) change 6 500 test/disk1/a/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-unrecoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Corrupt the 2-parity and some files, fix and check with PAR2
	$(TESTENV) ./mktest$(EXEEXT) write 7 100 10000 test/2-parity
	$(TESTENV) ./mktest$(EXEEXT) change 7 500 test/disk1/a/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR1) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-expect-recoverable -c $(PAR2) check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(PAR2) fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo --- Sync after all the fixes
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(CONF) check
	echo Success!

# Manual testing

MANUAL = ./snapraid --test-skip-device -c test-par1.conf sync

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
	utod < snapraid.conf.example.windows > tmp/snapraid.conf.example
	utod < INSTALL.windows > tmp/install.txt
	utod < README > tmp/readme.txt
	utod < AUTHORS > tmp/authors.txt
	utod < HISTORY > tmp/history.txt
	utod < COPYING > tmp/copying.txt
	utod < TODO > tmp/todo.txt
	cd tmp && zip -r ../$(PACKAGE)-$(VERSION)-windows-x64.zip *
	rm -r tmp

