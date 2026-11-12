# ─────────────────────────────────────────────────────────────────────────────
# OCL Interpreter — Makefile
#
# There is exactly one build: OCL links against its own C runtime in
# src/libc/ and against nothing else. No host libc, no host libm, no crt
# startup files, no system or compiler header directory.
#
# Targets: all, debug, release, freestanding, check-nodeps, check-libc,
#          test, test-freestanding, run, clean, help
# ─────────────────────────────────────────────────────────────────────────────

CC      := cc
TARGET  := ocl
VERSION := beta 0.6.0

# ── Directories ──────────────────────────────────────────────────────────────
SRC_DIR     := src
INC_DIR     := include
BUILD_DIR   := build
TEST_DIR    := Testfiles
STDLIB_DIR  := $(SRC_DIR)/stdlib
VM_DIR      := $(SRC_DIR)/vm
INTERP_DIR  := $(SRC_DIR)/interpreter
FRONT_DIR   := $(SRC_DIR)/frontend

# ── Sources ──────────────────────────────────────────────────────────────────
# Everything under src/, including the runtime in src/libc/.
SRCS := $(wildcard $(SRC_DIR)/*.c) \
        $(wildcard $(SRC_DIR)/*/*.c) \
        $(wildcard $(SRC_DIR)/*/*/*.c)

OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# ── Flags ────────────────────────────────────────────────────────────────────
# -nostdinc drops every system and compiler header directory. The only
# headers visible are include/ and include/oclrt/, and every runtime header
# is included by its in-tree path (<oclrt/stdio.h>), so no source line can
# resolve to a host header.
# -include ocl_prelude.h force-includes the in-tree runtime declarations into
# every translation unit, so the build cannot break if a file's own includes
# are edited.
# -nostdlib drops libc, libm and the crt startup files; src/libc/start.c
# provides _start and src/libc/ supplies every symbol.
CFLAGS_COMMON := \
    -std=c11 \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -fno-pie \
    -Wall \
    -Wextra \
    -Wno-unused-parameter \
    -nostdinc \
    -include ocl_prelude.h \
    -I$(INC_DIR)

CFLAGS_DEBUG   := $(CFLAGS_COMMON) -g3 -O0 -DDEBUG
CFLAGS_RELEASE := $(CFLAGS_COMMON) -O2 -DNDEBUG

LDFLAGS        := -nostdlib -static -no-pie -Wl,--build-id=none

CFLAGS := $(CFLAGS_DEBUG)

# ── Default target ───────────────────────────────────────────────────────────
.PHONY: all
all: CFLAGS := $(CFLAGS_DEBUG)
all: check-nodeps $(TARGET) check-no-external

# ── Release build ────────────────────────────────────────────────────────────
.PHONY: release
release: CFLAGS := $(CFLAGS_RELEASE)
release: clean $(TARGET)
	@echo "Release build complete: ./$(TARGET)"

# ── Debug build (same as all but explicit) ───────────────────────────────────
.PHONY: debug
debug: CFLAGS := $(CFLAGS_DEBUG)
debug: clean $(TARGET)
	@echo "Debug build complete: ./$(TARGET)"

# ── Freestanding build (alias: every build is freestanding) ──────────────────
.PHONY: freestanding
freestanding: all

# ── Link ─────────────────────────────────────────────────────────────────────
$(TARGET): $(OBJS)
	@echo "  LINK  $@ (nostdlib)"
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built $(TARGET) v$(VERSION) — freestanding, no host libc"

# ── Compile: pattern rule that handles any depth under src/ ──────────────────
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ── Guard: every header must resolve inside the repository ───────────────────
# Preprocesses each translation unit and fails if any header outside this
# repository is reachable.
.PHONY: check-nodeps
check-nodeps:
	@bad=$$(for f in $(SRCS); do \
	            $(CC) $(CFLAGS) -E "$$f" 2>/dev/null; \
	        done | grep -oE '^#[[:space:]]*[0-9]+[[:space:]]+"[^"]+"' \
	             | sed -E 's/^#[[:space:]]*[0-9]+[[:space:]]+"//; s/"$$//' \
	             | grep '^/' | grep -v "^$$(pwd)/" | sort -u || true); \
	if [ -n "$$bad" ]; then \
	    echo "  ERROR third-party headers reachable:"; echo "$$bad"; exit 1; \
	fi; \
	echo "  OK    every header resolves inside the repository"

# ── Guard: the linked binary must reference no external library ─────────────
# Fails if the executable has a dynamic section, a DT_NEEDED entry, an
# interpreter, or any undefined symbol.
.PHONY: check-no-external
check-no-external: $(TARGET)
	@fail=0; \
	if readelf -d $(TARGET) 2>/dev/null | grep -q NEEDED; then \
	    echo "  ERROR $(TARGET) links external libraries:"; \
	    readelf -d $(TARGET) | grep NEEDED; fail=1; \
	fi; \
	if readelf -S $(TARGET) 2>/dev/null | grep -qE '\.interp|\.dynamic'; then \
	    echo "  ERROR $(TARGET) has a dynamic section / interpreter"; fail=1; \
	fi; \
	if [ -n "$$(nm -u $(TARGET) 2>/dev/null)" ]; then \
	    echo "  ERROR $(TARGET) has undefined symbols:"; \
	    nm -u $(TARGET); fail=1; \
	fi; \
	if [ $$fail -ne 0 ]; then exit 1; fi; \
	echo "  OK    $(TARGET) references no external library"

# ── Runtime self-test (linked with -nostdlib) ────────────────────────────────
LIBC_OBJS := $(patsubst $(SRC_DIR)/libc/%.c, $(BUILD_DIR)/libc/%.o, \
             $(wildcard $(SRC_DIR)/libc/*.c))

.PHONY: check-libc
check-libc: $(LIBC_OBJS) $(BUILD_DIR)/tests/libc_selftest.o
	@echo "  LINK  $(BUILD_DIR)/libc_selftest (nostdlib)"
	@$(CC) $^ -o $(BUILD_DIR)/libc_selftest $(LDFLAGS)
	@./$(BUILD_DIR)/libc_selftest

$(BUILD_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ── Test (.ocl integration tests only) ───────────────────────────────────────
TEST_BIN ?= ./$(TARGET)

.PHONY: test
test: all
	@$(MAKE) --no-print-directory _run_tests TEST_BIN=./$(TARGET)

.PHONY: test-freestanding
test-freestanding: all
	@$(MAKE) --no-print-directory _run_tests TEST_BIN=./$(TARGET)

.PHONY: _run_tests
_run_tests:
	@echo "── Running tests with $(TEST_BIN) ──────────────────────"
	@pass=0; fail=0; \
	for f in $(TEST_DIR)/*.ocl $(TEST_DIR)/*/*.ocl; do \
	    [ -f "$$f" ] || continue; \
	    expected="$${f%.ocl}.expected"; \
	    exitcode_file="$${f%.ocl}.exitcode"; \
	    if [ -f "$$expected" ]; then \
	        actual=$$($(TEST_BIN) "$$f" 2>/dev/null); \
	        status=$$?; \
	        expected_output="$$(cat "$$expected")"; \
	        matched=1; \
	        if [ "$$actual" != "$$expected_output" ]; then \
	            matched=0; \
	        fi; \
	        if [ -f "$$exitcode_file" ]; then \
	            expected_exit=$$(cat "$$exitcode_file"); \
	            if [ "$$status" -ne "$$expected_exit" ]; then \
	                matched=0; \
	            fi; \
	        else \
	            case "$$f" in \
	                */errors/*) if [ "$$status" -eq 0 ]; then matched=0; fi ;; \
	                *)          if [ "$$status" -ne 0 ]; then matched=0; fi ;; \
	            esac; \
	        fi; \
	        if [ $$matched -eq 1 ]; then \
	            echo "  PASS  $$f"; pass=$$((pass+1)); \
	        else \
	            echo "  FAIL  $$f"; \
	            echo "    expected: $$expected_output"; \
	            echo "    actual:   $$actual"; \
	            fail=$$((fail+1)); \
	        fi; \
	    else \
	        $(TEST_BIN) "$$f" >/dev/null 2>&1 && echo "  PASS  $$f" && pass=$$((pass+1)) || (echo "  FAIL  $$f" && fail=$$((fail+1))); \
	    fi; \
	done; \
	echo "──────────────────────────────────────────────────────"; \
	echo "  Results: $$pass passed, $$fail failed"

# ── Run a single file ────────────────────────────────────────────────────────
.PHONY: run
run: all
	@test -n "$(FILE)" || (echo "usage: make run FILE=path/to/program.ocl"; exit 1)
	@./$(TARGET) $(FILE)

# ── Clean ────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR) $(TARGET) $(TARGET).exe ocl-freestanding ocl-freestanding.exe

# ── Help ─────────────────────────────────────────────────────────────────────
.PHONY: help
help:
	@echo "OCL build targets:"
	@echo "  make            Build ./ocl (freestanding, no host libc)"
	@echo "  make release    Optimised build"
	@echo "  make debug      Explicit debug build"
	@echo "  make freestanding  Alias for make (every build is freestanding)"
	@echo "  make check-nodeps  Verify every header resolves inside the repository"
	@echo "  make check-no-external  Verify the binary references no external library"
	@echo "  make check-libc    Run the from-scratch C runtime self-tests"
	@echo "  make test       Build and run all .ocl integration tests in $(TEST_DIR)/"
	@echo "  make run FILE=x Build and run a specific .ocl file"
	@echo "  make clean      Remove build artefacts"
	@echo "  make help       Show this message"
