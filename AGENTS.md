# Repository Guidelines

## Project Structure & Module Organization
`interpreter.c` hosts the lexer, parser, and evaluator for the Lisp core, while `gc.c`/`gc.h` expose the pluggable garbage-collector interface (currently a malloc-backed stub). The `Makefile` compiles both sources into `interpreter.wasm` via Emscripten; native builds create an `interpreter` binary alongside the sources. High-level usage notes live in `README.md`, and licensing details are under `LICENSE`. Generated artifacts stay in the repository root; add subdirectories only when a feature truly needs them.

## Build, Test & Development Commands
- `source /path/to/emsdk_env.sh && make` — builds `interpreter.wasm` with the exported `eval` entry point.
- `make clean` — deletes WebAssembly outputs before a fresh build.
- `gcc -o interpreter interpreter.c gc.c -lm` — produces a native binary for fast REPL-style testing.
- `./interpreter "(print (+ 1 2 3))"` — smoke-tests the parser, evaluator, and GC initialization path.

## Coding Style & Naming Conventions
Write ISO C (C11-compatible) with four-space indentation, no tabs, and compact file-scoped `static` helpers as used throughout `interpreter.c`. Functions and globals use `snake_case`, reserving the `gc_` prefix for allocator hooks and keeping exported entry points (`eval`) lowercase. Header guards follow the `FILE_H` pattern (`GC_H`). Add short intent-focused comments only where control flow is non-trivial (tokenization, allocation boundaries).

## Testing Guidelines
Automated tests are not yet wired up; rely on deterministic command-line runs. Exercise representative expressions (arithmetic depth, nested parentheses, `print`) via the native binary before pushing. When touching the GC, stress allocation paths by running long inputs or loops, and watch for crashes or leaks (e.g., under `valgrind`). Treat the WebAssembly build as a second validation by loading `interpreter.wasm` in Node.js and ensuring the exported `eval` initializes correctly.

## Commit & Pull Request Guidelines
History currently uses short, imperative commits (e.g., “Initial commit”); continue that style with present-tense summaries under ~60 characters. Each pull request should explain the motivation, list testing evidence (commands run, expressions evaluated), and call out GC or build-system side effects reviewers must note. Reference related issues, attach screenshots only when demonstrating console output changes, and keep branches rebased so the slim history stays linear.

## Garbage-Collector Extension Tips
New GC algorithms should keep the `gc_init`, `gc_allocate`, `gc_collect`, and `gc_free` signatures untouched so the interpreter never needs conditional code. Use `gc.h` to expose optional diagnostics behind `#ifdef` blocks, and document new tuning knobs directly in `gc.c` so future agents inherit the same expectations.
