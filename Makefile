CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
COMPILER_CFLAGS = $(CFLAGS) -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Werror=missing-prototypes -Werror=redundant-decls -Werror=implicit-function-declaration
RUNTIME_CFLAGS = $(CFLAGS) -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Werror=missing-prototypes -Werror=redundant-decls -Werror=implicit-function-declaration
VERSION := $(shell cat VERSION)
CFLAGS += -DRBL_VERSION=\"$(VERSION)\"
ROOT := $(abspath .)
BIN := $(ROOT)/bin/linux-x86_64
COMPILER_BUILD := $(ROOT)/build/compiler
COMPILER_SOURCES := compiler/common.c compiler/lexer.c compiler/ast.c compiler/parser.c compiler/sema.c compiler/codegen.c compiler/driver.c
COMPILER_HEADERS := compiler/rblc_internal.h runtime/rbl_abi.h
COMPILER_OBJECTS := $(patsubst compiler/%.c,$(COMPILER_BUILD)/%.o,$(COMPILER_SOURCES))

# Linux host targets. On Windows the toolchain is driven by rbl.py /
# tools/rbltool.py instead (`python rbl.py doctor`, `python rbl.py run file.rbl`),
# which selects the native MinGW-w64 toolchain or WSL automatically.
.PHONY: all compiler runtime test analyze bench bench5way bench3way fmt clean doctor ide ide-test
all: compiler runtime

# Bootstrap only: user RBL programs are assembled natively and never passed through C.
compiler: $(BIN)/rblc-asm
# Use `make -jN compiler` for a parallel clean build; per-module objects also
# keep single-file edits from recompiling unrelated translation units.
$(BIN)/rblc-asm: $(COMPILER_OBJECTS) VERSION
	mkdir -p $(BIN)
	$(CC) $(COMPILER_CFLAGS) $(COMPILER_OBJECTS) -o $@

$(COMPILER_BUILD)/%.o: compiler/%.c $(COMPILER_HEADERS) | $(COMPILER_BUILD)
	$(CC) $(COMPILER_CFLAGS) -c $< -o $@

# The version macro is consumed by driver.c; a VERSION edit must rebuild it.
$(COMPILER_BUILD)/driver.o: VERSION

$(COMPILER_BUILD):
	mkdir -p $@

runtime: $(BIN)/rbl_runtime.o $(BIN)/rbl_containers.o $(BIN)/rbl_abi.o $(BIN)/rbl_format.o
$(BIN)/rbl_runtime.o: runtime/rbl_runtime.s
	mkdir -p $(BIN)
	as --64 $< -o $@

# Containers are shared C code, linked into both the ELF and the PE build.
$(BIN)/rbl_containers.o: runtime/rbl_containers.c runtime/rbl_abi.h
	mkdir -p $(BIN)
	$(CC) $(RUNTIME_CFLAGS) -c $< -o $@

$(BIN)/rbl_abi.o: runtime/rbl_abi.c runtime/rbl_abi.h
	mkdir -p $(BIN)
	$(CC) $(RUNTIME_CFLAGS) -c $< -o $@

$(BIN)/rbl_format.o: runtime/rbl_format.c runtime/rbl_abi.h
	mkdir -p $(BIN)
	$(CC) $(RUNTIME_CFLAGS) -c $< -o $@

test: compiler runtime
	python3 tests/run_tests.py
	python3 tests/test_stdlib.py
	python3 tests/test_build_cache.py
	python3 tests/test_asm_call_alignment.py
	python3 tests/test_runtime_abi.py
	python3 tests/test_string_ownership.py
	python3 tests/test_string_io_gc.py
	python3 tests/test_toolchain_manifest.py
	python3 tests/test_diagnostics.py

analyze:
	bash tests/run_gcc_analyzer.sh

bench: compiler runtime
	python3 benchmarks/run_benchmarks.py

bench5way: compiler runtime
	python3 benchmarks/compare_all.py --reps=5 --warmups=1

# Backwards-compatible alias; the comparison now contains five implementations.
bench3way: bench5way

fmt:
	python3 rbl.py fmt examples/mega_test.rbl

doctor:
	python3 rbl.py doctor

ide-test:
	cargo test
	cargo run --release -- --self-test

ide:
	./RBLStudio.sh

clean:
	rm -rf build $(BIN)/rblc-asm $(BIN)/rbl_runtime.o $(BIN)/rbl_containers.o $(BIN)/rbl_abi.o $(BIN)/rbl_format.o
