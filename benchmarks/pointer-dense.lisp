; pointer-dense.lisp - Pointer-dense benchmark
; Tests GC performance with deep object graphs
; Expected: Performance depends on tree lifetime

(define (deep-tree depth)
  (if (= depth 0)
      'leaf
      (cons (deep-tree (- depth 1))
            (deep-tree (- depth 1)))))

(define (run-tree-test)
  (begin
    (print 'Starting-Pointer-Dense-Test)
    (print 'Building-Deep-Tree)
    (deep-tree 8)  ; Reduced from 10 to avoid excessive memory
    (print 'Tree-Built)
    (gc-stats)))

; Run the benchmark
(run-tree-test)
