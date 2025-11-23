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
- Automatic mark-and-sweep garbage collection with configurable thresholds and manual `(gc)` / `(gc-threshold ...)` builtins for deterministic tuning.
- Buildable to native binary **and** WebAssembly.
- Abstract GC API (`gc.h`/`gc.c`) ready for custom implementations (mark‑sweep, reference counting, etc.).

---

## Building

### Prerequisites

- **Emscripten** (for WASM target). Follow the installation guide at https://emscripten.org/docs/getting_started/downloads.html.
- A C compiler (e.g., `gcc` or `clang`) for native builds.

### Compile to WebAssembly

```sh
# Activate Emscripten environment (adjust path as needed)
source /path/to/emsdk_env.sh

make
```

The `make` command produces `interpreter.wasm`.

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

The `(gc)` builtin forces an immediate mark-and-sweep cycle. Automatic collections trigger when total allocations exceed the current threshold, which you can inspect or set (in bytes) via `(gc-threshold)` or `(gc-threshold 2000000)`.

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

Open `http://localhost:8080/` to use the bundled `web/index.html` harness that loads `interpreter.js` and lets you run Minimalisp programs in the browser.

### Visualization Dashboard

Minimalisp includes a web-based dashboard to visualize Garbage Collection in real-time.

1.  **Build**:
    ```sh
    make
    ```
    This generates `interpreter.js` and `interpreter.wasm`.

2.  **Run Server**:
    ```sh
    python3 -m http.server
    ```

3.  **Open Browser**:
    Navigate to [http://localhost:8000](http://localhost:8000).

**Features**:
- **Interactive REPL**: Execute Lisp code directly in the browser. Supports multi-line input.
- **Real-time Monitoring**: Watch memory usage (allocated vs current) and GC cycles update live.
- **Stress Test**: Run an automated allocation loop to observe GC behavior under load.

---

## Garbage Collector

The GC interface (`gc.h` / `gc.c`) currently wraps `malloc`/`free`. To experiment with other algorithms, replace the implementation in `gc.c` with a real tracing or reference‑counting collector while keeping the same API.

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
