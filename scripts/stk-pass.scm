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

(if (boolean? stk-pass-loaded)
    #t
    (begin
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

      (define act:layout:set-lef-bbox
	(lambda (cellname width height)
	  (begin
	    (act:pass:set_string "stk2layout" "cell_name" cellname)
	    (act:pass:set_int "stk2layout" "cell_width" width)
	    (act:pass:set_int "stk2layout" "cell_height" height)
	    (act:pass:runcmd "stk2layout" "setbbox")
	    )
	  )
	)
      (define act:layout:set-lef-bbox-helper (lambda (l) (apply act:layout:set-lef-bbox l)))

      (define act:layout:rect (lambda () (act:layout:run 4)))

      (define act:layout:def
	(lambda (def pins? area_mult aspect_ratio)
	  (let ((f (sys:open def "w")))
	    (begin
	      (ckt:mk-nets)
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

      (define round3 (lambda (x) (/ (truncate (+ (* x 1000) 0.5)) 1000.0)))
      
      (define act:layout:report
	(lambda ()
	  (let ((a (* (tech:uscale) (* (tech:uscale)
			 (act:pass:get_real "stk2layout" "total_area"))
		      )
		   )
		(astd (* (tech:uscale) (* (tech:uscale)
			 (act:pass:get_real "stk2layout" "stdcell_area"))
			 )
		      )
		(aht (act:pass:get_int "stk2layout" "cell_maxheight")))
	    (begin
	      (act:layout:run 2)
	      (if (>=? a 10000)
		  (echo "Total area:" (round3 (/ a 1000000)) "mm^2")
		  (echo "Total area:" (round3 a) "um^2")
		  )
	      (if (>=? astd 10000)
		  (echo "Total stdcell area:" (round3 (/ astd 1000000)) "mm^2")
		  (echo "Total stdcell area:" astd "um^2")
		  )
	      (echo "Max cell height:" (/ aht (tech:getpitch 1)))
	      (echo "Gridded cell improvement:" (* 100 (round3 (/ (- astd a) astd))) "%")
	      )
	    )
	  )
	)

      (define stk-pass-loaded #t)
      )
    )


(load-scm "more-string.scm")

(help-add "act:layout:create"
    "Create the layout for the ACT design"
	  (string-multiappend
	   (list 
	    ">  Usage: (act:layout:create)"
	    "|    This walks through the ACT design and creates internal data sturctures"
	    "|    for the layout problem, including individual cell layout and cell LEF"
	    "|    Normally this function is called by phydb:create rather than directly."
	    ""
	   )
	   )
	  )

(help-add "act:layout:lef"
    "Save LEF and CELL file for the design"
	  (string-multiappend
	   (list 
	    ">  Usage: (act:layout:lef lef-file-name cell-file-name)"
	    "|    act:layout:create must be called before this function can be used"
	    "|    Normally this function is called by phydb:create rather than directly."
	    ""
	   )
	   )
	  )

(help-add "act:layout:rect"
    "Save .rect files for cells to the current working directory"
	  (string-multiappend
	   (list
	    ">  Usage: (act:layout:rect)"
	    "|    act:layout:create must be called before this function can be used."
	    ""
	    )
	   )
	  )

(help-add "act:layout:def"
    "Generate DEF file with ACT netlist in DEF format"
	  (string-multiappend
	   (list
	    ">  Usage: (act:layout:def def-file pins? area_mult aspect_r)"
	    ">    def-file - the output file name"
	    ">       pins? - #t or #f if pins should be generated"
	    ">   area_mult - scale factor for die area compared to area required for cells"
	    ">    aspect_r - aspect ratio for the die area"
	    "|    act:layout:create must be called before this function can be used."
	    "|    Normally this function is called by phydb:create rather than directly."
	    ""
	    )
	   )
	  )

(help-add "act:layout:report"
    "Print out area report and break-down"
	  (string-multiappend
	   (list
	    ">  Usage: (act:layout:report)"
	    "|    act:layout:create must be called before this function can be used"
	    ""
	    )
	   )
	  )
