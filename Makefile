CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
ROOT := $(abspath .)
BIN := $(ROOT)/bin/linux-x86_64

# Linux/macOS host targets. On Windows the toolchain is driven by rbl.py /
# tools/rbltool.py instead (`python rbl.py doctor`, `python rbl.py run file.rbl`),
# which selects the native MinGW-w64 toolchain or WSL automatically.
.PHONY: all compiler runtime test bench bench3way fmt clean doctor ide
all: compiler runtime

# Bootstrap only: user RBL programs are assembled natively and never passed through C.
compiler: $(BIN)/rblc-asm
$(BIN)/rblc-asm: compiler/rblc_asm.c
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $< -o $@

runtime: $(BIN)/rbl_runtime.o $(BIN)/rbl_containers.o
$(BIN)/rbl_runtime.o: runtime/rbl_runtime.s
	mkdir -p $(BIN)
	as --64 $< -o $@

# Containers are shared C code, linked into both the ELF and the PE build.
$(BIN)/rbl_containers.o: runtime/rbl_containers.c
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) -c $< -o $@

test: compiler runtime
	python3 tests/run_tests.py
	python3 tests/test_stdlib.py

bench: compiler runtime
	python3 benchmarks/run_benchmarks.py

bench3way: compiler runtime
	python3 benchmarks/compare_all.py --reps=5 --warmups=1

fmt:
	python3 rbl.py fmt examples/mega_test.rbl

doctor:
	python3 rbl.py doctor
	python3 tools/ide_doctor.py

ide:
	./RBLStudio.sh

clean:
	rm -rf build $(BIN)/rblc-asm $(BIN)/rbl_runtime.o $(BIN)/rbl_containers.o
