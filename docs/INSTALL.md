# Installing OCL

## Requirements

- A C11 compiler such as GCC or Clang
- A Rust toolchain with `rustc`
- `make`
- `cmake`, for the native cross platform build path
- Git, if you want to clone the repository

## Get the Source

Clone:

```bash
git clone https://github.com/NumSlower/OCL.git
cd OCL
```

Or download a source archive and extract it.

## Build

Build the default debug binary:

```bash
make
```

Build with the native cross platform generator:

```bash
cmake -S . -B out/build
cmake --build out/build --config Release
```

Other useful targets:

```bash
make debug
make release
make valgrind
make clean
make help
```

Build output:

- Unix like hosts, `build/ocl`
- Windows, `build/ocl.exe`
- CMake builds place the executable under your chosen build directory
- Host builds compile the VM core from `src/vm/vm.rs`

## Run Source Files

```bash
./build/ocl Testfiles/main.ocl
./build/ocl --time Testfiles/showcase.ocl
./build/ocl --dump-tokens Testfiles/main.ocl
./build/ocl --dump-bytecode Testfiles/main.ocl
./build/ocl --no-typecheck Testfiles/main.ocl
```

## Build Executables

Build an OCL executable:

```bash
./build/ocl -e hello Testfiles/main.ocl
```

Run a compiled executable:

```bash
./build/ocl -r hello.elf
```

Build a NumOS ELF executable:

```bash
./build/ocl --emit-elf build/main.elf Testfiles/main.ocl
```

You can also use the Makefile helper:

```bash
make elf
make elf FILE=Testfiles/main.ocl OUT=build/main.elf
```

The NumOS ELF path still uses the legacy C VM snapshot in `src/vm/vm_legacy.c`.

## Tests

The Makefile includes:

```bash
make test
```

In this workspace, the `tests/fixtures/programs` directory is missing. The current sample programs are under `Testfiles/`.

## Troubleshooting

`make` not found:

- Install your platform build tools.
- Ubuntu or Debian, `sudo apt install build-essential`
- macOS, `xcode-select --install`
- Windows, install Visual Studio Build Tools or MSYS2 with a C compiler

C compiler errors:

- Check `gcc --version` or `clang --version`
- Use a compiler with C11 support

Rust toolchain errors:

- Check `rustc --version`
- Install Rust before running `make` or the CMake build

Runtime path errors:

- Run commands from the project root
- Confirm the input file path exists
