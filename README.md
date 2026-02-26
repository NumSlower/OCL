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

# Build and run via make
make run FILE=path/to/program.ocl

# Run the test suite
make test
```

### Test Suite

Tests live in `test/*.ocl`. Each test may have a paired `test/*.expected` file containing the expected stdout output. The `make test` target diffs actual vs. expected and reports pass/fail counts.

---

## Language Reference

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

Optional bit-width suffix is supported for integers: `int32`, `int64` (default is 64-bit).

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
- A function with a declared return type must end with a `return` statement.
- `main()` is the entry point. If it exists, it is called automatically.

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

### Operators

| Category     | Operators                              |
|--------------|----------------------------------------|
| Arithmetic   | `+`, `-`, `*`, `/`, `%`                |
| Comparison   | `==`, `!=`, `<`, `<=`, `>`, `>=`       |
| Logical      | `&&`, `\|\|`, `!`                      |
| Assignment   | `=`                                    |
| String concat| `+` (when both operands are strings)   |

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
```

Import statements are parsed and recognised but not currently resolved at runtime (the standard header `CoreSX.sxh` declares `printf` which is already built in).

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

### Type Conversions
`toInt`, `toFloat`, `toString`, `toBool`, `typeOf`

### Utilities
`exit`, `assert`, `isNull`, `isInt`, `isFloat`, `isString`, `isBool`

---

## What's Implemented

- ✅ **Lexer** — full tokenisation including block comments (`/# … #/`), string/char literals with escape sequences, integer and float literals, all operators, and all keywords
- ✅ **Parser** — recursive-descent parser producing a complete AST for:
  - `Let` and C-style variable declarations
  - Function declarations with typed parameters and return types
  - `if` / `else if` / `else`
  - `while` loops
  - `for` loops (C-style init/condition/increment)
  - `return`, `break`, `continue` statements
  - Full expression hierarchy (assignment, logical, comparison, arithmetic, unary, call, index)
  - Array index access (`a[i]`)
  - `Import` statements
- ✅ **Type Checker** — two-pass scoped symbol table; detects undefined variables/functions, argument count mismatches, duplicate declarations
- ✅ **Code Generator** — full AST-to-bytecode emission including:
  - Global and local variable slots
  - Forward-reference function calls (two-pass registration)
  - Backpatched jump instructions for branches and loops
  - Built-in function resolution by name
- ✅ **VM** — stack-based bytecode interpreter with:
  - Full arithmetic (`+`, `-`, `*`, `/`, `%`) for int and float, including string concatenation via `+`
  - All comparison and logical operators
  - `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_JUMP_IF_TRUE`
  - Function calls with proper call frames and local variable slots
  - Global variable load/store
  - Type coercions (`OP_TO_INT`, `OP_TO_FLOAT`, `OP_TO_STRING`)
  - `print` and `printf` with format specifiers (`%s`, `%d`, `%f`, `%b`, `%%`)
  - Division-by-zero detection
  - Stack overflow / underflow detection
  - `OP_HALT` with exit code propagation
- ✅ **Standard Library** — all 34 built-in functions listed above
- ✅ **Error reporting** — `ErrorCollector` accumulates lexer, parser, type, and runtime errors with source location (`file:line:col`)
- ✅ **`--time` flag** — reports execution time in µs / ms / s
- ✅ **`make test`** — automated `.ocl` / `.expected` test runner

---

## What's Not Yet Implemented / Known Limitations

- ❌ **`break` / `continue`** — parsed and represented in the AST but the codegen emits nothing for them. Loops cannot be exited early.
- ❌ **Arrays** — `OP_ARRAY_NEW`, `OP_ARRAY_GET`, `OP_ARRAY_SET`, `OP_ARRAY_LEN` are defined in the opcode table and array literals/index access exist in the AST, but the VM prints a "not yet implemented" error for all array opcodes.
- ❌ **Import resolution** — `Import <file.sxh>` is parsed but the referenced file is never loaded or executed.
- ❌ **`declare` keyword** — tokenised and parsed as `AST_DECLARE` but codegen ignores it.
- ❌ **Else-if short-circuit / fall-through** — `else if` is supported but implemented by wrapping the inner `if` in a synthetic block; semantics are correct, just slightly heavier than necessary.
- ❌ **Unit tests** — `test/test_lexer.c`, `test/test_parser.c`, `test/test_type_checker.c`, and `test/test_vm.c` exist with placeholder `TODO` bodies. The Makefile's `test` target runs `.ocl` integration tests only, not these C unit tests.
- ❌ **String escape in `printf` format** — escape sequences inside format strings are processed at runtime in the VM but are already interpreted by the lexer, meaning `\n` in a `printf` format literal is a literal newline at the source level; this is intentional but can surprise users writing `printf("line1\nline2")`.
- ❌ **Closures / first-class functions** — functions are top-level only; no lambdas, no function values.
- ❌ **Structs / custom types** — no user-defined types.
- ❌ **Garbage collection** — string values on the VM stack are not reference-counted or GC'd. Strings produced by built-ins (`toUpperCase`, `strReplace`, etc.) and by the string `+` operator are heap-allocated and freed when the stack frame is torn down, but strings stored in the constants table are freed only when `bytecode_free()` is called. In long-running programs that generate many dynamic strings this could accumulate.
- ⚠️ **`printf` colon syntax** — `printf("fmt": arg1, arg2)` is OCL-specific. The more standard comma syntax (`printf("fmt", arg1, arg2)`) also works because the parser treats the first comma-separated argument as the format string.
- ⚠️ **Type checker is advisory** — the type checker reports errors and returns `false` from `type_checker_check()`, but it does not prevent code generation or execution if you bypass the error check.

---

## Project Structure

```
.
├── Makefile
├── include/
│   ├── ast.h
│   ├── bytecode.h
│   ├── codegen.h
│   ├── common.h
│   ├── errors.h
│   ├── lexer.h
│   ├── parser.h
│   ├── runtime.h
│   ├── stdlib.h          # OCL stdlib (shadows system stdlib.h — handled carefully)
│   ├── type_checker.h
│   └── vm.h
├── src/
│   ├── common.c           # Value constructors, memory helpers
│   ├── frontend/
│   │   ├── errors.c       # ErrorCollector implementation
│   │   └── main.c         # Entry point, pipeline orchestration
│   ├── interpreter/
│   │   ├── ast.c          # AST node constructors and recursive free
│   │   ├── codegen.c      # AST → Bytecode code generator
│   │   ├── lexer.c        # Tokeniser
│   │   ├── parser.c       # Recursive-descent parser
│   │   └── type_checker.c # Symbol table & type inference
│   ├── stdlib/
│   │   └── stdlib.c       # Standard library built-in functions
│   └── vm/
│       ├── bytecode.c     # Bytecode chunk management
│       ├── runtime.c      # Frame/global helpers, error reporting
│       └── vm.c           # Main execution loop
├── test/
│   ├── test_lexer.c       # (stub)
│   ├── test_parser.c      # (stub)
│   ├── test_type_checker.c# (stub)
│   └── test_vm.c          # (stub)
└── ocl files/             # Example and test .ocl programs
    ├── main.ocl
    ├── test.ocl
    └── ...
```