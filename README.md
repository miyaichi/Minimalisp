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
- [GC Demo Programs](#gc-demo-programs)
  - [WebAssembly](#webassembly)
- [Garbage Collector](#garbage-collector)
- [Contributing](#contributing)
- [License](#license)

---

## Features

- Minimal Lisp syntax with numbers, symbols, quoting (`'`/`quote`), and flexible list literals via `cons`/`list`.
- Primitive list toolkit plus user‑defined procedures: `define`, `lambda`, `if`, and `begin` provide recursion and sequencing.
- Shared Lisp standard library (`standard-lib.lisp`) loaded at startup in both native and WASM builds so helpers such as `append`, `map`, `foldl`, and predicates live in Lisp space.
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

`make` uses a repo-local `.emscripten-cache/` (created automatically) so the build works even in sandboxed environments. Serve the `web/` directory with any static file server to exercise the browser REPL. The resulting `.data` bundle includes `standard-lib.lisp`, so rerun `make` after editing that file to update the browser build.

### Compile native binary (optional)

```sh
make native
# override compiler via NATIVE_CC=clang make native
```

The native binary reads `standard-lib.lisp` from disk on each launch, so you can tweak the library without rebuilding.

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

### Standard Library

Every invocation loads `standard-lib.lisp` before user code. The file defines pure-Lisp helpers such as `append`, `reverse`, `map`, `foldl`/`foldr`, 
`filter`, `range`, and predicates like `null?`, `not`, `any`, and `all`.

**Native builds**: The interpreter reads `standard-lib.lisp` from disk on each startup, so you can modify it without rebuilding.

**WASM builds**: The library is embedded via `--embed-file`, so you must run `make` after editing `standard-lib.lisp` to update the browser-side runtime.

**Troubleshooting**: If you see "Warning: standard-lib.lisp not found", the interpreter will continue but standard functions will be unavailable. Ensure the file exists in the current directory or at `/standard-lib.lisp` for WASM.

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

The interpreter also exposes a `(load filename)` builtin (pass the filename as a symbol, e.g., `(load 'gc-demo-programs.lisp)`), which evaluates another file at runtime without restarting the REPL.

### GC Demo Programs

`gc-demo-programs.lisp` bundles several GC-focused workloads:
- `demo-wave`: allocates lists and triggers `(gc)` to visualize allocation waves.
- `demo-fragment`: builds varying-length ranges to fragment the heap.
- `demo-generational`: mixes short-lived allocations with long-lived survivors.
- `demo-tree`: constructs binary trees to stress recursive graph allocation.

Load it via `./interpreter -f gc-demo-programs.lisp` (native) or paste the definitions into the WASM REPL, then invoke each demo to watch the heap canvas respond under different backends.

### WebAssembly

```sh
make            # produces web/interpreter.js + .wasm
python3 -m http.server 8080 --directory web
```

Open `http://localhost:8080/` to use the bundled `web/index.html` harness that loads `interpreter.js`, lets you pick a GC backend (mark-sweep / copying / generational), run programs, and inspect the heap. The Canvas view color-codes objects (numbers, cons cells, lambdas, bindings, etc.) so you can watch mark-sweep fragmentation or copying/Generational compaction in real time. Use “Auto snapshot” to refresh the view while your program allocates.

A hosted build is also available via GitHub Pages at `https://miyaichi.github.io/Minimalisp/landing.html`, which links to the live “Minimalisp WASM Playground” (`index.html`). The landing page describes the project and the playground serves the interactive REPL + heap viz directly in your browser.

---

## Garbage Collector

Minimalisp ships with multiple pluggable tracing collectors that all share the same API (`include/gc.h` + `src/gc/*`):

- **mark-sweep** (default): a simple stop-the-world collector that manages a single heap via a doubly-linked free list.
- **copying**: semi-space collector used for the WASM visualization demos; shows compaction behaviour very clearly.
- **generational**: combines a copying nursery with an old-generation mark-sweep heap plus remembered sets for write barriers.

Select a backend at runtime with `GC_BACKEND=mark-sweep|copying|generational make test-native` or via the dropdown in `web/index.html`. Every backend supports tagging (`gc_set_tag`) and heap snapshots (`gc_heap_snapshot`), which feed the Canvas visualizer so you can see fragmentation vs. compaction in real time. New algorithms belong under `src/gc/` and only need to implement the `GcBackend` vtable to plug into the rest of the interpreter.

### GC Performance Benchmarks

A comprehensive benchmark suite is available to analyze and compare GC performance:

```sh
# Run all benchmarks across all GC backends
./scripts/run-gc-benchmarks.sh

# Analyze results and generate comparison report
python3 scripts/analyze-results.py results

# View detailed performance report
cat docs/gc-performance-report.md
```

The benchmark suite includes 5 different workload patterns:
- **alloc-intensive**: Tests throughput under heavy allocation pressure
- **mixed-lifetime**: Tests handling of short-lived and long-lived objects
- **pointer-dense**: Tests performance with deep object graphs (binary trees)
- **fragmentation**: Tests memory fragmentation with varied allocation sizes
- **real-world**: Simulates realistic mixed workloads

Results are saved to `results/` directory and can be analyzed with the included Python script. The comprehensive performance report (`docs/gc-performance-report.md`) provides detailed analysis, performance characteristics, and GC selection guidelines.

**Key Finding**: Copying GC demonstrates **12,611x faster** performance than Mark-Sweep on allocation-intensive workloads, with sub-millisecond pause times (0.4ms) compared to multi-second pauses (3.4s) in Mark-Sweep.

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
