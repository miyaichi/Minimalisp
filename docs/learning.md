# Learning with Minimalisp

Minimalisp is designed not just as a compact Lisp interpreter, but as an approachable platform for learning **interpreter design**, **garbage collection (GC) algorithms**, and **WebAssembly (WASM) integration**.

Whether you are a student, a researcher, or a hobbyist, this project offers a unique "glass-box" view into how dynamic languages work under the hood.

---

## 1. Interpreter Design 101

At its core, Minimalisp demonstrates the fundamental components of a Lisp system in a small C codebase.

### Key Concepts
- **S-Expressions**: How code and data are represented uniformly (homoiconicity).
- **The Evaluator**: The recursive `eval` function in `src/interpreter.c`.
- **Environments**: How variables are bound and looked up in scopes.
- **Primitives vs User Functions**: How C functions interact with Lisp-defined lambdas.

### Suggested Exercises
1. **Trace the Execution**: Add `printf` statements in `eval_expr` to watch how `(+ 1 2)` is evaluated step-by-step.
2. **Add a Primitive**: Implement a new math function (e.g., `modulo`) in C and register it in `setup_env`.
3. **Implement `let`**: Currently, local variables are handled via `lambda`. Try implementing `let` as a macro or a special form.

---

## 2. Garbage Collection Research

Minimalisp's standout feature is its **pluggable GC architecture**. Unlike most toy interpreters that hardcode a single GC, Minimalisp defines a generic interface (`include/gc.h`) allowing you to swap and compare algorithms at runtime.

### Key Concepts
- **Mark-and-Sweep**: The default, simple tracing collector.
- **Copying GC**: A semi-space collector that moves objects to compact memory.
- **Generational GC**: A hybrid approach optimizing for the "generational hypothesis" (most objects die young).
- **Write Barriers**: How to track pointers from old to young generations.

### Suggested Exercises
1. **Compare Performance**: Use `scripts/run-gc-benchmarks.sh` to run the benchmark suite. Why is Copying GC faster than Mark-Sweep for allocation-heavy tasks?
2. **Visualize Memory**: Run the WASM version (`make` then open `web/index.html`). Watch how the heap fills up and how different algorithms clean it.
3. **Implement a New GC**: Create `src/gc/mark_compact.c` implementing the `GcBackend` interface. Try to combine the space efficiency of Mark-Sweep with the compaction of Copying GC.

---

## 3. WebAssembly & Systems Integration

Minimalisp compiles to WASM, allowing it to run in the browser with direct memory access for visualization. This makes it a great case study for **Emscripten** and **interfacing C with JavaScript**.

### Key Concepts
- **Emscripten Build Pipeline**: See the `Makefile` to understand how C code becomes `.wasm` and `.js`.
- **Shared Memory**: How the JavaScript frontend reads the C heap directly to draw the visualization.
- **Event Loop**: How a blocking C interpreter is adapted (or not) for the browser environment.

### Suggested Exercises
1. **Modify the Visualization**: Change `web/main.js` to display objects differently (e.g., different colors for different types).
2. **Expose a New Function to JS**: Use `EMSCRIPTEN_KEEPALIVE` to export a C function and call it from the browser console.

---

## 4. Educational Pathways

### 🎓 For Students (CS 101/201)
- **Goal**: Understand recursion and data structures.
- **Task**: Write `map` and `filter` in Lisp (see `standard-lib.lisp`). Implement a simple game like Tic-Tac-Toe.

### 🔬 For Researchers
- **Goal**: Test new memory management theories.
- **Task**: Modify the object header structure in `interpreter.c`. Implement a custom allocator or a concurrent GC (advanced). Use the benchmark suite to gather data.

### 🛠 For Systems Programmers
- **Goal**: Master C and low-level optimization.
- **Task**: Optimize the `mark` phase using prefetching. Implement "NaN boxing" for values to save memory.

---

## 5. "Break It to Learn It"

The codebase is small enough that you can break it and fix it in an afternoon.

- **Disable the GC**: Comment out the `gc()` call in `cons`. Run a loop. Watch it crash.
- **Corrupt the Heap**: Manually overwrite a pointer. See how the visualizer reacts (or how the interpreter segfaults).
- **Stack Overflow**: Write a deeply recursive function without tail-call optimization.

---

## Resources

- **`docs/gc-algorithms.md`**: Detailed explanation of the implemented GCs.
- **`docs/gc-performance-report.md`**: Analysis of GC performance.
- **`src/interpreter.c`**: The heart of the language.
- **`include/gc.h`**: The contract for GC backends.

Minimalisp is your sandbox. Build castles, dig holes, and enjoy the process of discovery!
