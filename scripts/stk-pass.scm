;-------------------------------------------------------------------------
;
;  Copyright (c) 2021 Rajit Manohar
;
;  This program is free software; you can redistribute it and/or
;  modify it under the terms of the GNU General Public License
;  as published by the Free Software Foundation; either version 2
;  of the License, or (at your option) any later version.
;
;  This program is distributed in the hope that it will be useful,
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

;;  Stack and layout generation passes

(act:pass:load "pass_layout.so" "stk2layout" "layout")

(define act:stk:run (lambda () (act:pass:run "net2stk" 0)))
(define act:layout:run (lambda (mode) (act:pass:run "stk2layout" mode)))

(define act:layout:create (lambda () (act:layout:run 0)))

(define act:layout:lef
  (lambda (lef cell)
    (let ( (f1 (sys:open lef "w")) (f2 (sys:open cell "w")) )
      (begin
	(act:pass:set_file "stk2layout" "lef_file" f1)
	(act:pass:set_file "stk2layout" "cell_file" f2)
	(act:layout:run 1)
	(sys:close f1)
	(sys:close f2)
	)
      )
    )
  )

(define act:layout:rect (lambda () (act:layout:run 4)))

(define act:layout:def
  (lambda (def pins? area_mult aspect_ratio)
    (let ((f (sys:open def "w")))
      (begin
	(act:ckt:mk-nets)
	(act:pass:set_file "stk2layout" "def_file" f)
	(act:pass:set_int "stk2layout" "do_pins" (if pins? 1 0))
	(act:pass:set_real "stk2layout" "area_mult" area_mult)
	(act:pass:set_real "stk2layout" "aspect_ratio" aspect_ratio)
	(act:layout:run 5)
	(sys:close f)
	)
      )
    )
  )
			 
(define act:layout:report
  (lambda ()
    (let ((a (act:pass:get_real "stk2layout" "total_area"))
	  (astd (act:pass:get_real "stk2layout" "stdcell_area"))
	  (aht (act:pass:get_int "stk2layout" "cell_maxheight")))
      (begin
	(act:layout:run 2)
	)
      )
    )
  )
      
