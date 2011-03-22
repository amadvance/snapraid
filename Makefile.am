bin_PROGRAMS = snapraid
check_PROGRAMS = mktest

snapraid_SOURCES = \
	snapraid.c \
	md5.c \
	tommy.c \
	util.c \
	elem.c \
	state.c \
	scan.c \
	sync.c \
	check.c \
	parity.c \
	handle.c \
	fnmatch.c

noinst_HEADERS = \
	portable.h \
	util.h \
	elem.h \
	state.h \
	parity.h \
	handle.h \
	md5.h \
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
	fnmatch.h

mktest_SOURCES = \
	mktest.c

EXTRA_DIST = \
	autogen.sh \
	noautomake.sh \
	README AUTHORS HISTORY INSTALL COPYING TODO \
	snapraid.d snapraid.1 snapraid.txt \
	test.conf \
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
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Create some files, sync and check
	$(TESTENV) ./mktest$(EXEEXT) generate 1 3 1000 4000
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Create some more files, sync and check
	$(TESTENV) ./mktest$(EXEEXT) generate 2 3 1000 4000
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Delete some files and create some new, sync and check
	rm test/disk1/1*
	rm test/disk2/2*
	rm test/disk2/3*
	$(TESTENV) ./mktest$(EXEEXT) generate 3 3 100 4000
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf sync
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Delete some files, fix and check
	rm test/disk1/1*
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Delete some dirs, fix and check
	rm -r test/disk2/_a
	rm -r test/disk2/_b
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Delete a whole disk, fix and check
	rm -r test/disk3
	mkdir test/disk3
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Delete the parity, fix and check
	rm test/parity
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Corrupt the parity, fix and check
	$(TESTENV) ./mktest$(EXEEXT) damage 1 10 10000 test/parity
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo --- Corrupt some files, fix and check
	$(TESTENV) ./mktest$(EXEEXT) damage 1 1 500 test/disk2/b*
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf fix 2>/dev/null
	$(TESTENV) ./snapraid$(EXEEXT) -c $(srcdir)/test.conf check
	echo Success!

# Manual testing

CHECK = ./snapraid -v -c test.conf sync

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

%.1 : %.d
	advd2 man < $(srcdir)/$< > $@

%.txt : %.d
	advd2 txt < $(srcdir)/$< | todos > $@

%.html : %.d
	advd2 html < $(srcdir)/$< > $@

# Windows distribution

DISTWIN = \
	snapraid.txt \
	snapraid.exe \
	snapraid.conf.example

distwindows: $(DISTWIN)
	rm -f $(PACKAGE)-$(VERSION)-windows-x86.zip
	mkdir tmp
	cp $(DISTWIN) tmp
	cp README tmp/readme.txt
	cp AUTHORS tmp/authors.txt
	cp HISTORY tmp/history.txt
	cp COPYING tmp/copying.txt
	cp TODO tmp/todo.txt
	cd tmp && zip -r ../$(PACKAGE)-$(VERSION)-windows-x86.zip *
	rm -r tmp

