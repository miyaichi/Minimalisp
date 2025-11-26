; Pretty-prints nested s-expressions, indenting list elements per depth and handling dotted tails.

; User-facing entry point: defaults indent to 0.
(define (pprint s-expression)
  (pprint-inner s-expression 0)
  nil)

(define (pprint-inner s-expression indent)
  ((lambda (new-indent)
     (if (atom s-expression)
         (princ s-expression)
         (begin
           (princ "(")
           (pprint-inner (car s-expression) new-indent)
           
           ; Iterate through the tail so lists and dotted pairs are handled uniformly.
           ((lambda (iter)
              (iter iter (cdr s-expression) nil))
            (lambda (self rest multiline)
              (if (not rest)
                  (begin
                    ; When we already emitted a tail element we need a newline before ')'.
                    (if multiline
                        (begin
                          (format t "~%")
                          (format t "~v@t" indent))
                        nil)
                    (princ ")"))
                  (if (atom rest)
                      ; Dotted tail – print " . tail" inline and close immediately.
                      (begin
                        (princ " . ")
                        (pprint-inner rest new-indent)
                        (princ ")"))
                      (begin
                        (format t "~%")
                        (format t "~v@t" new-indent)
                        (pprint-inner (car rest) new-indent)
                        (self self (cdr rest) t)))))))))
   (+ indent 2))
  nil)
