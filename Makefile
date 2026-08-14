# Top-level build for flex-unicode (flex 2.6.4 with Unicode support).
#
#   make        configure and build flex in flex-2.6.4/ (src/flex, or flex.exe)
#   make test   build flex, then run the Unicode regression tests
#   make clean  remove build artifacts from flex-2.6.4/
#
# Requires: gcc, make, autoconf/automake (only to regenerate Makefile.in).

.PHONY: all test clean

FLEXDIR := flex-2.6.4

all: flex

flex:
	cd $(FLEXDIR) && ./configure && make

test: flex
	bash run_tests.sh

clean:
	cd $(FLEXDIR) && make clean
