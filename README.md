# Minimalisp

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

**Minimalisp** is a tiny Lisp interpreter written in C, compiled to WebAssembly (WASM) via Emscripten. It provides a minimal core language and a pluggable garbage‑collector interface, making it ideal for experimenting with different GC algorithms.

---

## Table of Contents

- [Features](#features)
- [Building](#building)
- [Usage](#usage)
  - [Interactive REPL (native)](#interactive-repl-native)
  - [Command‑line (native)](#command-line-native)
  - [Script Files](#script-files)
  - [WebAssembly](#webassembly)
- [Garbage Collector](#garbage-collector)
- [Contributing](#contributing)
- [License](#license)

---

## Features

- Minimal Lisp syntax with numbers, symbols, quoting (`'`/`quote`), and flexible list literals via `cons`/`list`.
- Primitive list toolkit plus user‑defined procedures: `define`, `lambda`, `if`, and `begin` provide recursion and sequencing.
- Interactive REPL and script runner (`./interpreter -f file.lisp`) ready for experimentation; see `hanoi.lisp` for a Tower of Hanoi example.
- Automatic garbage collection with configurable thresholds and manual `(gc)` / `(gc-threshold ...)` builtins for deterministic tuning.
- Buildable to native binary **and** WebAssembly.
- Abstract GC API (`include/gc.h` + `src/gc/*`) with swappable backends (mark‑sweep by default, copying GC via `GC_BACKEND=copying`).

---

## Building

### Prerequisites

- **Emscripten** (for WASM target). Follow the installation guide at https://emscripten.org/docs/getting_started/downloads.html.
- A C compiler (e.g., `gcc` or `clang`) for native builds.

### Compile to WebAssembly

```sh
# Activate Emscripten environment (adjust path as needed)
source /path/to/emsdk_env.sh

make          # builds web/interpreter.js + .wasm
```

`make` uses a repo-local `.emscripten-cache/` (created automatically) so the build works even in sandboxed environments. Serve the `web/` directory with any static file server to exercise the browser REPL.

### Compile native binary (optional)

```sh
make native
# override compiler via NATIVE_CC=clang make native
```

### Native smoke tests

```sh
make test-native
```

Runs arithmetic, REPL-style, list, quoting, and script smoke tests to keep the evaluator healthy.

---

## Usage

### Interactive REPL (native)

```sh
./interpreter
ml> (define (double x) (+ x x))
double
ml> (double 5)
10
ml> (list 1 2 (cons 3 nil))
(1 2 (3))
ml> '(1 2 (+ 3 4))
(1 2 (+ 3 4))
```

`define`d procedures persist for the current session, `print` displays structured values, and `nil` is written as `()`. Multiline forms are supported—when the prompt changes to `...>`, keep typing until your parentheses balance. Press `Ctrl-D` (Unix) to exit.

### Command-line (native)

```sh
./interpreter "(begin (define (double x) (+ x x)) (double 8))"
```

`begin` lets you chain definitions with expressions inside a single invocation.

### Garbage Collection Controls

```sh
./interpreter "(gc)"
```

The `(gc)` builtin forces an immediate gaberge collection cycle. Automatic collections trigger when total allocations exceed the current threshold, which you can inspect or set (in bytes) via `(gc-threshold)` or `(gc-threshold 2000000)`.

### Selecting a GC backend

```sh
GC_BACKEND=copying ./interpreter "(print 'hello)"
GC_BACKEND=copying make test-native
```

Set `GC_BACKEND=mark-sweep` to use the default mark-and-sweep collector, `GC_BACKEND=copying` to run the semispace copying collector, or `GC_BACKEND=generational` to try the nursery (copying) + old-generation (mark-sweep) hybrid. Leaving the variable unset (or `mark-sweep`) falls back to the classic mark-and-sweep backend. Copying/Generational collectors use fixed semispace sizes; tweak the constants in `src/gc/copying.c` / `src/gc/generational.c` if you need more headroom.

### Script Files

```sh
./interpreter -f hanoi.lisp
(A C)
(A B)
(C B)
(A C)
(B A)
(B C)
(A C)
Result: ()
```

Use the `-f` flag to evaluate any `.lisp` file; the bundled `hanoi.lisp` prints the sequence of moves for a 3-disk Tower of Hanoi run.

### WebAssembly

```sh
make            # produces web/interpreter.js + .wasm
python3 -m http.server 8080 --directory web
```

Open `http://localhost:8080/` to use the bundled `web/index.html` harness that loads `interpreter.js`, lets you pick a GC backend (mark-sweep / copying / generational), run programs, and inspect the heap. The Canvas view color-codes objects (numbers, cons cells, lambdas, bindings, etc.) so you can watch mark-sweep fragmentation or copying/Generational compaction in real time. Use “Auto snapshot” to refresh the view while your program allocates.

---

## Garbage Collector

Minimalisp ships with multiple pluggable tracing collectors that all share the same API (`include/gc.h` + `src/gc/*`):

- **mark-sweep** (default): a simple stop-the-world collector that manages a single heap via a doubly-linked free list.
- **copying**: semi-space collector used for the WASM visualization demos; shows compaction behaviour very clearly.
- **generational**: combines a copying nursery with an old-generation mark-sweep heap plus remembered sets for write barriers.

Select a backend at runtime with `GC_BACKEND=mark-sweep|copying|generational make test-native` or via the dropdown in `web/index.html`. Every backend supports tagging (`gc_set_tag`) and heap snapshots (`gc_heap_snapshot`), which feed the Canvas visualizer so you can see fragmentation vs. compaction in real time. New algorithms belong under `src/gc/` and only need to implement the `GcBackend` vtable to plug into the rest of the interpreter.

---

## Contributing

Contributions are welcome! Feel free to:

- Add new language features.
- Implement alternative GC strategies.
- Improve the build system or documentation.

Please open an issue or submit a pull request on the GitHub repository.

---

## License

MIT License. See the `LICENSE` file for details.
