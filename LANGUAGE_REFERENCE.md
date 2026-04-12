# OCL Language Reference

Version: beta 0.6.0

This file matches `docs/reference/LANGUAGE_REFERENCE.md`.

## File Format

OCL source files use the `.ocl` extension.

Run source files:

```bash
./build/ocl path/to/program.ocl
./build/ocl path/to/program.ocl -- arg1 arg2
./build/ocl --time path/to/program.ocl
./build/ocl --dump-tokens path/to/program.ocl
./build/ocl --dump-bytecode path/to/program.ocl
./build/ocl --no-typecheck path/to/program.ocl
```

Compile and run an OCL executable:

```bash
./build/ocl -e hello path/to/program.ocl
./build/ocl -r hello.elf
./build/ocl -r hello.elf -- arg1 arg2
```

Build a NumOS ELF executable:

```bash
./build/ocl --emit-elf path/to/program.elf path/to/program.ocl
```

If a top level `main()` function exists, the VM calls it after global initialization.

## Comments

OCL supports block comments.

```ocl
/# one line #/

/#
multi line
#/
```

Single line `//` comments are not part of the language.

## Types

Built in types:

| Type | Accepted names | Notes |
| --- | --- | --- |
| `Int` | `Int`, `int`, `int32`, `int64` | Runtime integers are 64 bit |
| `Float` | `Float`, `float` | Double precision |
| `String` | `String`, `string` | Heap allocated string |
| `Bool` | `Bool`, `bool` | `true` or `false` |
| `Char` | `Char`, `char` | Single byte character |
| `Array` | `Array`, `array` | Dynamic array |
| `void` | `void`, `Void` | Function return type |

User defined struct types are also supported:

```ocl
struct User {
    name:String;
    age:Int;
}
```

## Variables

OCL style declarations:

```ocl
Let x:Int = 42;
Let y:String;
```

C style declarations:

```ocl
int counter = 0;
String label = "ok";
```

Forward declaration:

```ocl
declare total:Int;
```

Rules:

- Variables are lexically scoped.
- Re declaring a name in the same scope is a type error.
- A declaration without an initializer starts as `null`.

## Literals

```ocl
42
3.14
"hello"
'A'
true
false
[1, 2, 3]
```

Struct literal syntax:

```ocl
User{ name: "Ada", age: 30 }
```

Escape sequences in string and char literals are resolved during lexing.

Supported escapes:

- `\n`
- `\t`
- `\r`
- `\\`
- `\"`
- `\'`
- `\0`

Write `\\n` in source if you need the two characters `\` and `n` in the resulting string.

## Operators

Arithmetic:

- `+`
- `-`
- `*`
- `/`
- `%`

Comparison:

- `==`
- `!=`
- `<`
- `<=`
- `>`
- `>=`

Logical:

- `&&`
- `||`
- `!`

Conditional:

- `?:`

Bitwise:

- `&`
- `|`
- `^`
- `~`
- `<<`
- `>>`

Assignment:

- `=`
- `+=`
- `-=`
- `*=`
- `/=`
- `%=`
- `&=`
- `|=`
- `^=`
- `<<=`
- `>>=`

Increment and decrement:

- `++`
- `--`

Notes:

- `Int` bitwise operations use 64 bit two's complement values.
- `&&` and `||` short circuit.
- `>>` is an arithmetic right shift on `Int`.
- Use `bitLogicalShiftRight(x, n)` when you need zero fill right shift behavior.
- Postfix `x++` and `x--` are parsed, but they behave like rewritten assignment expressions, not C style value returning postfix operators.
- `condition ? whenTrue : whenFalse` is right associative.

Precedence, high to low:

1. Unary, `-`, `!`, `~`, prefix `++`, prefix `--`
2. Multiplication, division, modulo
3. Addition, subtraction
4. Shift, `<<`, `>>`
5. Comparison
6. Equality
7. Bitwise AND
8. Bitwise XOR
9. Bitwise OR
10. Logical AND
11. Logical OR
12. Ternary conditional, `?:`
13. Assignment and compound assignment

## Control Flow

If:

```ocl
if (x > 0) {
    print("positive");
} else if (x < 0) {
    print("negative");
} else {
    print("zero");
}
```

While:

```ocl
while (count < 10) {
    count++;
}
```

For:

```ocl
for (int i = 0; i < 10; i++) {
    print(i);
}

for (Let i:Int = 0; i < 10; i = i + 1) {
    print(i);
}
```

Loop control:

```ocl
break;
continue;
```

Notes:

- Braces are required for blocks.
- `for` loop declarations in the initializer must include an initializer value.
- There is no `do while` loop in the current parser.

## Functions

Declarations:

```ocl
func greet() {
    print("hello");
}

func int add(a:int, b:int) {
    return a + b;
}
```

Rules:

- `func` starts a function declaration.
- The return type, if present, comes before the function name.
- Parameters use `name:Type`.
- Functions are top level only.
- Function declarations are registered before code generation, so functions can call later functions in the same file.

## Arrays

Create arrays:

```ocl
Let nums:Array = [1, 2, 3];
Let empty:Array = [];
```

Index arrays and strings:

```ocl
print(nums[0]);
print("hello"[1]);
```

Assign array elements:

```ocl
nums[0] = 99;
```

Chain index access:

```ocl
Let matrix:Array = [[1, 2], [3, 4]];
print(matrix[0][1]);
```

## Structs

Declare a struct:

```ocl
struct Point {
    x:Int;
    y:Int;
}
```

Create and use a struct:

```ocl
Let p:Point = Point{ x: 3, y: 4 };
print(p.x);
p.y = 9;
```

Notes:

- Field names are checked by the type checker.
- Struct equality in the VM is identity based.

## Import

Import syntax:

```ocl
Import <Core>
Import <mylib>
```

Search order:

1. Same directory as the importing file
2. Runtime roots from `OCL_PROJECT_ROOT`, or the interpreter executable directory and its parent roots
3. Current working directory paths

Runtime root paths are checked in this order:

- `ocl_headers/`
- `stdlib_headers/`
- The runtime root itself

Extensions tried:

- Exact name
- `.ocl`

The parser reads and merges imported top level declarations into the current program.

The stable terminal module entry point is:

```ocl
Import <terminal>
```

## Built In Functions

I/O:

- `print(...)`
- `printf(...)`
- `input(prompt?)`
- `readLine()`

Math:

- `abs(x)`
- `sqrt(x)`
- `pow(x, y)`
- `sin(x)`
- `cos(x)`
- `tan(x)`
- `floor(x)`
- `ceil(x)`
- `round(x)`
- `max(a, b)`
- `min(a, b)`

Strings:

- `strLen(s)`
- `substr(s, start, length?)`
- `toUpperCase(s)`
- `toLowerCase(s)`
- `strContains(s, needle)`
- `strIndexOf(s, needle)`
- `strReplace(s, old, new)`
- `strTrim(s)`
- `strSplit(s, delim)`

Conversion and inspection:

- `toInt(x)`
- `toFloat(x)`
- `toString(x)`
- `toBool(x)`
- `typeOf(x)`
- `isNull(x)`
- `isInt(x)`
- `isFloat(x)`
- `isString(x)`
- `isBool(x)`

Utilities:

- `exit(code?)`
- `assert(cond, msg?)`
- `timeNow()`
- `random()`
- `random(n)`
- `random(lo, hi)`

Bitwise:

- `bitLogicalShiftRight(x, n)`
- `bitRotateLeft(x, n)`
- `bitRotateRight(x, n)`
- `bitPopcount(x)`
- `bitCountLeadingZeros(x)`
- `bitCountTrailingZeros(x)`
- `bitTest(x, index)`
- `bitSet(x, index)`
- `bitClear(x, index)`
- `bitToggle(x, index)`
- `bitNand(a, b)`
- `bitNor(a, b)`
- `bitXnor(a, b)`

Host tooling:

- `listFiles(path)`
- `measureFile(path)`

Standard module, `Import <terminal>`:

- `args()`
- `argCount()`
- `arg(index)`
- `run(program, processArgs)`
- `capture(program, processArgs)`
- `runShell(command)`
- `captureShell(command)`
- `status(result)`
- `output(result)`
- `os()`

Arrays:

- `arrayNew(size?)`
- `arrayPush(a, v)`
- `arrayPop(a)`
- `arrayGet(a, i)`
- `arraySet(a, i, v)`
- `arrayLen(a)`

`listFiles(path)` walks a directory recursively and returns an array of file paths.

`measureFile(path)` compiles and runs another OCL source file in quiet mode and returns elapsed milliseconds as a float. If the target file fails to parse, type check, or run, it returns `null`.

For `Import <terminal>`:

- `args()[0]` is the first user supplied value after `--`.
- `capture` and `captureShell` return `[exitCode, output]`.
- `capture` and `run` are the portable first choice.
- Shell helpers are convenience wrappers around the host shell.

Examples:

```bash
./build/ocl Testfiles/echo.ocl -- "Hello world!"
./build/ocl -e echo Testfiles/echo.ocl
./build/ocl -r echo.elf -- "Hello world!"
```

`print` writes a newline. `printf` does not.

Bitwise built ins operate on 64 bit `Int` values. `bitRotateLeft` and `bitRotateRight` wrap the shift count modulo 64. Index based helpers require an index from `0` to `63`.

The parser accepts both forms below for `print` and `printf` calls:

```ocl
printf("value=%d", x);
printf("value=%d": x);
```

## Known Limits

- Functions are not first class values.
- Nested functions are not supported.
- The VM call stack limit is 4096 frames.
- The type checker is skipped only when you pass `--no-typecheck`.
