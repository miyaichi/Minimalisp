; mixed-lifetime.lisp - Mixed object lifetime benchmark
; Tests handling of both short-lived and long-lived objects
; Expected: Generational optimal for this pattern

(define (range start end)
  (if (> start end)
      nil
      (cons start (range (+ start 1) end))))

(define (make-garbage n)
  (if (= n 0)
      nil
      (begin
        (cons n (cons n nil))
        (make-garbage (- n 1)))))

(define (mixed-lifetime-benchmark iterations)
  (define survivors (range 1 50))  ; Long-lived objects (reduced from 100)
  (define (loop n)
    (if (= n 0)
        survivors
        (begin
          (make-garbage 20)           ; Short-lived objects (reduced from 50)
          (loop (- n 1)))))
  (loop iterations))

(define (run-mixed-test)
  (begin
    (print 'Starting-Mixed-Lifetime-Test)
    (mixed-lifetime-benchmark 500)  ; Reduced from 1000
    (print 'Completed)
    (gc-stats)))

; Run the benchmark
(run-mixed-test)
