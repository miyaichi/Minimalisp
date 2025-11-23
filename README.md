# Minimalisp

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

**Minimalisp** is a tiny Lisp interpreter written in C, compiled to WebAssembly (WASM) via Emscripten. It provides a minimal core language and a pluggable garbage‑collector interface, making it ideal for experimenting with different GC algorithms.

---

## Table of Contents

- [Features](#features)
- [Building](#building)
- [Usage](#usage)
  - [Command‑line (native)](#command‑line-native)
  - [WebAssembly](#webassembly)
- [Garbage Collector](#garbage-collector)
- [Contributing](#contributing)
- [License](#license)

---

## Features

- Minimal Lisp syntax (numbers, list literals via `cons`/`list`, quoting via `'` or `quote`).
- Primitive list toolkit: `cons`, `car`, `cdr`, `list`, and the `nil` literal.
- Simple REPL‑style evaluator ready for experimentation.
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

Runs arithmetic, REPL-style, and list-focused smoke tests to keep the evaluator healthy.

---

## Usage

### Interactive REPL (native)

```sh
./interpreter
ml> (+ 1 (* 2 3))
7
ml> (list 1 2 (cons 3 nil))
(1 2 (3))
ml> (cdr (list 1 2 3))
(2 3)
ml> '(1 2 (+ 3 4))
(1 2 (+ 3 4))
```

`print` displays structured values, and `nil` is written as `()`. Press `Ctrl-D` (Unix) to exit.

### Command-line (native)

```sh
./interpreter "(print (+ 1 2 3))"
```

### WebAssembly

Load `interpreter.wasm` in a JavaScript environment (browser or Node.js). Example (Node.js):

```js
const fs = require('fs');
// Placeholder for actual Emscripten loading code
// const Module = require('./interpreter.js');
// const evalFunc = Module.cwrap('eval', 'number', ['string']);
// console.log('Result:', evalFunc('(print (+ 1 2 3))'));
```

> **Note**: The exact loading code depends on how Emscripten is configured. The above is illustrative.

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
