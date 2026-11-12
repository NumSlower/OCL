# ─────────────────────────────────────────────────────────────────────────────
# OCL Interpreter — Makefile
# Targets:  all, debug, release, clean, test
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

# ── Sources (portable wildcard matching, no dependency on external find) ─────
SRCS := $(wildcard $(SRC_DIR)/*.c) \
        $(wildcard $(SRC_DIR)/*/*.c) \
        $(wildcard $(SRC_DIR)/*/*/*.c)

# ── Objects: mirror the src/ tree under build/ ───────────────────────────────
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# ── Flags ─────────────────────────────────────────────────────────────────────
CFLAGS_COMMON := \
    -std=c11 \
	-g \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Wno-unused-parameter \
	-D_POSIX_C_SOURCE=200809L \
    -I$(INC_DIR)

# ── Detect OS and sanitizer availability ─────────────────────────────────────
UNAME := $(shell uname -s 2>/dev/null || echo Windows)

# Check for sanitizer runtime support (often missing on Windows/MSYS/Cygwin)
SAN_FLAGS :=
ifeq ($(findstring CYGWIN,$(UNAME)),)
ifeq ($(findstring MSYS,$(UNAME)),)
ifeq ($(findstring MINGW,$(UNAME)),)
    HAS_SAN := $(shell $(CC) -fsanitize=address,undefined -x c /dev/null -o /dev/null 2>&1)
    ifeq ($(HAS_SAN),)
        SAN_FLAGS := -fsanitize=address,undefined
    endif
endif
endif
endif

CFLAGS_DEBUG   := $(CFLAGS_COMMON) -g3 -O0 -DDEBUG $(SAN_FLAGS)
CFLAGS_RELEASE := $(CFLAGS_COMMON) -O2 -DNDEBUG

LDFLAGS        := -lm
LDFLAGS_DEBUG  := $(LDFLAGS) $(SAN_FLAGS)
CFLAGS         := $(CFLAGS_DEBUG)
LFLAGS         := $(LDFLAGS_DEBUG)

# ── Default target ────────────────────────────────────────────────────────────
.PHONY: all
all: CFLAGS := $(CFLAGS_DEBUG)
all: LFLAGS := $(LDFLAGS_DEBUG)
all: $(TARGET)

# ── Valgrind build (debug with no optimisations) ─────────────────────────────
.PHONY: valgrind
valgrind: CFLAGS := $(CFLAGS_COMMON) -g -O0
valgrind: LFLAGS := $(LDFLAGS)
valgrind: clean $(TARGET)
	@echo "Valgrind build complete: ./$(TARGET)"

# ── Release build ─────────────────────────────────────────────────────────────
.PHONY: release
release: CFLAGS := $(CFLAGS_RELEASE)
release: LFLAGS := $(LDFLAGS)
release: clean $(TARGET)
	@echo "Release build complete: ./$(TARGET)"

# ── Debug build (same as all but explicit) ────────────────────────────────────
.PHONY: debug
debug: CFLAGS := $(CFLAGS_DEBUG)
debug: LFLAGS := $(LDFLAGS_DEBUG)
debug: clean $(TARGET)
	@echo "Debug build complete: ./$(TARGET)"

# ── Link ──────────────────────────────────────────────────────────────────────
$(TARGET): $(OBJS)
	@echo "  LINK  $@"
	@$(CC) $(OBJS) -o $@ $(LFLAGS)
	@echo "Built $(TARGET) v$(VERSION)"

# ── Compile: pattern rule that handles any depth under src/ ──────────────────
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ── Clean ─────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR) $(TARGET) $(TARGET).exe

# ── Test (.ocl integration tests only) ───────────────────────────────────────
.PHONY: test
test: all
	@echo "── Running tests ──────────────────────────────────────"
	@pass=0; fail=0; \
	for f in $(TEST_DIR)/*.ocl $(TEST_DIR)/*/*.ocl; do \
	    [ -f "$$f" ] || continue; \
	    expected="$${f%.ocl}.expected"; \
	    exitcode_file="$${f%.ocl}.exitcode"; \
	    if [ -f "$$expected" ]; then \
	        actual=$$(./$(TARGET) "$$f" 2>/dev/null); \
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
	        ./$(TARGET) "$$f" >/dev/null 2>&1 && echo "  PASS  $$f" && pass=$$((pass+1)) || (echo "  FAIL  $$f" && fail=$$((fail+1))); \
	    fi; \
	done; \
	echo "──────────────────────────────────────────────────────"; \
	echo "  Results: $$pass passed, $$fail failed"

# ── Run a specific file ───────────────────────────────────────────────────────
.PHONY: run
run: all
	@[ -n "$(FILE)" ] || (echo "Usage: make run FILE=path/to/file.ocl" && exit 1)
	@./$(TARGET) $(FILE)

# ── Print help ────────────────────────────────────────────────────────────────
.PHONY: help
help:
	@echo "OCL Interpreter v$(VERSION) — Makefile targets:"
	@echo "  make            Build with debug + sanitizers (default)"
	@echo "  make release    Build optimised release binary"
	@echo "  make debug      Explicit debug build"
	@echo "  make clean      Remove build artefacts"
	@echo "  make test       Build and run all .ocl integration tests in $(TEST_DIR)/"
	@echo "  make run FILE=x Build and run a specific .ocl file"
	@echo "  make help       Show this message"
