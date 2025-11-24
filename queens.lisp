(define (null? x)
  (if x '() t))

(define (append xs ys)
  (if xs
      (cons (car xs) (append (cdr xs) ys))
      ys))

(define (reverse-iter src acc)
  (if src
      (reverse-iter (cdr src) (cons (car src) acc))
      acc))

(define (reverse xs)
  (reverse-iter xs '()))

(define (abs x)
  (if (< x 0) (- 0 x) x))

(define (safe? column queens offset)
  (if (null? queens)
      t
      (if (= column (car queens))
          '()
          (if (= (abs (- column (car queens))) offset)
              '()
              (safe? column (cdr queens) (+ offset 1))))))

(define (extend-columns queens row size col solutions)
  (if (> col size)
      solutions
      ((lambda (next new-col)
         (if (safe? col queens 1)
             (extend-columns queens row size new-col
                              (append (extend next (+ row 1) size) solutions))
             (extend-columns queens row size new-col solutions)))
       (cons col queens)
       (+ col 1))))

(define (extend queens row size)
  (if (> row size)
      ((lambda (solution)
         (print solution)
         (list solution))
       (reverse queens))
      (extend-columns queens row size 1 '())))

(define (n-queens size)
  (extend '() 1 size))

(n-queens 4)
