;-------------------------------------------------------------------------
;
;  Copyright (c) 2021 Rajit Manohar
;
;  This library is free software; you can redistribute it and/or
;  modify it under the terms of the GNU Lesser General Public
;  License as published by the Free Software Foundation; either
;  version 2.1 of the License, or (at your option) any later version.
;
;  This library is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;  GNU General Public License for more details.
;
;  You should have received a copy of the GNU General Public License
;  along with this program; if not, write to the Free Software
;  Foundation, Inc., 51 Franklin Street, Fifth Floor,
;  Boston, MA  02110-1301, USA.
;
;-------------------------------------------------------------------------
;;
;;
;;  ACT option parsing
;;
;;
;; To use:
;;
;;  1. Define default option symbols in the global scope for your script
;;
;;    For example:
;;
;;    (define output-file "")
;;    (define f-flag #f)
;;
;;  2. Use a quoted list of pairs to specify which command-line options
;;     are used to set each global symbol. For the example, that awould be
;;
;;     '(("o:" output-file) ("f" f-flag))    
;;
;;  3. Call act:getoptions with the quoted list as an argument. The
;;  appropriate symbols will be set, and the variable
;;  act:remaining-args will be a list of the remaining arguments on
;;  the command-line
;;


(if (boolean? act-option-parsing-loaded)
    #t
    (begin

					; where parsed options end and where the rest begin
      (define act:_arg-division
	(lambda (n)
	  (if (>=? n (getargc))
	      n
	      (if (zero? (string-compare (substring (getargv n) 0 1) "-"))
		  (act:_arg-division (+ 1 n))
		  n
		  )
	      )
	  )
	)

					; search for argument that matches request
      (define act:_find-arg
	(let ((arg-end (act:_arg-division 1)))
	  (lambda (argpair n)
	    (if (=? n arg-end)
		#f
		(let ((s (getargv n)))
		  (if (zero? (string-compare
			      (substring s 1 2)
			      (substring (car argpair) 0 1)
			      )
			     )
		      s
		      (act:_find-arg argpair (+ n 1))
		      )
		  )
		)
	    )
	  )
	)

      (define act:get-options
	(let ((process-cmd
	       (lambda (x y)
		 (if (>? (string-length x) 1) (substring y 2 (string-length y)) #t)
		 )
	       ))
	  (lambda (arglist)
	    (if (null? arglist)
		#t
		(let ((x (act:_find-arg (car arglist) 1)))
		  (begin
		    (if 
		     (boolean? x)
		     #t
		     (apply set! (list (cadar arglist) (process-cmd (caar arglist) x))))
		    (act:get-options (cdr arglist))
		    )
		  )
		)
	    )
	  )
	)


      (define act:remaining-args
	(letrec ((helper
		  (lambda (n)
		    (if (=? n (getargc)) () (cons (getargv n) (helper (+ n 1))))
		    )))
	  (helper (act:_arg-division 1)))
	)

      (define act:bool-to-int (lambda (x) (if x 1 0)))

      (define act-option-parsing-loaded #t)
      )
    )
