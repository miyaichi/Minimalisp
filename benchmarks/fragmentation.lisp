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
    (map (lambda (n) 
           (begin
             (range 1 (* n 10))
             (gc)))
         (range 1 20))
    (print 'Completed)
    (gc-stats)))

; Run the benchmark
(fragmentation-test)
