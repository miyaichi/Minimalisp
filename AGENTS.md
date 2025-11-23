# Repository Guidelines

## Project Structure & Module Organization
The code now lives under `src/` with headers in `include/`. `src/interpreter.c` owns the lexer, parser, evaluator, and CLI/REPL, while GC implementations sit in `src/gc/` (e.g., `mark_sweep.c`) behind the `gc_backend` interface and `gc_runtime` shim. WebAssembly artifacts and the browser harness live under `web/`. `README.md` describes usage/build steps and `hanoi.lisp` under the repo root shows a non-trivial program.

## Build, Test & Development Commands
- `source /path/to/emsdk_env.sh && make` — emits `web/interpreter.js`/`.wasm` using a repo-local `.emscripten-cache` so CI/sandboxes work.
- `make native` — builds the native CLI/REPL to `./interpreter` (override compiler via `NATIVE_CC=clang make native`).
- `make test-native` — runs the deterministic smoke suite (arithmetic, list ops, quoting, scripts, GC, multi-line REPL).
- `./interpreter "(print (+ 1 2 3))"` or `./interpreter -f hanoi.lisp` — spot-checks for manual debugging.

## Coding Style & Naming Conventions
Write ISO C (C11-compatible) with four-space indentation, no tabs, and compact file-scoped `static` helpers as used throughout `interpreter.c`. Functions and globals use `snake_case`, reserving the `gc_` prefix for allocator hooks and keeping exported entry points (`eval`) lowercase. Header guards follow the `FILE_H` pattern (`GC_H`). Add short intent-focused comments only where control flow is non-trivial (tokenization, allocation boundaries).

## Testing Guidelines
Use `make test-native` before pushing—it exercises arithmetic, list ops, quoting, GC builtins, script loading, and multi-line REPL input. When touching GC internals, craft stress cases (long recursive loops, large lists) and run under tools like `valgrind` or `asan`. Rebuild the WASM target via `make` and load `web/index.html` through a static server to ensure the browser harness still works.

## Commit & Pull Request Guidelines
History currently uses short, imperative commits (e.g., “Initial commit”); continue that style with present-tense summaries under ~60 characters. Each pull request should explain the motivation, list testing evidence (commands run, expressions evaluated), and call out GC or build-system side effects reviewers must note. Reference related issues, attach screenshots only when demonstrating console output changes, and keep branches rebased so the slim history stays linear.

## Garbage-Collector Extension Tips
Each GC backend implements the `GcBackend` vtable (see `src/gc/gc_backend.h`) and plugs into `gc_runtime`. New algorithms belong in their own file under `src/gc/` and should expose a factory (e.g., `gc_mark_sweep_backend`). Keep interpreter-facing APIs stable by routing everything through `gc_runtime`. Document backend-specific knobs/comments inside the backend file and update `README.md`/`AGENTS.md` when adding user-visible controls.
