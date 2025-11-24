(define (not x)
  (if x '() t))

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

(define (emit-solution solution)
  (print solution)
  (list solution))

(define (conflict? column value offset)
  (if (= column value)
      t
      (if (= column (+ value offset))
          t
          (if (= column (- value offset))
              t
              '()))))

(define (safe? column queens offset)
  (if (null? queens)
      t
      (if (conflict? column (car queens) offset)
          '()
          (safe? column (cdr queens) (+ offset 1)))))

(define (extend-columns queens row size col accum)
  (if (> col size)
      accum
      (if (safe? col queens 1)
          (extend-columns queens row size (+ col 1)
                           (append (extend (cons col queens) (+ row 1) size)
                                   accum))
          (extend-columns queens row size (+ col 1) accum))))

(define (extend queens row size)
  (if (> row size)
      (emit-solution (reverse queens))
      (extend-columns queens row size 1 '())))

(define (n-queens size)
  (extend '() 1 size))

(n-queens 4)
