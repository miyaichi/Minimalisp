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
1. **Trace the Execution**: Add `printf` statements in `eval_value` to watch how `(+ 1 2)` is evaluated step-by-step.
2. **Add a Primitive**: Implement a new math function (e.g., `modulo`) in C and register it in `init_builtins`.
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

## 5. Common Pitfalls and How to Avoid Them

### Lazy Evaluation Gotchas

**Problem**: Attempting to use `and`/`or` as if they were regular functions

```lisp
; ❌ This evaluates ALL arguments before calling 'my-and'
(define (my-and a b) (if a b '()))
(my-and (> x 0) (/ 1 x))  ; Will crash if x=0!

; ✅ Use thunks for short-circuit evaluation
(tand (lambda () (> x 0)) (lambda () (/ 1 x)))
```

**Why**: Minimalisp is strictly eager - all function arguments are evaluated before the function body runs.

### Memory Management Patterns

**Best Practice**: Understanding when GC runs

```lisp
; This creates many temporary objects
(define (bad-sum n)
  (if (= n 0) 
      0
      (+ n (bad-sum (- n 1)))))  ; Each call allocates

; Monitor with gc-stats to see collection frequency
(begin
  (define stats-before (gc-stats))
  (bad-sum 1000)
  (define stats-after (gc-stats))
  (print (list 'Collections-increased 
               (- (cdr (car stats-after)) 
                  (cdr (car stats-before))))))
```

### Recursion Without Tail-Call Optimization

**Problem**: Deep recursion causes stack overflow

```lisp
; ❌ Not tail-recursive
(define (factorial n)
  (if (= n 0)
      1
      (* n (factorial (- n 1)))))

; ✅ Tail-recursive with accumulator
(define (factorial n)
  (define (fact-iter n acc)
    (if (= n 0)
        acc
        (fact-iter (- n 1) (* n acc))))
  (fact-iter n 1))
```

### Quoting Confusion

```lisp
; These are different!
(define x 42)
x          ; => 42 (evaluates to the value)
'x         ; => x  (the symbol itself)
(quote x)  ; => x  (same as above)

; Practical use:
(list 1 2 3)        ; => (1 2 3)
'(1 2 3)            ; => (1 2 3) (same result, no evaluation)
(list (+ 1 2) 3)    ; => (3 3)
'((+ 1 2) 3)        ; => ((+ 1 2) 3) (keeps structure)
```

---

## 6. Performance Profiling Patterns

### Measuring Allocation Pressure

```lisp
(define (measure-allocations thunk)
  (define before (gc-stats))
  (define result (thunk))
  (gc)  ; Force collection to get accurate stats
  (define after (gc-stats))
  (define get-allocated 
    (lambda (stats) (cdr (car (cdr stats)))))
  (print (list 'Allocated-bytes 
               (- (get-allocated after) 
                  (get-allocated before))))
  result)

; Usage:
(measure-allocations 
  (lambda () (map (lambda (x) (* x x)) (range 1 1000))))
```

### Comparing GC Backends

```bash
# Test the same workload across all backends
for backend in mark-sweep copying generational; do
  echo "Testing $backend..."
  GC_BACKEND=$backend ./interpreter -f my-benchmark.lisp
done
```

### Fragmentation Detection

```lisp
(define (check-fragmentation)
  (define stats (gc-stats))
  (define get-val 
    (lambda (key stats)
      (if (null? stats)
          0
          (if (eq? (car (car stats)) key)
              (cdr (car stats))
              (get-val key (cdr stats))))))
  (define frag-index (get-val 'fragmentation-index stats))
  (if (> frag-index 0.5)
      (print '(Warning: High fragmentation detected))
      (print '(Fragmentation: OK))))
```

---

## 7. Debugging Techniques

### Tracing Function Calls

```lisp
; Wrap any function to trace its calls
(define (traced name fn)
  (lambda (arg)
    (begin
      (print (list 'Calling name arg))
      (define result (fn arg))
      (print (list 'Returned result))
      result)))

; Usage:
(define factorial 
  (traced 'factorial
    (lambda (n)
      (if (= n 0) 1 (* n (factorial (- n 1)))))))

(factorial 5)
```

### Inspecting Data Structures

```lisp
; Simple pretty-printer for nested structures
(define (pp expr depth)
  (define (indent n)
    (if (= n 0)
        nil
        (begin
          (princ " ")
          (indent (- n 1)))))
  (if (atom expr)
      (begin
        (indent (* depth 2))
        (print expr))
      (begin
        (indent (* depth 2))
        (print '()
        (map (lambda (x) (pp x (+ depth 1))) expr)
        (indent (* depth 2))
        (print '))))

; Or load pprint.lisp for the full implementation
(load 'pprint.lisp)
(pprint '(define (fact n) (if (= n 0) 1 (* n (fact (- n 1))))))
```

### Understanding Error Messages

Common errors and their meanings:

| Error | Meaning | Fix |
|-------|---------|-----|
| `Undefined symbol: x` | Variable not in scope | Check `define` or function parameters |
| `Attempt to call nil` | Calling undefined function | Verify function name |
| `car expects a list` | Type mismatch | Check data type with `atom` |
| `Too few arguments` | Lambda parameter mismatch | Count arguments |

---

## 8. Advanced Patterns

### Y Combinator (Recursion without `define`)

```lisp
; The Y combinator allows anonymous recursion
(define Y
  (lambda (f)
    ((lambda (x) (f (lambda (y) ((x x) y))))
     (lambda (x) (f (lambda (y) ((x x) y)))))))

; Factorial using Y
((Y (lambda (fact)
      (lambda (n)
        (if (= n 0)
            1
            (* n (fact (- n 1)))))))
 5)  ; => 120
```

### Church Encoding (Numbers as Functions)

```lisp
; Represent numbers as repeated function application
(define zero (lambda (f) (lambda (x) x)))
(define one (lambda (f) (lambda (x) (f x))))
(define two (lambda (f) (lambda (x) (f (f x)))))

; Successor function
(define succ
  (lambda (n)
    (lambda (f)
      (lambda (x)
        (f ((n f) x))))))

; Convert to integer
(define church->int
  (lambda (n)
    ((n (lambda (x) (+ x 1))) 0)))

(church->int (succ two))  ; => 3
```

### Continuation-Passing Style (CPS)

```lisp
; Direct style
(define (factorial n)
  (if (= n 0) 1 (* n (factorial (- n 1)))))

; CPS style - continuation receives result
(define (factorial-cps n cont)
  (if (= n 0)
      (cont 1)
      (factorial-cps (- n 1)
                     (lambda (res) 
                       (cont (* n res))))))

; Call with identity continuation
(factorial-cps 5 (lambda (x) x))  ; => 120
```

---

## 9. "Break It to Learn It"

The codebase is small enough that you can break it and fix it in an afternoon.

- **Disable the GC**: Comment out the `gc()` call in `cons`. Run a loop. Watch it crash.
- **Corrupt the Heap**: Manually overwrite a pointer. See how the visualizer reacts (or how the interpreter segfaults).
- **Stack Overflow**: Write a deeply recursive function without tail-call optimization.
- **Memory Leak**: Create a circular reference and see if the GC handles it.
- **Type Confusion**: Pass a number where a list is expected and trace the error.

---

## Resources

- **`docs/gc-algorithms.md`**: Detailed explanation of the implemented GCs.
- **`docs/gc-performance-report.md`**: Analysis of GC performance.
- **`src/interpreter.c`**: The heart of the language.
- **`include/gc.h`**: The contract for GC backends.
- **`standard-lib.lisp`**: Pure-Lisp implementations of common functions.

---

Minimalisp is your sandbox. Build castles, dig holes, and enjoy the process of discovery!
