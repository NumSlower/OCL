# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# OCL Interpreter â€” Makefile
# Targets:  all, debug, release, clean, test
# Supports: Linux, macOS, Windows (MSYS2/MinGW)
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

# â”€â”€ OS detection â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
ifeq ($(OS),Windows_NT)
  DETECTED_OS := WINDOWS
else
  DETECTED_OS := UNIX
endif

ifeq ($(DETECTED_OS),UNIX)
  UNAME_OUT := $(shell uname -s 2>/dev/null)
  ifeq ($(UNAME_OUT),Darwin)
    DETECTED_OS := MACOS
  else
    DETECTED_OS := LINUX
  endif
endif

# â”€â”€ Toolchain â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
CC           ?= gcc
VERSION      := beta 1.7.0
ifeq ($(DETECTED_OS),WINDOWS)
NUMOS_CC     ?= gcc
NUMOS_LD     ?= ld
NUMOS_AS     ?= nasm
else
NUMOS_TARGET ?= x86_64-elf
NUMOS_CC     ?= $(or $(shell command -v $(NUMOS_TARGET)-gcc 2>/dev/null),$(shell command -v gcc 2>/dev/null),$(shell command -v clang 2>/dev/null))
NUMOS_LD     ?= $(or $(shell command -v $(NUMOS_TARGET)-ld 2>/dev/null),$(shell command -v ld 2>/dev/null),$(shell command -v ld.lld 2>/dev/null))
NUMOS_AS     ?= $(or $(shell command -v nasm 2>/dev/null),$(shell command -v yasm 2>/dev/null))
endif

# â”€â”€ Directories â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
SRC_DIR   := src
INC_DIR   := include
BUILD_DIR := build
TARGET    := $(BUILD_DIR)/ocl$(if $(filter WINDOWS,$(DETECTED_OS)),.exe,)
TEST_DIR  := Testfiles/ReferenceSuite

# â”€â”€ Recursive source discovery without shell find â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# This avoids the Windows/PowerShell + sh.exe path issue and works on Unix too.
rwildcard = $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2)$(filter $(subst *,%,$2),$d))
SRCS := $(call rwildcard,$(SRC_DIR)/,*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TESTS := $(call rwildcard,$(TEST_DIR)/,*.ocl)

# â”€â”€ Sanitizers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
ifeq ($(DETECTED_OS),WINDOWS)
  SANITIZE_FLAGS :=
else ifeq ($(DETECTED_OS),MACOS)
  SANITIZE_FLAGS := -fsanitize=address -fsanitize=undefined
else
  SANITIZE_FLAGS := -fsanitize=address,undefined
endif

# â”€â”€ Compiler flags â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
CFLAGS_COMMON := \
    -std=c11 \
    -g \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Wno-unused-parameter \
    -D_POSIX_C_SOURCE=200809L \
    -DOCL_DEFAULT_NUMOS_CC=\"$(NUMOS_CC)\" \
    -DOCL_DEFAULT_NUMOS_LD=\"$(NUMOS_LD)\" \
    -DOCL_DEFAULT_NUMOS_AS=\"$(NUMOS_AS)\" \
    -iquote $(INC_DIR)

CFLAGS_DEBUG   := $(CFLAGS_COMMON) -g3 -O0 -DDEBUG $(SANITIZE_FLAGS)
CFLAGS_RELEASE := $(CFLAGS_COMMON) -O2 -DNDEBUG

# â”€â”€ Linker flags â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
LDFLAGS :=
ifeq ($(DETECTED_OS),WINDOWS)
  LDFLAGS += -lws2_32 -lbcrypt
endif

LDFLAGS_DEBUG := $(LDFLAGS) $(if $(filter WINDOWS,$(DETECTED_OS)),,$(SANITIZE_FLAGS))

# Defaults
CFLAGS ?= $(CFLAGS_DEBUG)
LFLAGS ?= $(LDFLAGS_DEBUG)

# â”€â”€ Default target â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
.PHONY: all
all: CFLAGS := $(CFLAGS_DEBUG)
all: LFLAGS := $(LDFLAGS_DEBUG)
all: $(TARGET)

# â”€â”€ Valgrind build â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
.PHONY: valgrind
valgrind: CFLAGS := $(CFLAGS_COMMON) -g -O0
valgrind: LFLAGS := $(LDFLAGS)
valgrind: clean $(TARGET)
	@echo "Valgrind build complete: $(TARGET)"

# â”€â”€ Release build â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
.PHONY: release
release: CFLAGS := $(CFLAGS_RELEASE)
release: LFLAGS := $(LDFLAGS)
release: clean $(TARGET)
	@echo "Release build complete: $(TARGET)"

# â”€â”€ Debug build â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
.PHONY: debug
debug: CFLAGS := $(CFLAGS_DEBUG)
debug: LFLAGS := $(LDFLAGS_DEBUG)
debug: clean $(TARGET)
	@echo "Debug build complete: $(TARGET)"

# â”€â”€ Ensure build/ exists â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$(BUILD_DIR):
ifeq ($(DETECTED_OS),WINDOWS)
	@powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(BUILD_DIR)' | Out-Null"
else
	@mkdir -p $(BUILD_DIR)
endif

# â”€â”€ Link â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$(TARGET): $(BUILD_DIR) $(OBJS)
	@echo "  LINK  $@"
	@$(CC) $(OBJS) -o $@ $(LFLAGS)
	@echo "Built $(TARGET) v$(VERSION)"

# â”€â”€ Compile â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
ifeq ($(DETECTED_OS),WINDOWS)
	@powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(dir $@)' | Out-Null"
else
	@mkdir -p $(dir $@)
endif
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# â”€â”€ Clean â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
.PHONY: clean
clean:
	@echo "  CLEAN"
ifeq ($(DETECTED_OS),WINDOWS)
	@powershell -NoProfile -Command "if (Test-Path -LiteralPath '$(BUILD_DIR)') { Remove-Item -LiteralPath '$(BUILD_DIR)' -Recurse -Force }"
else
	@rm -rf $(BUILD_DIR)
endif

# â”€â”€ Test (.ocl integration tests only) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
.PHONY: test
ifeq ($(DETECTED_OS),WINDOWS)
test: all
	@echo "Running tests"
	@powershell -NoProfile -Command "$$pass = 0; $$fail = 0; $$tests = Get-ChildItem -LiteralPath '$(TEST_DIR)' -Recurse -Filter *.ocl | Sort-Object FullName; foreach ($$f in $$tests) { $$expected = [System.IO.Path]::ChangeExtension($$f.FullName, '.expected'); $$argsFile = [System.IO.Path]::ChangeExtension($$f.FullName, '.args'); $$cliArgs = @($$f.FullName); if (Test-Path -LiteralPath $$argsFile) { $$fileArgs = @(Get-Content -LiteralPath $$argsFile); $$cliArgs += '--'; $$cliArgs += $$fileArgs; } if (Test-Path -LiteralPath $$expected) { $$actual = (& '$(TARGET)' $$cliArgs 2>&1 | Out-String).TrimEnd(\"`r\", \"`n\"); $$want = (Get-Content -LiteralPath $$expected -Raw).TrimEnd(\"`r\", \"`n\"); if ($$actual -ceq $$want) { Write-Host ('  PASS  ' + $$f.FullName); $$pass++; } else { Write-Host ('  FAIL  ' + $$f.FullName); Write-Host ('    expected: ' + $$want); Write-Host ('    actual:   ' + $$actual); $$fail++; } } else { & '$(TARGET)' $$cliArgs; if ($$LASTEXITCODE -eq 0) { Write-Host ('  RUN   ' + $$f.FullName + ' (no .expected)'); $$pass++; } else { $$fail++; } } }; Write-Host ('  Results: ' + $$pass + ' passed, ' + $$fail + ' failed'); if ($$fail -ne 0) { exit 1 }"
else
test: all
	@echo "Running tests"
	@pass=0; fail=0; \
	for f in $(TESTS); do \
	    [ -f "$$f" ] || continue; \
	    expected="$${f%.ocl}.expected"; \
	    argsfile="$${f%.ocl}.args"; \
	    if [ -f "$$expected" ]; then \
	        if [ -f "$$argsfile" ]; then \
	            set --; \
	            while IFS= read -r line || [ -n "$$line" ]; do set -- "$$@" "$$line"; done < "$$argsfile"; \
	            actual=$$($(TARGET) "$$f" -- "$$@" 2>&1); \
	        else \
	            actual=$$($(TARGET) "$$f" 2>&1); \
	        fi; \
	        if [ "$$actual" = "$$(cat $$expected)" ]; then \
	            echo "  PASS  $$f"; pass=$$((pass+1)); \
	        else \
	            echo "  FAIL  $$f"; \
	            echo "    expected: $$(cat $$expected)"; \
	            echo "    actual:   $$actual"; \
	            fail=$$((fail+1)); \
	        fi; \
	    else \
	        if [ -f "$$argsfile" ]; then \
	            set --; \
	            while IFS= read -r line || [ -n "$$line" ]; do set -- "$$@" "$$line"; done < "$$argsfile"; \
	            $(TARGET) "$$f" -- "$$@" && echo "  RUN   $$f (no .expected)" && pass=$$((pass+1)) || fail=$$((fail+1)); \
	        else \
	            $(TARGET) "$$f" && echo "  RUN   $$f (no .expected)" && pass=$$((pass+1)) || fail=$$((fail+1)); \
	        fi; \
	    fi; \
	done; \
	echo "  Results: $$pass passed, $$fail failed"
endif
.PHONY: run
run: all
	@[ -n "$(FILE)" ] || (echo "Usage: make run FILE=path/to/file.ocl" && exit 1)
	@$(TARGET) $(FILE)

# â”€â”€ Build a Linux ELF (Linux host only) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
.PHONY: elf
elf: release
	@if [ -n "$(FILE)" ]; then \
		out="$(OUT)"; \
		if [ -z "$$out" ]; then out="$${FILE%.ocl}.elf"; fi; \
		$(TARGET) --emit-elf "$$out" "$(FILE)"; \
	else \
		echo "Built Linux ELF interpreter: $(TARGET)"; \
		echo "Use 'make elf FILE=path/to/file.ocl [OUT=path/to/output]' to compile an OCL program to its own ELF."; \
	fi

# â”€â”€ Help â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
.PHONY: help
help:
	@echo "OCL Interpreter v$(VERSION) â€” Makefile targets:"
	@echo "  make            Build with debug + sanitizers (sanitizers disabled on Windows)"
	@echo "  make release    Build optimised release binary"
	@echo "  make debug      Explicit debug build"
	@echo "  make clean      Remove build artefacts"
	@echo "  make test       Build and run all .ocl integration tests in $(TEST_DIR)/ recursively"
	@echo "  make run FILE=x Build and run a specific .ocl file"
	@echo "  make elf        Build the interpreter as a Linux ELF (Linux only)"
	@echo "  make elf FILE=x Build a specific .ocl file as a Linux ELF (Linux only)"
	@echo "  make help       Show this message"
	@echo ""
	@echo "  Detected OS: $(DETECTED_OS)"
	@echo "  Sanitizers:  $(if $(SANITIZE_FLAGS),$(SANITIZE_FLAGS),disabled)"
