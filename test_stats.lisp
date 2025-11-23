(print 'Initial-Stats)
(define s1 (gc-stats))
(print s1)

(print 'Allocating...)
(define (make-garbage n)
  (if (= n 0)
      nil
      (cons n (make-garbage (- n 1)))))

(make-garbage 1000)

(print 'Stats-After-Allocation)
(define s2 (gc-stats))
(print s2)

(print 'Running-GC...)
(gc)

(print 'Stats-After-GC)
(define s3 (gc-stats))
(print s3)

(define (get-collections stats)
  (cdr (car stats)))

(define (get-allocated stats)
  (cdr (car (cdr stats))))

(define alloc1 (get-allocated s1))
(define alloc2 (get-allocated s2))
(define coll1 (get-collections s1))
(define coll3 (get-collections s3))

(print (list 'Allocated-Increased? (> alloc2 alloc1)))
(print (list 'Collections-Increased? (> coll3 coll1)))
