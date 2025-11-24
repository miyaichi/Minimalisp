(define (abs x)
  (if (< x 0) (- 0 x) x))

(define (safe? col queens)
  (let loop ((qs queens) (row 1))
    (if (null? qs)
        #t
        (let ((c (car qs)))
          (if (or (= c col)
                  (= (- c row) (- col (length queens)))
                  (= (+ c row) (+ col (length queens))))
              #f
              (loop (cdr qs) (+ row 1)))))))

(define (extend queens n size)
  (if (> n size)
      (begin
        (print queens)
        (list queens))
      (let ((solutions '()))
        (let try ((col 1))
          (if (> col size)
              solutions
              (let ((new (cons col queens)))
                (if (safe? col queens)
                    (set! solutions (append (extend new (+ n 1) size) solutions)))
                (try (+ col 1))))))))

(define (n-queens size)
  (extend '() 1 size))

(n-queens 4)
