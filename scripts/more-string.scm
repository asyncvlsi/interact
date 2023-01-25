(define string-nl " ")
(string-set! string-nl 0 10)
(define string-multiappend
  (lambda (l)
    (cond
     ((null? l) "")
   ((null? (cdr l)) (car l))
   (#t (string-append (string-append (car l) string-nl) (string-multiappend (cdr l))))
   )
    )
  )
