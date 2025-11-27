(define (null? x)       ; true iff x is ()
  (if x '() 't))

(define (not x)
  (if x '() 't))

(define (identity x) x)

; Classic list helpers (mirror Scheme small primitives)

(define (append xs ys)
  (if (null? xs)
      ys
      (cons (car xs) (append (cdr xs) ys))))

(define (reverse-iter src acc)
  (if (null? src)
      acc
      (reverse-iter (cdr src) (cons (car src) acc))))

(define (reverse xs)    ; uses the iterative helper defined above
  (reverse-iter xs '()))

(define (length xs)     ; compute list length
  (if (null? xs)
      0
      (+ 1 (length (cdr xs)))))

(define (foldl fn acc xs)   ; left fold
  (if (null? xs)
      acc
      (foldl fn (fn acc (car xs)) (cdr xs))))

(define (foldr fn acc xs)   ; right fold
  (if (null? xs)
      acc
      (fn (car xs) (foldr fn acc (cdr xs)))))

(define (map fn xs)
  (if (null? xs)
      '()
      (cons (fn (car xs)) (map fn (cdr xs)))))

(define (filter pred xs)
  (if (null? xs)
      '()
      (if (pred (car xs))
          (cons (car xs) (filter pred (cdr xs)))
          (filter pred (cdr xs)))))

(define (take xs n)
  (if (null? xs)
      '()
      (if (= n 0)
          '()
          (cons (car xs) (take (cdr xs) (- n 1))))))

(define (drop xs n)
  (if (null? xs)
      '()
      (if (= n 0)
          xs
          (drop (cdr xs) (- n 1)))))

(define (zip-with fn xs ys)
  (if (null? xs)
      '()
      (if (null? ys)
          '()
          (cons (fn (car xs) (car ys))
                (zip-with fn (cdr xs) (cdr ys))))))

(define (abs x)
  (if (< x 0) (- 0 x) x))

(define (range start end)
  (if (> start end)
      '()
      (cons start (range (+ start 1) end))))

(define (sum xs)
  (foldl (lambda (acc x) (+ acc x)) 0 xs))

(define (product xs)
  (foldl (lambda (acc x) (* acc x)) 1 xs))

(define (any pred xs)
  (if (null? xs)
      '()
      (if (pred (car xs))
          't
          (any pred (cdr xs)))))

(define (all pred xs)
  (if (null? xs)
      't
      (if (pred (car xs))
          (all pred (cdr xs))
          '())))

(define (tand thunk1 thunk2)
  (if (thunk1)
      (thunk2)
      '()))

(define (tor thunk1 thunk2)
  ((lambda (val)
     (if val
         val
         (thunk2)))
   (thunk1)))
