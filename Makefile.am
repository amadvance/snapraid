bin_PROGRAMS = snapraid
check_PROGRAMS = testgen

snapraid_SOURCES = \
	snapraid.c \
	getopt.c \
	md5.c \
	tommy.c


noinst_HEADERS = \
	portable.h \
	md5.h \
	tommyarray.h \
	tommychain.h \
	tommyhash.h \
	tommyhashdyn.h \
	tommylist.h \
	tommytypes.h \
	util.h \
	tommyarray.c \
	tommyhash.c \
	tommyhashdyn.c \
	tommylist.c

testgen_SOURCES = \
	testgen.c

EXTRA_DIST = \
	autogen.sh \
	noautomake.sh \
	README AUTHORS HISTORY INSTALL COPYING \
	test.conf \
	acinclude.m4

clean-local:
	rm -f valgrind.log callgrind.log
	rm -rf test

CHECK = ./snapraid -v -c test.conf sync

check-local:
	rm -rf test
	mkdir test
	mkdir test/disk1
	mkdir test/disk2
	mkdir test/disk3
	./testgen 1 3 2000
	./snapraid -v -c $(srcdir)/test.conf sync
	./snapraid -v -c $(srcdir)/test.conf check
	./testgen 2 3 2000
	./snapraid -v -c $(srcdir)/test.conf sync
	./snapraid -v -c $(srcdir)/test.conf check
	rm test/disk1/a*
	./snapraid -v -c $(srcdir)/test.conf check
	echo Success!

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

