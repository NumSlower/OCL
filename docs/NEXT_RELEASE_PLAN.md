# Next Release Plan

## Goal

This release should focus on features with the highest day to day value.
The target is simple.
Make OCL easier to write, safer to run, and faster to test.

## Priority 1, Core language and safety

### Error handling

Add a `Result` type, `try/catch`, or both.
This is the biggest gap for robust programs.
Without a standard error path, file access, process execution, and parsing code stay fragile.

### `switch` or `match`

Add a branch form built for many cases.
Long `if` and `else if` chains get noisy fast.
This change improves command dispatch, token handling, and state driven code.

### Enums

Add enum declarations and enum values.
The type checker already tracks struct names.
That makes enums a natural extension of work the compiler already does.

## Priority 2, Standard library basics

### File I O

Add `readFile`, `writeFile`, and `appendFile`.
This opens a large class of programs.
Scripts gain access to config files, logs, generated output, and data transforms.

### HashMap or Dict

Add a built in key value type.
Today, users fake maps with parallel arrays or custom structs.
That raises code size and bug risk for common lookup tasks.

### Array transforms

Add `arrayMap`, `arrayFilter`, and `arrayReduce`.
The current array helpers cover storage.
They do not cover the most common data flow operations.

### Better date and time formatting

Extend time support beyond the raw `timeNow()` millisecond value.
Programs need readable timestamps for logs, reports, and file names.

### Sort with a comparator

Add a sort builtin that accepts a comparator function.
The current sort in `Math.ocl` is a bubble sort with no comparator.
That blocks custom ordering and does not scale well.

## Priority 3, Function ergonomics

### Default parameter values

Add default values for function parameters.
This removes repeated wrapper functions and reduces call site noise.

### Array destructuring

Add multi assignment from arrays, such as `Let [a, b] = someArray`.
This helps with tuple like returns and simple unpacking.

## Priority 4, Tooling

### REPL

Support `./build/ocl` with no arguments as a REPL.
This lowers the barrier for experiments, quick tests, and teaching.

### `--check`

Add a flag that runs the type checker without execution.
This is useful in CI and for editor integrations.

### Formatter

Add a formatter or a `--fmt` flag.
Consistent formatting improves code review speed and sample quality.

## Priority 5, VM and codegen work

### Constant folding

Fold constant expressions during codegen.
Examples include `2 * 3` and `"hello" + " world"`.
This cuts runtime work with low user facing complexity.

### Tail call optimization

Optimize tail calls.
The current hard limit is 4096 stack frames.
That hurts recursive programs even when the recursive form is otherwise safe.

## Suggested release order

1. Error handling, file I O, HashMap or Dict, and `--check`
2. `switch` or `match`, enums, default parameters, and array destructuring
3. Array transforms, date and time formatting, and comparator based sort
4. REPL and formatter
5. Constant folding and tail call optimization

## What this release would change

- Programs gain real file access and structured error paths
- Application code gets cleaner control flow and data modeling
- The standard library covers core collection and time tasks
- Tooling gets faster feedback loops for local work and CI
- The VM removes avoidable overhead in obvious cases
