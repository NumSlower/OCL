# OCL Language Reference

> **Version:** beta 0.6.0  
> OCL is a statically-scoped, bytecode-compiled scripting language with a C-like syntax and a set of built-in functions for common tasks. This document is the complete reference for the language as it is currently implemented.

---

## Table of Contents

1. [File Format](#file-format)
2. [Comments](#comments)
3. [Types](#types)
4. [Variables](#variables)
5. [Literals](#literals)
6. [Operators](#operators)
7. [Control Flow](#control-flow)
8. [Functions](#functions)
9. [Arrays](#arrays)
10. [Import](#import)
11. [Built-in Functions](#built-in-functions)
12. [Print and Printf](#print-and-printf)
13. [Known Limitations and Edge Cases](#known-limitations-and-edge-cases)

---

## File Format

OCL source files use the `.ocl` extension. The interpreter is invoked as:

```bash
./ocl path/to/program.ocl
./ocl --time path/to/program.ocl      # show execution time
./ocl --dump-tokens path/to/file.ocl  # print lexer tokens and exit
./ocl --dump-bytecode path/to/file.ocl # print bytecode disassembly and exit
./ocl --no-typecheck path/to/file.ocl  # skip the type checker
```

If a top-level `main()` function is defined, it is called automatically after all global variable initialisers have run. If no `main()` exists, top-level statements execute directly in the order they appear.

---

## Comments

OCL supports **block comments only**. There is no single-line `//` comment syntax.

```ocl
/# This is a single-line comment #/

/#
  This is a
  multi-line comment
#/
```

---

## Types

| OCL Type       | Aliases          | Description                          |
|----------------|------------------|--------------------------------------|
| `Int`          | `int`, `int32`, `int64` | 64-bit signed integer           |
| `Float`        | `float`          | 64-bit IEEE 754 double               |
| `String`       | `string`         | UTF-8 string (heap-allocated)        |
| `Bool`         | `bool`           | `true` or `false`                    |
| `Char`         | `char`           | Single character (`'A'`)             |
| `void` / `Void`| —                | Return type for functions that return nothing |

> **Note:** `int32` and `int64` are accepted as type names by the parser and are stored as aliases for `Int`. All integers are 64-bit at runtime regardless of the suffix.

---

## Variables

OCL supports two declaration syntaxes.

### OCL-style (preferred)

```ocl
Let name:Type = expression;
Let name:Type;    /# initialised to null if no initialiser #/
```

### C-style

```ocl
Type name = expression;
Type name;        /# initialised to null if no initialiser #/
```

### Examples

```ocl
Let x:Int = 42;
Let pi:Float = 3.14159;
Let greeting:String = "Hello, world!";
Let flag:Bool = true;
Let ch:Char = 'A';

int counter = 0;
float ratio;
```

### Scope Rules

- Variables are lexically scoped. A variable declared inside a `{...}` block is not visible outside it.
- At the top level of a file, variables are **global**.
- Inside a function or block, variables are **local** to that scope.
- Re-declaring a variable in the same scope is a type-checker error.

### `declare` Keyword

The `declare` keyword forward-declares a variable name and type to the type checker without emitting an initialiser. It initialises the slot to `null` at runtime.

```ocl
declare counter:Int;
```

This is useful for referencing a name before its full `Let` declaration is in scope.

---

## Literals

| Literal           | Type     | Example                |
|-------------------|----------|------------------------|
| Integer           | `Int`    | `42`, `0`, `-7`        |
| Float             | `Float`  | `3.14`, `0.5`          |
| String            | `String` | `"hello\nworld"`       |
| Character         | `Char`   | `'A'`, `'\n'`          |
| Boolean           | `Bool`   | `true`, `false`        |
| Array literal     | `Array`  | `[1, 2, 3]`            |

### String Escape Sequences

Escape sequences are processed at **lex time** (when the source is tokenised):

| Sequence | Character |
|----------|-----------|
| `\n`     | Newline   |
| `\t`     | Tab       |
| `\r`     | Carriage return |
| `\\`     | Backslash |
| `\"`     | Double quote |
| `\'`     | Single quote |
| `\0`     | Null byte |

> **Important:** Because escape sequences are resolved by the lexer, a string literal `"line1\nline2"` already contains a real newline by the time it reaches the VM. There is no second layer of escape processing for string values stored in variables.

---

## Operators

### Arithmetic

| Operator | Description            | Types            |
|----------|------------------------|------------------|
| `+`      | Addition or string concatenation | `Int`, `Float`, `String` |
| `-`      | Subtraction            | `Int`, `Float`   |
| `*`      | Multiplication         | `Int`, `Float`   |
| `/`      | Division (integer or float) | `Int`, `Float` |
| `%`      | Modulo                 | `Int` only       |

- Integer division truncates toward zero.
- Using `%` on `Float` operands is a runtime error.
- Division by zero (integer or float) is a runtime error.

### Comparison

| Operator | Description       |
|----------|-------------------|
| `==`     | Equal             |
| `!=`     | Not equal         |
| `<`      | Less than         |
| `<=`     | Less than or equal |
| `>`      | Greater than      |
| `>=`     | Greater than or equal |

String equality (`==`, `!=`) compares content, not identity.  
Mixed `Int`/`Float` comparisons are supported.

### Logical

| Operator | Description   |
|----------|---------------|
| `&&`     | Logical AND (short-circuit) |
| `\|\|`   | Logical OR (short-circuit)  |
| `!`      | Logical NOT   |

`&&` and `||` are **short-circuit**: the right-hand side is only evaluated if the left-hand side does not determine the result. For `&&`, if the left side is falsy the right side is skipped; for `||`, if the left side is truthy the right side is skipped.

### Assignment

```ocl
x = expression;        /# simple assignment #/
arr[i] = expression;   /# array element assignment #/
```

Assignment is an expression (it evaluates to the value assigned) but in practice it is used as a statement.

### Increment and Decrement

`++` and `--` are available in both prefix and postfix positions. They desugar to `x = x + 1` / `x = x - 1` and do **not** return the old value — they behave like prefix operators regardless of position.

```ocl
i++;     /# equivalent to: i = i + 1 #/
++i;     /# same result #/
i--;
--i;
```

### Operator Precedence (high to low)

| Level | Operators                  |
|-------|----------------------------|
| 1 (highest) | Unary: `-`, `!`, prefix `++`/`--` |
| 2     | `*`, `/`, `%`              |
| 3     | `+`, `-`                   |
| 4     | `<`, `<=`, `>`, `>=`       |
| 5     | `==`, `!=`                 |
| 6     | `&&`                       |
| 7     | `\|\|`                     |
| 8 (lowest) | `=` (assignment)      |

---

## Control Flow

### If / Else if / Else

```ocl
if (condition) {
    /# then branch #/
} else if (otherCondition) {
    /# else-if branch #/
} else {
    /# else branch #/
}
```

Conditions must be wrapped in parentheses. Braces are mandatory.

### While Loop

```ocl
while (condition) {
    /# body #/
}
```

### For Loop

```ocl
/# C-style for loop #/
for (int i = 0; i < 10; i = i + 1) {
    print(i);
}

/# OCL-style for loop (Let syntax in init) #/
for (Let i:Int = 0; i < 10; i++) {
    print(i);
}
```

All three clauses (init, condition, increment) are optional:

```ocl
for (;;) {
    /# infinite loop #/
}
```

### Break and Continue

`break` exits the innermost enclosing loop. `continue` skips to the next iteration.

```ocl
while (true) {
    if (done) { break; }
    if (skip) { continue; }
    doWork();
}

for (int i = 0; i < 10; i++) {
    if (i == 5) { continue; }
    print(i);
}
```

Both are compile-time errors if used outside a loop.

---

## Functions

### Declaration

```ocl
/# Void function (no return value) #/
func greet() {
    print("Hello!");
}

/# Function with a return type — type goes before the name #/
func int add(a:int, b:int) {
    return a + b;
}

/# Explicit void return type #/
func void doSomething(name:string) {
    print(name);
}
```

**Syntax rules:**
- The keyword is `func`.
- The optional return type comes **before** the function name.
- Parameters use `name:Type` syntax separated by commas.
- If a return type is declared (and is not `void`), the function body should end with a `return` statement. If it does not, an implicit `return null` is appended by the compiler.
- `main()` is called automatically as the entry point if it exists.

### Calling Functions

```ocl
greet();
Let result:Int = add(3, 4);
```

### Scope and Visibility

All function declarations are hoisted to the top of the program during code generation. A function can therefore call another function declared later in the same file.

Functions are top-level only — **nested function declarations and function values are not supported**.

### Recursion

Recursion is supported up to the call-stack limit (`VM_FRAMES_MAX = 4096` frames).

```ocl
func int factorial(n:int) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}
```

---

## Arrays

Arrays are reference-counted heap objects. An array can hold values of any type (including mixed types).

### Creating Arrays

**Array literal syntax:**
```ocl
Let nums:Int = [1, 2, 3, 4, 5];
Let mixed = [1, "hello", true];
Let empty = [];
```

**`arrayNew` built-in** (creates an array pre-filled with `null`):
```ocl
Let arr = arrayNew(10);   /# array of 10 nulls #/
```

### Accessing Elements

```ocl
Let first = nums[0];      /# index access, 0-based #/
```

Index access is zero-based. Accessing an out-of-bounds index is a runtime error.

Strings also support index access, returning a `Char`:

```ocl
Let ch = "hello"[1];   /# 'e' #/
```

### Assigning Elements

```ocl
nums[0] = 99;
```

### Built-in Array Functions

| Function              | Signature                        | Description                                 |
|-----------------------|----------------------------------|---------------------------------------------|
| `arrayNew(size)`      | `(Int) → Array`                  | Create array of `size` nulls                |
| `arrayPush(arr, val)` | `(Array, Any) → null`            | Append `val` to end of `arr`                |
| `arrayPop(arr)`       | `(Array) → Any`                  | Remove and return last element              |
| `arrayGet(arr, idx)`  | `(Array, Int) → Any`             | Get element at index (returns null if OOB)  |
| `arraySet(arr, idx, val)` | `(Array, Int, Any) → null`  | Set element at index                        |
| `arrayLen(arr)`       | `(Array) → Int`                  | Number of elements                          |

### Chained Index Access

```ocl
Let matrix = [[1,2],[3,4]];
Let val = matrix[0][1];    /# 2 — chained indexing is supported #/
```

---

## Import

```ocl
Import <filename>
Import <CoreSX.sxh>
```

The parser searches for the named file in the following locations (in order):

1. Same directory as the importing file
2. `ocl_headers/<filename>`
3. `stdlib_headers/<filename>`
4. The current working directory

Extensions tried: exact name, then `.ocl`, then `.sxh`.

If the file is found, it is lexed and parsed in-place — its top-level declarations (functions, variables) are merged into the current program as if they had been written there. If the file is **not** found, the interpreter emits a parse error and halts.

> **Note:** `CoreSX.sxh` is a standard header that declares `printf`. Since `printf` is already a built-in, importing it is optional.

---

## Built-in Functions

All built-in functions are resolved at compile time by name. They do not need to be imported.

### I/O

| Function        | Signature                         | Description                              |
|-----------------|-----------------------------------|------------------------------------------|
| `print(val)`    | `(Any) → null`                    | Print a value followed by a newline      |
| `printf(fmt: ...)` | `(String, ...) → null`         | Formatted print (see [Printf](#print-and-printf)) |
| `input(prompt?)` | `(String?) → String`            | Print optional prompt, read a line from stdin |
| `readLine()`    | `() → String`                     | Read a line from stdin with no prompt    |

### Math

| Function     | Signature              | Description                           |
|--------------|------------------------|---------------------------------------|
| `abs(x)`     | `(Int\|Float) → same`  | Absolute value                        |
| `sqrt(x)`    | `(Any) → Float`        | Square root (returns 0.0 for negatives) |
| `pow(x, y)`  | `(Any, Any) → Float`   | x raised to the power y               |
| `sin(x)`     | `(Any) → Float`        | Sine (radians)                        |
| `cos(x)`     | `(Any) → Float`        | Cosine (radians)                      |
| `tan(x)`     | `(Any) → Float`        | Tangent (radians)                     |
| `floor(x)`   | `(Any) → Float`        | Round down                            |
| `ceil(x)`    | `(Any) → Float`        | Round up                              |
| `round(x)`   | `(Any) → Float`        | Round to nearest integer              |
| `max(a, b)`  | `(Int\|Float, Int\|Float) → same` | Larger of two values    |
| `min(a, b)`  | `(Int\|Float, Int\|Float) → same` | Smaller of two values   |

### String

| Function                       | Signature                          | Description                                      |
|--------------------------------|------------------------------------|--------------------------------------------------|
| `strLen(s)`                    | `(String) → Int`                   | Length in bytes                                  |
| `substr(s, start, length?)`    | `(String, Int, Int?) → String`     | Substring from `start` for `length` chars        |
| `toUpperCase(s)`               | `(String) → String`                | Convert to upper case                            |
| `toLowerCase(s)`               | `(String) → String`                | Convert to lower case                            |
| `strContains(s, needle)`       | `(String, String) → Bool`          | Whether `s` contains `needle`                   |
| `strIndexOf(s, needle)`        | `(String, String) → Int`           | First index of `needle` in `s`, or `-1`          |
| `strReplace(s, old, new)`      | `(String, String, String) → String`| Replace all occurrences of `old` with `new`      |
| `strTrim(s)`                   | `(String) → String`                | Strip leading and trailing whitespace            |
| `strSplit(s, delim)`           | `(String, String) → Int`           | **Returns the count of tokens**, not an array    |

> **`strSplit` limitation:** The current implementation splits the string and returns the number of parts as an `Int`. The individual tokens are not accessible. Use manual string operations or `strIndexOf` as a workaround.

### Type Conversion

| Function         | Signature          | Description                                    |
|------------------|--------------------|------------------------------------------------|
| `toInt(x)`       | `(Any) → Int`      | Convert to integer (strings parsed as base-10) |
| `toFloat(x)`     | `(Any) → Float`    | Convert to float                               |
| `toString(x)`    | `(Any) → String`   | Convert to string representation               |
| `toBool(x)`      | `(Any) → Bool`     | Convert to boolean (0/empty/"" → false)        |
| `typeOf(x)`      | `(Any) → String`   | Returns `"Int"`, `"Float"`, `"String"`, `"Bool"`, `"Char"`, `"Array"`, or `"null"` |

### Inspection

| Function        | Signature        | Description                   |
|-----------------|------------------|-------------------------------|
| `isNull(x)`     | `(Any) → Bool`   | True if `x` is `null`         |
| `isInt(x)`      | `(Any) → Bool`   | True if `x` is `Int`          |
| `isFloat(x)`    | `(Any) → Bool`   | True if `x` is `Float`        |
| `isString(x)`   | `(Any) → Bool`   | True if `x` is `String`       |
| `isBool(x)`     | `(Any) → Bool`   | True if `x` is `Bool`         |

### Utilities

| Function           | Signature              | Description                                               |
|--------------------|------------------------|-----------------------------------------------------------|
| `exit(code?)`      | `(Int?) → null`        | Halt execution with the given exit code (default 0)       |
| `assert(cond, msg?)` | `(Any, String?) → null` | Halt with exit code 1 if `cond` is falsy; print `msg` if provided |

---

## Print and Printf

### `print`

Accepts one or more arguments. Prints each separated by a space, then appends a newline.

```ocl
print("hello");         /# hello\n #/
print(1, 2, 3);         /# 1 2 3\n #/
print(x);
```

### `printf`

Formatted output. The format string is separated from its arguments by a colon (`:`), though a comma also works.

```ocl
printf("Hello, %s!\n": name);
printf("x = %d, y = %f\n": x, y);
printf("%d + %d = %d\n": a, b, a + b);
```

**Format specifiers:**

| Specifier  | Description                        |
|------------|------------------------------------|
| `%d`, `%i` | Integer                            |
| `%f`       | Float (or integer promoted to float) |
| `%s`       | String (or any value via `toString`) |
| `%c`       | Character                          |
| `%b`       | Boolean (`true` or `false`)        |
| `%%`       | Literal `%`                        |

> **Escape sequences in `printf` format strings:** Because the lexer processes escape sequences when tokenising string literals, a `\n` in a `printf` format string is already a real newline character by the time it reaches the VM. The VM's runtime escape processing in `printf` is therefore only relevant for strings that arrive as values from variables — not from literals. This means `printf("line1\nline2\n")` works exactly as expected.

---

## Known Limitations and Edge Cases

### `strSplit` Does Not Return Tokens

`strSplit(s, delim)` returns an `Int` (the number of parts), not an array of strings.

### No Short-Circuit Assignment (`+=`, `-=`, etc.)

Compound assignment operators are not implemented. Use explicit form:

```ocl
x = x + 1;   /# not: x += 1 #/
```

### No First-Class Functions or Closures

Functions cannot be assigned to variables, passed as arguments, or returned from functions. All functions must be declared at the top level.

### No User-Defined Types (Structs)

There is no `struct`, `class`, or record type. Data must be modelled with parallel arrays or via other conventions.

### No Garbage Collection for Dynamic Strings

Strings produced at runtime by built-ins (`toUpperCase`, `strReplace`, `+` on strings, etc.) are heap-allocated and freed when the call frame they belong to is torn down. Strings in the constants table are freed when the bytecode is freed (at program exit). Long-running programs that produce many transient strings will accumulate memory, but it is all reclaimed on exit.

### Type Checker Is Advisory

The type checker reports errors but does not prevent code generation or execution. Passing `--no-typecheck` skips it entirely. The VM performs runtime type checks independently.

### Call Stack Depth

The VM supports a maximum call depth of 4096 frames (`VM_FRAMES_MAX`). Deep recursion beyond this will produce a "call stack overflow" runtime error.

### `value_to_string` Uses a Rotating Buffer Pool

The internal `value_to_string` function (used by `toString`, `print`, and `printf`) writes into a pool of 8 static buffers (8 KiB each), cycling round-robin. This means nested calls — for example, printing arrays whose elements are themselves arrays — each get a distinct slot, avoiding the corruption that a single shared buffer would cause. However, if call nesting exceeds 8 levels deep within a single expression, output may still be corrupted. For most programs this is not a concern.

### Escape sequences are lexer-resolved

`\n` and other escape sequences in string literals are converted to real characters when the source is tokenised. This is intentional but means there is no way to produce a string containing the literal two-character sequence `\n` from source code.