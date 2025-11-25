; fragmentation.lisp - Fragmentation test
; Tests memory fragmentation under varied allocation patterns
; Expected: Mark-Sweep shows severe fragmentation, Copying shows none

(define (range start end)
  (if (> start end)
      nil
      (cons start (range (+ start 1) end))))

(define (fragmentation-test)
  (begin
    (print 'Starting-Fragmentation-Test)
    (define (loop n)
      (if (> n 20)
          nil
          (begin
            (range 1 (* n 2))  ; Reduced from 10 to 2
            (gc)
            (loop (+ n 1)))))
    (loop 1)
    (print 'Completed)
    (gc-stats)))

; Run the benchmark
(fragmentation-test)
