; === GC Visualization Demos ===

; Demo 1: Simple allocation wave
; Repeatedly allocate lists and trigger collections to visualize
; allocation/collection waves in the heap view.
(define (make-garbage n)
  (if (= n 0)
      nil
      (cons n (make-garbage (- n 1)))))

(define (demo-wave)
  (begin
    (print '(=== Wave Demo ===))
    (make-garbage 100)
    (gc)
    (make-garbage 100)
    (gc)
    (make-garbage 100)))

; Demo 2: Fragmentation test
; Builds many lists of varying length to fragment mark-sweep heaps
; so you can observe free-list gaps and copying compaction behavior.
(define (demo-fragment)
  (begin
    (print '(=== Fragmentation Demo ===))
    (map (lambda (n) (range 1 n)) 
         (range 1 20))))

; Demo 3: Generational test
; Keeps some survivors while allocating short-lived objects so the
; generational backend shows nursery promotions and survivor retention.
(define (demo-generational)
  (begin
    (print '(=== Generational Demo ===))
    (define survivors (list 1 2 3))
    (define (loop n)
      (if (= n 0)
          survivors
          (begin
            (cons n nil)
            (loop (- n 1)))))
    (loop 100)))

; Demo 4: Tree allocation
; Constructs a full binary tree to stress recursive allocation and
; highlight how each backend handles large graph structures.
(define (demo-tree)
  (begin
    (print '(=== Tree Demo ===))
    (define (tree d)
      (if (= d 0) 
          'leaf
          (list (tree (- d 1)) (tree (- d 1)))))
    (tree 6)))
