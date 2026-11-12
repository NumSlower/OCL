# OCL Interpreter

A bytecode-compiled interpreter for **OCL** (a custom scripting language), written in C11. The interpreter takes `.ocl` source files through a full pipeline: lexing → parsing → type checking → bytecode code generation → VM execution.

---

## Table of Contents

- [Building](#building)
- [Usage](#usage)
- [Language Reference](#language-reference)
- [Architecture](#architecture)
- [Standard Library](#standard-library)
- [What's Implemented](#whats-implemented)
- [What's Not Yet Implemented / Known Limitations](#whats-not-yet-implemented--known-limitations)
- [Project Structure](#project-structure)

---

## Building

Requires a C11 compiler (`cc`) and `make`.

```bash
# Debug build of ./ocl (no host libc, no host libm, no crt, no system headers)
make

# Release build (optimised)
make release

# Explicit debug build
make debug

# Clean all build artefacts
make clean

# Verify every header resolves inside the repository
make check-nodeps

# Verify the from-scratch C runtime on its own
make check-libc
```

The resulting binary is `./ocl`.

### No third-party code

There is exactly one build, and it links against nothing but OCL's own
runtime in `src/libc/`. It is compiled with
`-ffreestanding -fno-builtin -nostdinc -nostdlib` and links with
`-nostdlib -static`:

| Area | File | Notes |
|------|------|-------|
| OS primitives | `src/libc/port_linux.c` | Raw `syscall`/`svc` — the only host-specific file |
| Memory | `src/libc/mem.c` | `memcpy`, `memmove`, `memset`, `memcmp`, `str*` |
| Allocator | `src/libc/alloc.c` | First-fit free list with coalescing over `mmap` |
| stdio | `src/libc/file.c` | Unbuffered `FILE` over descriptors |
| printf | `src/libc/fmt.c` | Full formatter: `%d %u %o %x %c %s %p %f %e %g`, flags/width/precision |
| Numbers | `src/libc/num.c` | `strtol`, `strtoll`, `strtoull`, `strtod`, `atoi` |
| Math | `src/libc/math.c` | `sqrt`, `pow`, `exp`, `log`, `sin`, `cos`, `tan`, `atan`, `floor`, `ceil`, `round`, `fmod`, ... |
| Misc | `src/libc/misc.c` | `ctype`, `rand`, `clock_gettime`, `strerror`, `errno` |
| Entry point | `src/libc/start.c` | `_start` for `-nostdlib` links |

`include/freestanding/` supplies every header the build can see — including
`stddef.h`, `stdint.h`, `stdbool.h` and `stdarg.h` — so neither the system
header directory nor the compiler's own header directory is on the search
path (`-nostdinc`, no `-isystem`). Porting to a new host means reimplementing
the primitives in `src/libc/port.h` and nothing else.

Verification:

```bash
make check-nodeps              # every header resolves in-tree
nm -u ocl                      # no undefined symbols
ldd ocl                        # "not a dynamic executable"
make check-libc                # 90 runtime self-tests
make test                      # full .ocl suite
```

---

## Usage

```bash
# Run a source file
./ocl path/to/program.ocl

# Run with execution timing
./ocl --time path/to/program.ocl

# Print all lexer tokens and exit
./ocl --dump-tokens path/to/program.ocl

# Print bytecode disassembly and exit
./ocl --dump-bytecode path/to/program.ocl

# Skip the type checker
./ocl --no-typecheck path/to/program.ocl

# Build and run via make
make run FILE=path/to/program.ocl

# Run the test suite
make test
```

### Test Suite

Tests live in `test/*.ocl`. Each test may have a paired `test/*.expected` file containing the expected stdout output. The `make test` target diffs actual vs. expected and reports pass/fail counts.

---

## Language Reference

See [LANGUAGE_REFERENCE.md](LANGUAGE_REFERENCE.md) for the full language reference, including syntax, operators, control flow, functions, arrays, built-ins, and known edge cases.

A quick summary follows below.

### Variables

OCL supports two declaration styles:

**OCL-style** (preferred):
```ocl
Let name:Type = value;
```

**C-style** (also accepted):
```ocl
Type name = value;
Type name;          /# declaration without initialiser #/
```

**Types:** `Int` / `int`, `Float` / `float`, `String` / `string`, `Bool` / `bool`, `Char` / `char`

Optional bit-width suffix is accepted for integers: `int32`, `int64` (all integers are 64-bit at runtime).

```ocl
Let x:Int = 42;
Let pi:Float = 3.14;
Let greeting:String = "Hello";
Let flag:Bool = true;
Let ch:Char = 'A';
```

### Comments

Block comments only (no single-line `//` comments):

```ocl
/# This is a comment #/

/#
  Multi-line comment
#/
```

### Functions

```ocl
/# Void function (no return type = void) #/
func greet() {
    print("Hello!");
}

/# Function with return type #/
func int add(a:int, b:int) {
    return a + b;
}

/# Explicit void #/
func void doSomething(name:string) {
    print(name);
}
```

- Return type is written **before** the function name.
- If no return type is specified, the function is `void`.
- `main()` is the entry point. If it exists, it is called automatically after global initialisers.

### Control Flow

**If / else:**
```ocl
if (x > 10) {
    print("big");
} else {
    print("small");
}
```

**While loop:**
```ocl
while (counter < 5) {
    print(counter);
    counter = counter + 1;
}
```

**For loop:**
```ocl
for (int i = 0; i < 5; i = i + 1) {
    print(i);
}
```

**Break / continue:**
```ocl
while (true) {
    if (done) { break; }
    if (skip) { continue; }
    doWork();
}
```

### Arrays

```ocl
Let nums = [1, 2, 3];
Let first = nums[0];
nums[0] = 99;
arrayPush(nums, 4);
Let len = arrayLen(nums);
```

Strings also support index access, returning a `Char`:

```ocl
Let ch = "hello"[0];   /# 'h' #/
```

### Operators

| Category     | Operators                              |
|--------------|----------------------------------------|
| Arithmetic   | `+`, `-`, `*`, `/`, `%`                |
| Comparison   | `==`, `!=`, `<`, `<=`, `>`, `>=`       |
| Logical      | `&&`, `\|\|`, `!`                      |
| Assignment   | `=`                                    |
| Increment    | `++`, `--` (prefix and postfix; desugar to `x = x ± 1`) |
| String concat| `+` (when both operands are strings)   |

`&&` and `||` are **short-circuit** — the right-hand operand is only evaluated when necessary.

### Print / Printf

```ocl
/# Simple print — appends a newline #/
print(42);
print("Hello");
print(x);

/# Formatted print — colon separates format string from arguments #/
printf("Hello %s, you are %d years old\n": name, age);
printf("Pi is approximately %f\n": 3.14159);
```

**Format specifiers:** `%s` (string/char), `%d` or `%i` (integer), `%f` (float), `%b` (boolean), `%%` (literal `%`).

### Import

```ocl
Import <CoreSX.sxh>
Import <mylib.ocl>
```

The parser searches for the file relative to the importing file's directory, then under `ocl_headers/` and `stdlib_headers/` (next to the executable and in the current directory). If found, the file is inlined at parse time. If not found, a parse error is raised.

`CoreSX.sxh` is built into the runtime, so `Import <CoreSX.sxh>` always succeeds even when no header file is present on disk.

---

## Architecture

The interpreter runs a five-stage pipeline:

```
Source (.ocl)
    │
    ▼
┌─────────┐
│  Lexer  │  Tokenises source into a flat token array
└────┬────┘
     │
     ▼
┌──────────┐
│  Parser  │  Recursive-descent; produces an AST (ProgramNode)
└────┬─────┘
     │
     ▼
┌──────────────┐
│ Type Checker │  Two-pass symbol-table check; reports type errors
└──────┬───────┘
       │
       ▼
┌──────────┐
│  Codegen │  Walks the AST; emits a flat Bytecode chunk
└────┬─────┘
     │
     ▼
┌────────┐
│   VM   │  Register-less stack machine; executes the bytecode
└────────┘
```

### Key Data Structures

| Structure | File | Purpose |
|-----------|------|---------|
| `Lexer` | `src/interpreter/lexer.c` | Streaming tokeniser |
| `ProgramNode` / `ASTNode` | `src/interpreter/ast.c` | AST node hierarchy |
| `Parser` | `src/interpreter/parser.c` | Pratt-style precedence-climbing parser |
| `TypeChecker` / `SymbolTable` | `src/interpreter/type_checker.c` | Scoped symbol resolution |
| `CodeGenerator` | `src/interpreter/codegen.c` | AST → bytecode emitter |
| `Bytecode` | `src/vm/bytecode.c` | Instruction + constant + function tables |
| `VM` | `src/vm/vm.c` | Bytecode execution engine |

---

## Standard Library

Built-in functions are resolved at compile time by name and dispatched via `OP_CALL_BUILTIN`.

### I/O
| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `print(value)` | Print a value followed by a newline |
| `printf` | `printf(fmt: args...)` | Formatted print |
| `input` | `input(prompt?)` → `String` | Read a line from stdin |
| `readLine` | `readLine()` → `String` | Read a line with no prompt |

### Math
`abs`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `floor`, `ceil`, `round`, `max`, `min`

### String
`strLen`, `substr`, `toUpperCase`, `toLowerCase`, `strContains`, `strIndexOf`, `strReplace`, `strTrim`, `strSplit`

### Array
`arrayNew`, `arrayPush`, `arrayPop`, `arrayGet`, `arraySet`, `arrayLen`

### Type Conversions
`toInt`, `toFloat`, `toString`, `toBool`, `typeOf`

### Utilities
`exit`, `assert`, `isNull`, `isInt`, `isFloat`, `isString`, `isBool`

---

## What's Implemented

- ✅ **Lexer** — full tokenisation including block comments (`/# … #/`), string/char literals with escape sequences, integer and float literals, all operators, and all keywords
- ✅ **Parser** — recursive-descent parser producing a complete AST for:
  - `Let` and C-style variable declarations
  - `declare` keyword (forward-declares a variable to the type checker)
  - Function declarations with typed parameters and return types
  - `if` / `else if` / `else` (flat chain, no synthetic wrapper nodes)
  - `while` loops
  - `for` loops (C-style init/condition/increment; both `Let` and C-style init)
  - `return`, `break`, `continue` statements
  - Full expression hierarchy (assignment, logical, comparison, arithmetic, unary, call, index)
  - Prefix and postfix `++` / `--` (desugared to `x = x ± 1`)
  - Array literal syntax (`[1, 2, 3]`)
  - Chained array index access (`a[i][j]`)
  - `Import` statements with file resolution and inline merging
- ✅ **Type Checker** — two-pass scoped symbol table; detects undefined variables/functions, argument count mismatches, duplicate declarations
- ✅ **Code Generator** — full AST-to-bytecode emission including:
  - Global and local variable slots
  - Forward-reference function calls (two-pass registration)
  - Backpatched jump instructions for branches and loops
  - Built-in function resolution by name
  - `break` and `continue` via a loop-context stack with full backpatching
  - Array literals emitting `OP_ARRAY_NEW` with element count
  - Array index reads/writes emitting `OP_ARRAY_GET` / `OP_ARRAY_SET`
  - `declare` nodes emitting a null-initialised variable slot
  - Short-circuit `&&` and `||` via `OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` sequences
- ✅ **VM** — stack-based bytecode interpreter with:
  - Full arithmetic (`+`, `-`, `*`, `/`, `%`) for int and float, including string concatenation via `+`
  - All comparison and logical operators
  - `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_JUMP_IF_TRUE`
  - Function calls with proper call frames and local variable slots
  - Global variable load/store
  - Type coercions (`OP_TO_INT`, `OP_TO_FLOAT`, `OP_TO_STRING`)
  - `print` and `printf` with format specifiers (`%s`, `%d`, `%f`, `%b`, `%%`)
  - Full array opcodes: `OP_ARRAY_NEW`, `OP_ARRAY_GET`, `OP_ARRAY_SET`, `OP_ARRAY_LEN`
  - String character indexing via `OP_ARRAY_GET` (returns `Char`)
  - `arrayLen` on strings (returns byte length)
  - Division-by-zero and modulo-by-zero detection
  - Stack overflow / underflow detection
  - `OP_HALT` with exit code propagation
- ✅ **Short-circuit `&&` / `||`** — both operators are now properly short-circuit. The code generator emits `OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` sequences so the right-hand operand is skipped when unnecessary.
- ✅ **`break` / `continue`** — fully implemented end-to-end: parsed, represented in the AST, and emitted by the code generator using a per-loop `LoopContext` stack. `break` jumps to the first instruction after the loop; `continue` jumps to the backwards-jump / increment point for `while` / `for` loops respectively. Both are backpatched once the loop body has been fully emitted.
- ✅ **Arrays** — array literals, index access (`a[i]`), chained index access, and all four array opcodes are fully implemented in the VM. The `arrayNew`, `arrayPush`, `arrayPop`, `arrayGet`, `arraySet`, and `arrayLen` built-ins are also implemented.
- ✅ **Import resolution** — `Import <file>` searches for the named file relative to the importing file's directory, then under `ocl_headers/` and `stdlib_headers/`. Found files are lexed and merged into the AST at parse time.
- ✅ **`declare` keyword** — parsed and code-generated; creates a null-initialised variable slot and registers the name/type with the type checker.
- ✅ **Standard Library** — all 40 built-in functions listed above
- ✅ **Error reporting** — `ErrorCollector` accumulates lexer, parser, type, and runtime errors with source location (`file:line:col`)
- ✅ **`--time` flag** — reports execution time in µs / ms / s
- ✅ **`make test`** — automated `.ocl` / `.expected` test runner

---

## What's Not Yet Implemented / Known Limitations

- ❌ **`strSplit` does not return tokens** — `strSplit(s, delim)` returns the count of parts as an `Int`, not an array of strings. The individual tokens are not accessible from OCL code.
- ❌ **Unit tests** — `test/test_lexer.c`, `test/test_parser.c`, `test/test_type_checker.c`, and `test/test_vm.c` exist with placeholder `TODO` bodies. The Makefile's `test` target runs `.ocl` integration tests only, not these C unit tests.
- ❌ **Closures / first-class functions** — functions are top-level only; no lambdas, no function values, no passing functions as arguments.
- ❌ **Structs / custom types** — no user-defined types.
- ❌ **Compound assignment operators** — `+=`, `-=`, `*=`, `/=` are not implemented. Use `x = x + 1` etc.
- ❌ **Garbage collection** — string values on the VM stack are not reference-counted or GC'd. Strings produced by built-ins (`toUpperCase`, `strReplace`, etc.) and by the string `+` operator are heap-allocated and freed when the stack frame is torn down, but strings stored in the constants table are freed only when `bytecode_free()` is called. Long-running programs that generate many dynamic strings may accumulate memory.
- ⚠️ **`value_to_string` uses a rotating buffer pool** — the internal conversion function writes into a pool of 8 static buffers (8 KiB each), cycling round-robin. Nesting beyond 8 levels deep within a single expression (e.g. arrays of arrays of arrays) may corrupt output, though this is rarely a concern in practice.
- ⚠️ **`printf` colon syntax** — `printf("fmt": arg1, arg2)` is OCL-specific. A comma also works: `printf("fmt", arg1, arg2)`.
- ⚠️ **Escape sequences are lexer-resolved** — `\n` and other escapes in string literals are converted to real characters when the source is tokenised. This is intentional but means there is no way to produce a string containing a literal two-character sequence `\n` from source code.
- ⚠️ **Type checker is advisory** — the type checker reports errors and returns `false` from `type_checker_check()`, but it does not prevent code generation or execution if you bypass the error check (or pass `--no-typecheck`).
- ⚠️ **Call stack depth** — the VM supports a maximum call depth of 4096 frames (`VM_FRAMES_MAX`). Deep recursion beyond this produces a "call stack overflow" runtime error.

---

## Project Structure

```
.
├── Makefile
├── README.md
├── LANGUAGE_REFERENCE.md
├── docs/
│   └── INSTALL.md
├── include/
│   ├── ast.h
│   ├── bytecode.h
│   ├── codegen.h
│   ├── common.h
│   ├── errors.h
│   ├── freestanding/      # Every header the build can see
│   │   ├── ctype.h
│   │   ├── errno.h
│   │   ├── inttypes.h
│   │   ├── math.h
│   │   ├── stdarg.h
│   │   ├── stdbool.h
│   │   ├── stddef.h
│   │   ├── stdint.h
│   │   ├── stdio.h
│   │   ├── stdlib.h
│   │   ├── string.h
│   │   └── time.h
│   ├── lexer.h
│   ├── ocl_stdlib.h      # OCL stdlib declarations
│   ├── parser.h
│   ├── runtime.h
│   ├── type_checker.h
│   └── vm.h
├── src/
│   ├── common.c           # Value constructors, array heap object, memory helpers
│   ├── frontend/
│   │   ├── errors.c       # ErrorCollector implementation
│   │   └── main.c         # Entry point, pipeline orchestration
│   ├── interpreter/
│   │   ├── ast.c          # AST node constructors and recursive free
│   │   ├── codegen.c      # AST → Bytecode code generator
│   │   ├── lexer.c        # Tokeniser
│   │   ├── parser.c       # Recursive-descent parser + import resolution
│   │   └── type_checker.c # Symbol table & type inference
│   ├── libc/              # From-scratch C runtime (the only runtime)
│   │   ├── alloc.c        # malloc/calloc/realloc/free
│   │   ├── file.c         # FILE streams over raw descriptors
│   │   ├── fmt.c          # printf family
│   │   ├── math.c         # libm replacement
│   │   ├── mem.c          # mem*/str* routines
│   │   ├── misc.c         # ctype, rand, time, errno, strerror
│   │   ├── num.c          # strtol/strtod/atoi
│   │   ├── port.h         # OS primitive contract
│   │   ├── port_linux.c   # Raw syscalls (x86-64, aarch64)
│   │   └── start.c        # _start for -nostdlib links
│   ├── stdlib/
│   │   └── stdlib.c       # Standard library built-in functions (40 functions)
│   └── vm/
│       ├── bytecode.c     # Bytecode chunk management
│       ├── runtime.c      # Frame/global helpers, error reporting
│       └── vm.c           # Main execution loop
├── tests/
│   └── libc_selftest.c    # Runtime self-test (built with -nostdlib)
└── Testfiles/
    ├── algorithms/        # Sorting, searching, bigint, matrix, sieve
    ├── crypto/            # AES-128 implementation and KATs
    ├── errors/            # Expected-failure programs
    ├── language/          # Syntax and semantics coverage
    ├── stress/            # Recursion and allocation stress
    └── strings/           # String operation coverage
```