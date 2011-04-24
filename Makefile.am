bin_PROGRAMS = snapraid
check_PROGRAMS = mktest

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
	parity.c \
	handle.c \
	fnmatch.c \
	raid.c \
	selftest.c \
	speed.c

noinst_HEADERS = \
	portable.h \
	snapraid.h \
	util.h \
	elem.h \
	state.h \
	parity.h \
	handle.h \
	md5.h \
	md5.c \
	murmurhash3.c \
	tommyarray.h \
	tommychain.h \
	tommyhash.h \
	tommyhashdyn.h \
	tommylist.h \
	tommytypes.h \
	tommyarray.c \
	tommyhash.c \
	tommyhashdyn.c \
	tommylist.c \
	fnmatch.h \
	cpu.h \
	raid.h \
	tables.h

mktest_SOURCES = \
	mktest.c

EXTRA_DIST = \
	autogen.sh \
	noautomake.sh \
	README AUTHORS HISTORY INSTALL COPYING TODO \
	snapraid.d snapraid.1 snapraid.txt \
	test-raid5.conf \
	test-raid6.conf \
	snapraid.conf.example \
	acinclude.m4

man_MANS = snapraid.1

clean-local:
	rm -f valgrind.log callgrind.log cachegrind.log
	rm -rf test

maintainer-clean-local:
	rm -f snapraid.1 snapraid.txt

# Automatic testing

check-local:
	rm -rf test
	mkdir test
	mkdir test/disk1
	mkdir test/disk2
	mkdir test/disk3
	echo --- As empty, sync and check
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Create some files, sync raid6 and check
	$(TESTENV) ./mktest$(EXEEXT) generate 1 3 1000 4000
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf check
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Create some more files, sync raid5 and check
	$(TESTENV) ./mktest$(EXEEXT) generate 2 3 1000 4000
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid5.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf check
	echo --- Extend raid5 to raid6 with fix and check
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid6.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf check
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Dry
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf dry
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf dry
	echo --- Delete some files, create some new, sync and check
	rm test/disk1/1*
	rm test/disk2/2*
	rm test/disk3/3*
	$(TESTENV) ./mktest$(EXEEXT) generate 3 3 100 4000
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Move some files, sync and check
	mv test/disk1/7* test/disk1/_a/
	mv test/disk2/7* test/disk2/_a/
	mv test/disk3/7* test/disk3/_a/
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete one disk, fix and check with raid5
	rm -r test/disk3
	mkdir test/disk3
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid5.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete two disks, fix and check with raid6
	rm -r test/disk1
	mkdir test/disk1
	rm -r test/disk2
	mkdir test/disk2
	$(TESTENV) ./snapraid$(EXEEXT) -U -c $(srcdir)/test-raid5.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid6.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Sync after all the fixes with an empty drive to test -E
	$(TESTENV) ./snapraid$(EXEEXT) -v -E -c $(srcdir)/test-raid6.conf sync
	echo --- Delete some files and create some new, sync and check in multiple steps
	rm test/disk1/4*
	rm test/disk2/5*
	rm test/disk3/6*
	$(TESTENV) ./mktest$(EXEEXT) generate 4 3 100 4000
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid6.conf -t 7 sync
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid6.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid6.conf -t 11 sync
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid6.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete some files, fix and check with raid5
	rm test/disk1/1*
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid5.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete some dirs, fix and check with raid5
	rm -r test/disk2/_a
	rm -r test/disk2/_b
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid5.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete some dirs, fix and check with raid6
	rm -r test/disk2/_a
	rm -r test/disk2/_b
	rm -r test/disk3/_a
	rm -r test/disk3/_b
	$(TESTENV) ./snapraid$(EXEEXT) -U -c $(srcdir)/test-raid5.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid6.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete the parity, fix and check with raid5
	rm test/parity
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete the parity and the q-parity, fix and check with raid6
	rm test/parity
	rm test/q-parity
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete the parity and a disk, fix and check with raid6
	rm test/parity
	rm -r test/disk2/_a
	rm -r test/disk2/_b
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Delete the q-parity and a disk, fix and check with raid6
	rm test/q-parity
	rm -r test/disk3/_a
	rm -r test/disk3/_b
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Corrupt the parity, fix and check with raid5
	$(TESTENV) ./mktest$(EXEEXT) damage 1 10 10000 test/parity
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid5.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Corrupt some files, fix and check with raid5
	$(TESTENV) ./mktest$(EXEEXT) damage 1 1 500 test/disk2/b*
	$(TESTENV) ./snapraid$(EXEEXT) -R -c $(srcdir)/test-raid5.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid5.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo --- Sync after all the fixes
	$(TESTENV) ./snapraid$(EXEEXT) -v -c $(srcdir)/test-raid6.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test-raid6.conf check
	echo Success!

# Manual testing

CHECK = ./snapraid -v -c test-raid5.conf sync

valgrind:
	valgrind \
		--tool=memcheck \
		--track-origins=yes \
		--read-var-info=yes \
		--leak-check=full \
		-v $(CHECK) \
		2> valgrind.log
		tail valgrind.log

callgrind:
	valgrind \
		--tool=callgrind \
		--dump-instr=yes \
		--trace-jump=yes \
		-v $(CHECK) \
		2> callgrind.log
		tail callgrind.log

cachegrind:
	valgrind \
		--tool=cachegrind \
		-v $(CHECK) \
		2> cachegrind.log
		tail cachegrind.log

# Rules for documentation

snapraid.1 : snapraid.d
	advd2 man < $(srcdir)/$< > $@

snapraid.txt : snapraid.d
	advd2 txt < $(srcdir)/$< | todos > $@

snapraid.html : snapraid.d
	advd2 html < $(srcdir)/$< > $@

snapraid.hh : snapraid.d
	advd2 frame < $(srcdir)/$< > $@

# Windows distribution

DISTWIN = \
	snapraid.txt \
	snapraid.exe

distwindows: $(DISTWIN)
	rm -f $(PACKAGE)-$(VERSION)-windows-x86.zip
	mkdir tmp
	cp $(DISTWIN) tmp
	cp snapraid.conf.example.windows tmp/snapraid.conf.example
	cp INSTALL.windows tmp/install.txt
	cp README tmp/readme.txt
	cp AUTHORS tmp/authors.txt
	cp HISTORY tmp/history.txt
	cp COPYING tmp/copying.txt
	cp TODO tmp/todo.txt
	cd tmp && zip -r ../$(PACKAGE)-$(VERSION)-windows-x86.zip *
	rm -r tmp

