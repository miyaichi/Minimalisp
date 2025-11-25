; alloc-intensive.lisp - Allocation-intensive benchmark
; Tests throughput under heavy allocation pressure
; Expected: Generational > Copying > Mark-Sweep

(define (alloc-benchmark n)
  (if (= n 0)
      nil
      (begin
        (cons n (cons n nil))
        (alloc-benchmark (- n 1)))))

(define (run-alloc-test iterations)
  (begin
    (print 'Starting-Allocation-Intensive-Test)
    (alloc-benchmark iterations)
    (print 'Completed)
    (gc-stats)))

; Run the benchmark
(run-alloc-test 5000)
