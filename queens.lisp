; queens.lisp - N-Queens problem solver
(define (safe? column queens offset)
  (if (null? queens)
      't
      (if (= column (car queens))
          '()
          (if (= (abs (- column (car queens))) offset)
              '()
              (safe? column (cdr queens) (+ offset 1))))))

(define (extend-columns-step queens row size col solutions next new-col)
  (if (safe? col queens 1)
      (extend-columns queens row size new-col
                       (append (extend next (+ row 1) size) solutions))
      (extend-columns queens row size new-col solutions)))

(define (extend-columns queens row size col solutions)
  (if (> col size)
      solutions
      (extend-columns-step queens row size col solutions
                            (cons col queens)
                            (+ col 1))))

(define (emit-solution solution)
  (print solution)
  (list solution))

(define (extend queens row size)
  (if (> row size)
      (emit-solution (reverse queens))
      (extend-columns queens row size 1 '())))

(define (n-queens size)
  (extend '() 1 size))

(n-queens 4)
