# Installing OCL

> **Note:** This is a beta release. You may encounter bugs or incomplete features. Please open an issue on [GitHub](https://github.com/NumSlower/OCL/issues) if you run into any problems.

## Prerequisites

- A C11-compatible compiler (GCC or Clang)
- `make`
- Git (optional, for cloning)

## Getting the Source

**Clone the repository:**

```bash
git clone https://github.com/NumSlower/OCL.git
cd OCL
```

**Or download the beta release archive** from the [Releases page](https://github.com/NumSlower/OCL/releases) and extract it:

```bash
tar -xzf OCL-beta.tar.gz
cd OCL
```

## Building

Run `make` in the root of the project:

```bash
make
```

This will compile the interpreter and produce the `ocl` executable.

### Freestanding build (no host C library)

OCL also ships its own C runtime, so it can be built without the host libc,
libm or C runtime startup files:

```bash
make freestanding
```

This produces `./ocl-freestanding`, compiled with
`-ffreestanding -fno-builtin -nostdinc -nostdlib` and linked against
`src/libc/`. Verify the result:

```bash
nm -u ocl-freestanding     # must print nothing
make check-libc            # runtime self-tests, also linked with -nostdlib
make test-freestanding     # full .ocl suite on the freestanding binary
```

Only `src/libc/port_linux.c` (raw syscalls) is host-specific; porting to a new
platform means reimplementing the primitives declared in `src/libc/port.h`.

## Running OCL

Once built, you can run a `.ocl` source file with:

```bash
./ocl yourfile.ocl
```

## Running Tests

```bash
make test
```

## Cleaning Up

To remove compiled object files and the executable:

```bash
make clean
```

## Troubleshooting

**`make` not found:** Install it via your package manager. On Ubuntu/Debian: `sudo apt install build-essential`. On macOS: `xcode-select --install`.

**Compiler errors:** Make sure your compiler supports C11 (`gcc --version` or `clang --version`). GCC 5+ and Clang 3.3+ both support C11.

If you're still stuck, check the [Issues page](https://github.com/NumSlower/OCL/issues) or open a new issue with the full error output.