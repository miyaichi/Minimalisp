; real-world.lisp - Real-world simulation benchmark
; Simulates realistic workload patterns
; Expected: Balanced performance test across all GCs

(define (range start end)
  (if (> start end)
      nil
      (cons start (range (+ start 1) end))))

(define (factorial n acc)
  (if (= n 0)
      acc
      (factorial (- n 1) (* n acc))))

(define (run-mixed-workload)
  (begin
    (print 'Starting-Real-World-Test)
    
    ; Factorial computation
    (print 'Running-Factorial)
    (factorial 100 1)
    
    ; List manipulation
    (print 'Running-List-Operations)
    (map (lambda (x) (* x x)) (range 1 100))
    
    ; Nested list construction
    (print 'Running-Nested-Construction)
    (map (lambda (x) (range 1 x)) (range 1 50))
    
    (print 'Completed)
    (gc-stats)))

; Run the benchmark
(run-mixed-workload)
