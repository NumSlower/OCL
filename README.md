# OCL Interpreter

OCL is a bytecode compiled scripting language with a C11 front end and a Rust VM execution engine.

The toolchain reads `.ocl` source code through five stages:

1. Lexing
2. Parsing
3. Type checking
4. Bytecode generation
5. VM execution

The current build also supports:

- Built in standard library functions
- Arrays
- Struct declarations and struct values
- Imports
- Bytecode executable output
- NumOS ELF output

## Build

Requirements:

- A C11 compiler such as GCC or Clang
- A Rust toolchain with `rustc`
- `make`
- `cmake`, for the native cross platform build path

Common targets:

```bash
make
make debug
make release
make valgrind
make clean
make help
```

Native CMake build:

```bash
cmake -S . -B out/build
cmake --build out/build --config Release
```

Notes:

- `make` builds a debug binary.
- Host builds compile the VM core from `src/vm/vm.rs`.
- Sanitizers are enabled on Unix like hosts and disabled on Windows.
- The main output is `build/ocl` or `build/ocl.exe`.
- `cmake` works on Windows, macOS, and Linux or Unix hosts.

## Run

Run source code:

```bash
./build/ocl path/to/program.ocl
./build/ocl path/to/program.ocl -- arg1 arg2
./build/ocl --time path/to/program.ocl
./build/ocl --dump-tokens path/to/program.ocl
./build/ocl --dump-bytecode path/to/program.ocl
./build/ocl --check path/to/program.ocl
./build/ocl --no-typecheck path/to/program.ocl
```

Compile to an OCL executable and run it later:

```bash
./build/ocl -e hello path/to/program.ocl
./build/ocl -r hello.elf
./build/ocl -r hello.elf -- arg1 arg2
./build/ocl --dump-bytecode hello.elf
```

Build a NumOS ELF file:

```bash
./build/ocl --emit-elf path/to/program.elf path/to/program.ocl
make elf
make elf FILE=path/to/program.ocl OUT=path/to/program.elf
```

Notes:

- `-e NAME` writes `NAME.elf` in the current directory.
- `-r FILE` runs a compiled OCL executable.
- `--emit-elf PATH` builds a NumOS ELF executable from source.
- `--emit-elf` still embeds the legacy C VM snapshot from `src/vm/vm_legacy.c`.
- `--` starts the user argument list for `Import <terminal>`.
- `--dump-tokens` is only valid for source input.

## Examples

Sample `.ocl` programs in this workspace live under `Testfiles/`.

Examples:

- `Testfiles/showcase.ocl`
- `Testfiles/showcase_v1_core.ocl`
- `Testfiles/showcase_v2_stdlib.ocl`
- `Testfiles/showcase_v3_terminal.ocl`
- `Testfiles/showcase_v4_interactive.ocl`
- `Testfiles/showcase_v5_exit.ocl`

Argument example:

```bash
./build/ocl Testfiles/showcase.ocl -- alpha beta
./build/ocl Testfiles/showcase_v3_terminal.ocl -- alpha beta
./build/ocl Testfiles/showcase_v4_interactive.ocl
```

Bootstrap example:

```ocl
Import <bootstrap>

func Int main() {
    return bootstrap("./build/ocl", "compiler.ocl", "compiler-next", []);
}
```

## Language Summary

Declarations:

```ocl
Let count:Int = 42;
String name = "OCL";
declare later:Int;
```

Comments:

```ocl
/# block comment #/
```

Functions:

```ocl
func int add(a:int, b:int) {
    return a + b;
}
```

Arrays:

```ocl
Let nums:Array = [1, 2, 3];
nums[0] = 99;
print(arrayLen(nums));
```

Structs:

```ocl
struct User {
    name:String;
    age:Int;
}

Let user:User = User{ name: "Ada", age: 30 };
print(user.name);
```

For the full reference, see `docs/reference/LANGUAGE_REFERENCE.md`.
For the next milestone scope, see `docs/NEXT_RELEASE_PLAN.md`.

## Built In Functions

Current built in areas:

- I/O, `print`, `printf`, `input`, `readLine`, `readFile`, `writeFile`, `appendFile`
- Math, `abs`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `floor`, `ceil`, `round`, `max`, `min`
- Strings, `strLen`, `substr`, `toUpperCase`, `toLowerCase`, `strContains`, `strIndexOf`, `strReplace`, `strTrim`, `strSplit`
- Conversion and inspection, `toInt`, `toFloat`, `toString`, `toBool`, `typeOf`, `isNull`, `isInt`, `isFloat`, `isString`, `isBool`
- Bitwise, `bitLogicalShiftRight`, `bitRotateLeft`, `bitRotateRight`, `bitPopcount`, `bitCountLeadingZeros`, `bitCountTrailingZeros`, `bitTest`, `bitSet`, `bitClear`, `bitToggle`, `bitNand`, `bitNor`, `bitXnor`
- Utilities, `exit`, `assert`, `timeNow`, `random`
- Arrays, `arrayNew`, `arrayPush`, `arrayPop`, `arrayGet`, `arraySet`, `arrayLen`

## Standard Modules

Import the stable terminal API with:

```ocl
Import <terminal>
```

Public functions:

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

Notes:

- `args()[0]` is the first value after `--`.
- `capture` and `captureShell` return `[exitCode, output]`.
- `capture` and `run` are the portable first choice.
- Shell helpers are convenience wrappers around the host shell.

Import the bootstrap helpers with:

```ocl
Import <bootstrap>
```

Public functions:

- `compiledExtension()`
- `compiledArtifact(name)`
- `check(executable, source)`
- `captureCheck(executable, source)`
- `dumpTokens(executable, source)`
- `dumpBytecode(executable, source)`
- `build(executable, source, outputName)`
- `captureBuild(executable, source, outputName)`
- `emitElf(executable, source, outputPath)`
- `runSource(executable, source, programArgs)`
- `captureSource(executable, source, programArgs)`
- `runCompiled(compiled, programArgs)`
- `captureCompiled(compiled, programArgs)`
- `bootstrap(executable, source, outputName, programArgs)`
- `captureBootstrap(executable, source, outputName, programArgs)`

Notes:

- The bootstrap module lives in `stdlib_headers/bootstrap.ocl`.
- It uses `Import <terminal>` plus the existing array, string, and file helpers.
- `compiledArtifact(name)` adds `.exe` on Windows and `.elf` on other hosts when needed.
- `bootstrap(...)` runs `-e`, then launches the compiled artifact directly.

## Project Layout

```text
.
|-- README.md
|-- LANGUAGE_REFERENCE.md
|-- Makefile
|-- docs/
|   |-- INSTALL.md
|   |-- NEXT_RELEASE_PLAN.md
|   `-- reference/
|       `-- LANGUAGE_REFERENCE.md
|-- include/
|-- src/
|   |-- frontend/
|   |-- interpreter/
|   |-- stdlib/
|   `-- vm/
|       |-- vm.c
|       |-- vm.rs
|       `-- vm_legacy.c
`-- Testfiles/
```

## Limits

Current limits and behavior:

- Functions are not first class values.
- Nested functions are not supported.
- The call stack limit is 4096 frames.
- The type checker is enforced by default, but `--no-typecheck` skips it.
- Escape sequences in string literals are resolved by the lexer. Write `\\n` in source if you need the two characters `\` and `n`.
- The Makefile includes a `test` target, but this workspace does not contain the `tests/fixtures/programs` directory that target expects.
