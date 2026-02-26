# ─────────────────────────────────────────────────────────────────────────────
# OCL Interpreter — Makefile
# Targets:  all, debug, release, clean, test
# ─────────────────────────────────────────────────────────────────────────────

CC      := cc
TARGET  := ocl
VERSION := 0.2.0

# ── Directories ──────────────────────────────────────────────────────────────
SRC_DIR     := src
INC_DIR     := include
BUILD_DIR   := build
TEST_DIR    := test
STDLIB_DIR  := $(SRC_DIR)/stdlib
VM_DIR      := $(SRC_DIR)/vm
INTERP_DIR  := $(SRC_DIR)/interpreter
FRONT_DIR   := $(SRC_DIR)/frontend

# ── Sources ───────────────────────────────────────────────────────────────────
SRCS := \
    $(SRC_DIR)/common.c \
    $(INTERP_DIR)/*.c \
    $(VM_DIR)/*.c \
    $(STDLIB_DIR)/*.c \
    $(FRONT_DIR)/*.c

OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# ── Flags ─────────────────────────────────────────────────────────────────────
CFLAGS_COMMON := \
    -std=c11 \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Wno-unused-parameter \
    -I$(INC_DIR)

CFLAGS_DEBUG   := $(CFLAGS_COMMON) -g3 -O0 -DDEBUG -fsanitize=address,undefined
CFLAGS_RELEASE := $(CFLAGS_COMMON) -O2 -DNDEBUG

LDFLAGS        := -lm
LDFLAGS_DEBUG  := $(LDFLAGS) -fsanitize=address,undefined

# Default build type
CFLAGS ?= $(CFLAGS_DEBUG)
LFLAGS ?= $(LDFLAGS_DEBUG)

# ── Detect OS for any platform-specific tweaks ────────────────────────────────
UNAME := $(shell uname -s)
ifeq ($(UNAME), Darwin)
    # macOS: ASAN is available but lives in a different path on Apple Clang
    CFLAGS_DEBUG := $(filter-out -fsanitize=address,undefined, $(CFLAGS_DEBUG))
    CFLAGS_DEBUG += -fsanitize=address -fsanitize=undefined
endif

# ── Default target ────────────────────────────────────────────────────────────
.PHONY: all
all: CFLAGS := $(CFLAGS_DEBUG)
all: LFLAGS := $(LDFLAGS_DEBUG)
all: $(TARGET)

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

# ── Compile ───────────────────────────────────────────────────────────────────
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ── Clean ─────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR) $(TARGET)

# ── Test ──────────────────────────────────────────────────────────────────────
.PHONY: test
test: all
	@echo "── Running tests ──────────────────────────────────────"
	@pass=0; fail=0; \
	for f in $(TEST_DIR)/*.ocl; do \
	    [ -f "$$f" ] || continue; \
	    expected="$${f%.ocl}.expected"; \
	    if [ -f "$$expected" ]; then \
	        actual=$$(./$(TARGET) "$$f" 2>&1); \
	        if [ "$$actual" = "$$(cat $$expected)" ]; then \
	            echo "  PASS  $$f"; pass=$$((pass+1)); \
	        else \
	            echo "  FAIL  $$f"; \
	            echo "    expected: $$(cat $$expected)"; \
	            echo "    actual:   $$actual"; \
	            fail=$$((fail+1)); \
	        fi; \
	    else \
	        ./$(TARGET) "$$f" && echo "  RUN   $$f (no .expected)" && pass=$$((pass+1)) || fail=$$((fail+1)); \
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
	@echo "  make test       Build and run all tests in $(TEST_DIR)/"
	@echo "  make run FILE=x Build and run a specific .ocl file"
	@echo "  make help       Show this message"