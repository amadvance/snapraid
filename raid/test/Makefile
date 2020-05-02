#
# Test programs for the RAID library
#
# selftest - Runs the same selftest and speedtest executed at the module startup.
# fulltest - Runs a more extensive test that checks all the built-in functions.
# speedtest - Runs a more complete speed test.
# invtest - Runs an extensive matrix inversion test of all the 377.342.351.231
#           possible square submatrices of the Cauchy matrix used.
# covtest - Runs a coverage test.
# sdecovtest - Runs a coverage test with the sde emulator.
#

MACHINE = $(shell uname -m)
ifeq ($(MACHINE),i686)
SDE = sde
else
SDE = sde64
endif
CC = gcc
LD = ld
CFLAGS = -I.. -Wall -Wextra -g
ifeq ($(COVERAGE),)
CFLAGS += -O2
else
CFLAGS += -O0 --coverage -DCOVERAGE=1 -DNDEBUG=1
endif
OBJS = raid.o check.o int.o intz.o x86.o x86z.o tables.o memory.o test.o helper.o module.o tag.o

%.o: ../%.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: fulltest speedtest selftest invtest

fulltest: $(OBJS) fulltest.o
	$(CC) $(CFLAGS) -o fulltest $^

speedtest: $(OBJS) speedtest.o
	$(CC) $(CFLAGS) -o speedtest $^

selftest: $(OBJS) selftest.o
	$(CC) $(CFLAGS) -o selftest $^

invtest: $(OBJS) invtest.o
	$(CC) $(CFLAGS) -o invtest $^

mktables: mktables.o
	$(CC) $(CFLAGS) -o mktables $^

tables.c: mktables
	./mktables > tables.c

# Use this target to run a coverage test using lcov
covtest:
	$(MAKE) clean
	$(MAKE) lcov_reset
	$(MAKE) COVERAGE=1 all
	./fulltest
	./selftest
	./speedtest
	$(MAKE) lcov_capture
	$(MAKE) lcov_html

# Use this target to run a coverage test using lcov and the sde
sdecovtest:
	$(MAKE) clean
	$(MAKE) lcov_reset
	$(MAKE) COVERAGE=1 all
	$(SDE) -p4p -- ./fulltest
	$(SDE) -mrm -- ./fulltest
	$(SDE) -nhm -- ./fulltest
	$(SDE) -hsw -- ./fulltest
	$(SDE) -p4p -- ./selftest
	$(SDE) -mrm -- ./selftest
	$(SDE) -nhm -- ./selftest
	$(SDE) -hsw -- ./selftest
	$(SDE) -hsw -- ./speedtest
	$(MAKE) lcov_capture
	$(MAKE) lcov_html

lcov_reset:
	lcov --directory . -z
	rm -f lcov.info

lcov_capture:
	lcov --directory . --capture --rc lcov_branch_coverage=1 -o lcov.info

lcov_html:
	rm -rf coverage
	mkdir coverage
	genhtml --branch-coverage -o coverage lcov.info

clean:
	rm -f *.o mktables tables.c
	rm -f *.gcda *.gcno lcov.info
	rm -rf coverage

distclean: clean
	rm -f fulltest speedtest selftest invtest

