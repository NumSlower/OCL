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
# Debug build (default — includes AddressSanitizer + UBSan)
make

# Release build (optimised, no sanitizers)
make release

# Explicit debug build
make debug

# Clean all build artefacts
make clean
```

The resulting binary is `./ocl`.

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

### Operators

| Category     | Operators                              |
|--------------|----------------------------------------|
| Arithmetic   | `+`, `-`, `*`, `/`, `%`                |
| Comparison   | `==`, `!=`, `<`, `<=`, `>`, `>=`       |
| Logical      | `&&`, `\|\|`, `!`                      |
| Assignment   | `=`                                    |
| Increment    | `++`, `--` (prefix and postfix; desugar to `x = x ± 1`) |
| String concat| `+` (when both operands are strings)   |

> **Note:** `&&` and `||` are **not short-circuit** — both operands are always evaluated.

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

The parser searches for the file relative to the importing file's directory, then under `ocl_headers/` and `stdlib_headers/`. If found, the file is inlined at parse time. If not found, a parse error is raised.

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
- ✅ **VM** — stack-based bytecode interpreter with:
  - Full arithmetic (`+`, `-`, `*`, `/`, `%`) for int and float, including string concatenation via `+`
  - All comparison and logical operators
  - `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_JUMP_IF_TRUE`
  - Function calls with proper call frames and local variable slots
  - Global variable load/store
  - Type coercions (`OP_TO_INT`, `OP_TO_FLOAT`, `OP_TO_STRING`)
  - `print` and `printf` with format specifiers (`%s`, `%d`, `%f`, `%b`, `%%`)
  - Full array opcodes: `OP_ARRAY_NEW`, `OP_ARRAY_GET`, `OP_ARRAY_SET`, `OP_ARRAY_LEN`
  - Division-by-zero and modulo-by-zero detection
  - Stack overflow / underflow detection
  - `OP_HALT` with exit code propagation
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
- ⚠️ **`&&` and `||` are not short-circuit** — both operands are always fully evaluated before the operator is applied. Guarding a potentially-faulting expression with `&&` (e.g. `ptr != null && ptr.field > 0`) does not protect against the right-hand side being evaluated.
- ⚠️ **`value_to_string` uses a static buffer** — the internal conversion function writes to a single 512-byte static buffer. Printing deeply nested arrays (arrays of arrays) can corrupt output because inner and outer calls share the same buffer.
- ⚠️ **`printf` colon syntax** — `printf("fmt": arg1, arg2)` is OCL-specific. A comma also works: `printf("fmt", arg1, arg2)`.
- ⚠️ **Escape sequences are lexer-resolved** — `\n` and other escapes in string literals are converted to real characters when the source is tokenised. This is intentional but means there is no way to produce a string containing a literal two-character sequence `\n` from source code.
- ⚠️ **Type checker is advisory** — the type checker reports errors and returns `false` from `type_checker_check()`, but it does not prevent code generation or execution if you bypass the error check (or pass `--no-typecheck`).
- ⚠️ **Call stack depth** — the VM supports a maximum call depth of 256 frames (`VM_FRAMES_MAX`). Deep recursion beyond this produces a "call stack overflow" runtime error.

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
│   ├── stdlib/
│   │   └── stdlib.c       # Standard library built-in functions (40 functions)
│   └── vm/
│       ├── bytecode.c     # Bytecode chunk management
│       ├── runtime.c      # Frame/global helpers, error reporting
│       └── vm.c           # Main execution loop
└── test/
    ├── test_lexer.c       # (stub — TODO)
    ├── test_parser.c      # (stub — TODO)
    ├── test_type_checker.c# (stub — TODO)
    └── test_vm.c          # (stub — TODO)

```