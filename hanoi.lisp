; hanoi.lisp - Implementation of the Tower of Hanoi problem
(define (hanoi n from to aux)
  (if (= n 0)
      nil
      (begin
        (hanoi (- n 1) from aux to)
        (print (list from to))
        (hanoi (- n 1) aux to from))))

(hanoi 3 'A 'C 'B)
