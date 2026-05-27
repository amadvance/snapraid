#
# Test programs for the RAID library
#
# Primary Targets:
# all          - Builds all test and utility binaries (fulltest, speedtest, selftest, invtest, mktables).
# clean        - Removes all temporary object files, build artifacts, and coverage metadata.
# distclean    - Runs clean and deletes all compiled test executables.
#
# Test & Diagnostic Binaries:
# selftest     - Runs the same verification and speed tests executed at module initialization.
# speedtest    - Runs a comprehensive performance/throughput benchmark suite.
# fulltest     - Runs an exhaustive test suite verifying all built-in RAID functions.
# invtest      - Runs an extensive matrix inversion test on all 377,342,351,231
#                possible square submatrices of the Cauchy matrix used.
#
# Test Automation & Emulation:
# test         - Automated test runner that executes ./selftest, ./speedtest, and ./fulltest.
# covtest      - Executes a code coverage test using lcov/genhtml to generate HTML reports.
# sdecovtest   - Runs an extensive coverage test using the Intel SDE emulator across 
#                multiple CPU architectures (Prescott, Merom, Nehalem, Haswell, Skylake, Ice Lake).
# sde          - Runs selftest, speedtest, and fulltest explicitly on the Ice Lake simulator.
#
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
OBJS = raid.o check.o int.o intz.o x86.o x86z.o gfni.o tables.o memory.o test.o helper.o module.o

%.o: ../%.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: fulltest speedtest selftest invtest mktables tables.c

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

# Use this target to run a complete test
test:
	./selftest
	./speedtest
	./fulltest

# Use this target to run a coverage test using lcov
covtest:
	$(MAKE) clean
	$(MAKE) lcov_reset
	$(MAKE) COVERAGE=1 all
	./selftest
	./speedtest
	./fulltest
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
	$(SDE) -skx -- ./selftest
	$(SDE) -clx -- ./selftest # For slow AVX-512 detection
	$(SDE) -icl -- ./selftest
	$(SDE) -icl -- ./speedtest
	$(MAKE) lcov_capture
	$(MAKE) lcov_html

# Use this target to run a complete test on Ice Lake simulator
sde:
	$(SDE) -icl -- ./selftest
	$(SDE) -icl -- ./speedtest
	$(SDE) -icl -- ./fulltest

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

