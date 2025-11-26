; tarai.lisp - Implementation of the Tarai (Takeuchi) function in Lisp
(define (tarai x y z)
  ; if you want to debug, uncomment the following line:
  ; (print (list x y z))
  (if (<= x y)
      y
      (tarai (tarai (- x 1) y z)
             (tarai (- y 1) z x)
             (tarai (- z 1) x y))))

(tarai 4 1 0)