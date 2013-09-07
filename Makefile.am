bin_PROGRAMS = snapraid
check_PROGRAMS = mktest
EXTRA_PROGRAMS = mktables

snapraid_SOURCES = \
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
	pool.c \
	parity.c \
	handle.c \
	fnmatch.c \
	raid.c \
	selftest.c \
	speed.c \
	import.c \
	mingw.c \
	unix.c

mktables_SOURCES = \
	mktables.c

noinst_HEADERS = \
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
	tommyarrayof.h \
	tommychain.h \
	tommyhash.h \
	tommyhashdyn.h \
	tommylist.h \
	tommytypes.h \
	tommyarray.c \
	tommyarrayof.c \
	tommyhash.c \
	tommyhashdyn.c \
	tommylist.c \
	fnmatch.h \
	cpu.h \
	raid.h \
	tables.h \
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
	test-raid5.conf test-raid6.conf test-raid6-hole.conf \
	snapraid.conf.example \
	configure.windows-x86 configure.windows-x64 snapraid.conf.example.windows \
	acinclude.m4

man_MANS = snapraid.1

clean-local:
	rm -f valgrind.log callgrind.log cachegrind.log
	rm -rf test
	rm -f test.log
	rm -f *.gcda *.gcno *.gcov
	rm -rf cov
	rm -f lcov.info
	rm -f gmon.out

maintainer-clean-local:
	rm -f snapraid.1 snapraid.txt

# Automatic testing

CHECKFLAGS = --test-skip-device
CHECKCOUNT = 2000
CHECKSIZE = 4000

check-local:
	rm -rf test
	rm -f test.log
	mkdir test
	mkdir test/disk1
	mkdir test/disk2
	mkdir test/disk3
	mkdir test/pool
	echo --- Help
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -h
	echo --- As empty, sync and check forcing murmur3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -v -c $(srcdir)/test-raid6.conf diff
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -v -c $(srcdir)/test-raid6.conf dup
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) -c $(srcdir)/test-raid6.conf --test-force-murmur3 sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf pool
endif
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -a check
	echo --- Create some files, sync raid6 and check
	$(TESTENV) ./mktest$(EXEEXT) generate 1 3 $(CHECKCOUNT) $(CHECKSIZE)
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -v -c $(srcdir)/test-raid6.conf diff > test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -v -c $(srcdir)/test-raid6.conf dup
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-force-order-physical sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf pool
endif
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf -a check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -a check
	echo --- Create some more files, hardlinks and empty directories, sync raid5 and check
	$(TESTENV) ./mktest$(EXEEXT) generate 2 3 $(CHECKCOUNT) $(CHECKSIZE)
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
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -v -c $(srcdir)/test-raid5.conf diff > test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -v -c $(srcdir)/test-raid5.conf dup
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-skip-fallocate -c $(srcdir)/test-raid5.conf --test-force-order-inode sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-skip-sequential -c $(srcdir)/test-raid5.conf check
if HAVE_SYMLINK
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf pool
endif
	echo --- Extend raid5 to raid6 with fix and check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf check 2>> test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix 2>> test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Dry
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf dry
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf dry
	echo --- Scrub some times
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-force-scrub 100 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-force-scrub 1000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-force-scrub 100000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
	echo --- Corrupt one file, scrub and fix filtering for error
	rm -f test/disk1/content
	cp test/content test/disk1/content
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./mktest$(EXEEXT) damage 1 10 1 test/disk1/content
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-expect-recoverable --test-force-scrub 100000 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -e
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -p 0 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -o 0 scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -p 100 scrub
	rm test/disk1/content
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Make a hole in the disk array and sync with --force-empty
	mv test/disk2 test/disk2.old
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --force-empty sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Use with the hole
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6-hole.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6-hole.conf check
	rm -r test/disk1/_a
	rm -r test/disk3/_a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6-hole.conf fix 2>> test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6-hole.conf check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6-hole.conf dry
	echo --- Fill the hole with a new disk
	rm -r test/disk2
	mv test/disk2.old test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete some files, create some new, sync and check
	rm test/disk1/1*
	rm test/disk2/2*
	rm test/disk3/3*
	$(TESTENV) ./mktest$(EXEEXT) generate 3 3 $(CHECKCOUNT) $(CHECKSIZE)
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Move some files, sync and check
	mv test/disk1/7* test/disk1/_a/
	mv test/disk2/7* test/disk2/_a/
	mv test/disk3/7* test/disk3/_a/
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete one disk, fix and check with raid5
	rm -r test/disk3
	mkdir test/disk3
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid5.conf check 2>> test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf fix 2>> test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Sync after all the fixes with an empty drive to test --force-empty
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --force-empty -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete two disks, fix and check with raid6
	rm -r test/disk1
	mkdir test/disk1
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-unrecoverable -c $(srcdir)/test-raid5.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Delete some files and create some new, sync and check in multiple steps
	rm test/disk1/4*
	rm test/disk2/5*
	rm test/disk3/6*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -t 10 sync --test-force-content-text
	cp test/content test/content.txt
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf test-rewrite
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-force-content-text test-rewrite
	cmp test/content test/content.txt
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./mktest$(EXEEXT) generate 4 3 $(CHECKCOUNT) $(CHECKSIZE)
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -t 100 sync --test-force-content-text
	cp test/content test/content.txt
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf test-rewrite
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-force-content-text test-rewrite
	cmp test/content test/content.txt
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -t 1000 sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Rehash to spooky2 and rehash even blocks
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-force-spooky2 rehash
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-force-scrub-even scrub
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf status
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete files from three disks and fix with import in RAID6
	rm -r test/disk1/_a
	rm -r test/disk1/_b
	rm -r test/disk2/_a
	rm -r test/disk2/_b
	mv test/disk3/_a test/_a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-unrecoverable -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -i test/_a -c $(srcdir)/test-raid6.conf fix -l test.log
	rm -r test/_a
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Abort sync late with additions, delete them and recover with RAID6
	echo --- This triggers the recoving with q using p to check the validity
	cp -pR test/disk1/_a test/disk1/_c
	cp -pR test/disk1/_a test/disk1/_d
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-kill-after-sync -c $(srcdir)/test-raid6.conf sync
	rm -r test/disk1/_c
	rm -r test/disk1/_d
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	rm -r test/disk1/_c
	rm -r test/disk1/_d
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Abort sync early with additions, delete them and recover with RAID6
	echo --- This triggers the recoving with q using p to check the validity, but failing for new block being filled with zero
	cp -pR test/disk1/_a test/disk1/_c
	cp -pR test/disk1/_a test/disk1/_d
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -t 1 --test-kill-after-sync -c $(srcdir)/test-raid6.conf sync
	rm -r test/disk1/_c
	rm -r test/disk1/_d
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-unrecoverable -c $(srcdir)/test-raid6.conf fix -l test.log
	rm -r test/disk1/_c
	rm -r test/disk1/_d
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Abort sync late with additions and then delete the new additions and sync again
	cp -pR test/disk1/_a test/disk1/_c
	cp -pR test/disk2/_a test/disk2/_c
	cp -pR test/disk3/_a test/disk3/_c
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-kill-after-sync -c $(srcdir)/test-raid6.conf sync
	rm -r test/disk1/_c
	rm -r test/disk2/_c
	rm -r test/disk3/_c
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Abort sync late with additions and then delete some unchanged stuff and fix with RAID5 and then with RAID6
	cp -pR test/disk1/_b test/disk1/_c
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf --test-kill-after-sync sync
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	rm -r test/disk1/_c
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Abort sync late with more deletions than additions and then delete some unchanged stuff and fix with RAID5
	cp -pR test/disk1/_a test/disk1/_c
	mv test/disk1/_a test/_a
	mv test/disk1/_b test/_b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf --test-kill-after-sync sync
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf fix -l test.log
	-rm -r test/disk1/_a
	-rm -r test/disk1/_b
	rm -r test/disk1/_c
	mv test/_a test/disk1/_a
	mv test/_b test/disk1/_b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Abort sync early with more deletions than additions and then delete some unchanged stuff and fix with RAID6
	cp -pR test/disk1/_a test/disk1/_c
	mv test/disk1/_a test/_a
	mv test/disk1/_b test/_b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -t 1 --test-kill-after-sync sync
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-unrecoverable -c $(srcdir)/test-raid5.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	-rm -r test/disk1/_a
	-rm -r test/disk1/_b
	rm -r test/disk1/_c
	mv test/_a test/disk1/_a
	mv test/_b test/disk1/_b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	echo --- Delete some files, fix with -m and check with raid5
	rm test/disk1/8*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf -m fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete some dirs in a disk, fix with -m and check with raid5
	rm -r test/disk2/_a
	rm -r test/disk2/_b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf -m fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete some dirs in two disks, fix and check with raid6 using the -d option for each disk
	rm -r test/disk2/_a
	rm -r test/disk2/_b
	rm -r test/disk3/_a
	rm -r test/disk3/_b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-unrecoverable -c $(srcdir)/test-raid5.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -d disk2 fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -d disk3 fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete the parity, fix and check with raid5
	rm test/parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete the parity and the q-parity, fix and check with raid6
	rm test/parity
	rm test/q-parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete the parity and a disk, fix and check with raid6
	rm test/parity
	rm -r test/disk2/_a
	rm -r test/disk2/_b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Delete the q-parity and a disk, fix and check with raid6
	rm test/q-parity
	rm -r test/disk3/_a
	rm -r test/disk3/_b
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Corrupt the parity, fix and check with raid5
	$(TESTENV) ./mktest$(EXEEXT) damage 1 100 10000 test/parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf -a check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Corrupt the parity and the q-parity, fix and check with raid6
	$(TESTENV) ./mktest$(EXEEXT) damage 2 100 10000 test/parity
	$(TESTENV) ./mktest$(EXEEXT) damage 2 100 10000 test/q-parity
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf -a check
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Corrupt some files, fix and check with raid5
	$(TESTENV) ./mktest$(EXEEXT) damage 3 1 500 test/disk2/b*
	$(TESTENV) ./mktest$(EXEEXT) append 3 500 test/disk2/c*
	$(TESTENV) ./mktest$(EXEEXT) truncate 3 500 test/disk2/d*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid5.conf -a check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid5.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Corrupt some files, fix and check with raid6
	$(TESTENV) ./mktest$(EXEEXT) damage 4 1 500 test/disk2/_a/_a/*
	$(TESTENV) ./mktest$(EXEEXT) damage 4 1 500 test/disk3/_a/_a/*
	$(TESTENV) ./mktest$(EXEEXT) damage 4 1 500 test/disk2/_a/_b/*
	$(TESTENV) ./mktest$(EXEEXT) damage 4 1 500 test/disk3/_a/_b/*
	$(TESTENV) ./mktest$(EXEEXT) append 4 500 test/disk2/_b/_a/*
	$(TESTENV) ./mktest$(EXEEXT) append 4 500 test/disk3/_b/_a/*
	$(TESTENV) ./mktest$(EXEEXT) truncate 4 500 test/disk2/_b/_b/*
	$(TESTENV) ./mktest$(EXEEXT) truncate 4 500 test/disk3/_b/_b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid5.conf -a check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf -a check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-unrecoverable -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Another time with different values
	$(TESTENV) ./mktest$(EXEEXT) damage 5 2 50 test/disk2/_a/_a/*
	$(TESTENV) ./mktest$(EXEEXT) damage 5 2 50 test/disk3/_a/_a/*
	$(TESTENV) ./mktest$(EXEEXT) append 5 500 test/disk2/_a/_a/*
	$(TESTENV) ./mktest$(EXEEXT) truncate 5 500 test/disk3/_a/_a/*
	$(TESTENV) ./mktest$(EXEEXT) damage 5 2 50 test/disk2/_a/_b/*
	$(TESTENV) ./mktest$(EXEEXT) damage 5 2 50 test/disk3/_a/_b/*
	$(TESTENV) ./mktest$(EXEEXT) append 5 1500 test/disk2/_b/_a/*
	$(TESTENV) ./mktest$(EXEEXT) append 5 1500 test/disk3/_b/_a/*
	$(TESTENV) ./mktest$(EXEEXT) truncate 5 1500 test/disk2/_b/_b/*
	$(TESTENV) ./mktest$(EXEEXT) truncate 5 1500 test/disk3/_b/_b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-unrecoverable -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Corrupt the parity and some files, fix and check with raid6
	$(TESTENV) ./mktest$(EXEEXT) damage 6 100 10000 test/parity
	$(TESTENV) ./mktest$(EXEEXT) damage 6 1 500 test/disk1/_a/_a/*
	$(TESTENV) ./mktest$(EXEEXT) damage 6 1 500 test/disk1/_a/_b/*
	$(TESTENV) ./mktest$(EXEEXT) append 6 500 test/disk1/_b/_a/*
	$(TESTENV) ./mktest$(EXEEXT) truncate 6 500 test/disk1/_b/_b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-unrecoverable -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Corrupt the q-parity and some files, fix and check with raid6
	$(TESTENV) ./mktest$(EXEEXT) damage 7 100 10000 test/q-parity
	$(TESTENV) ./mktest$(EXEEXT) damage 7 1 500 test/disk1/_a/_a/*
	$(TESTENV) ./mktest$(EXEEXT) damage 7 1 500 test/disk1/_a/_b/*
	$(TESTENV) ./mktest$(EXEEXT) append 7 500 test/disk1/_b/_a/*
	$(TESTENV) ./mktest$(EXEEXT) truncate 7 500 test/disk1/_b/_b/*
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid5.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self --test-expect-recoverable -c $(srcdir)/test-raid6.conf check -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf fix -l test.log
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo --- Sync after all the fixes
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) $(CHECKFLAGS) --test-skip-self -c $(srcdir)/test-raid6.conf check
	echo Success!

# Manual testing

MANUAL = ./snapraid --test-skip-self --test-skip-device -c test-raid5.conf sync

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

