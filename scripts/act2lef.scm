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

(load-scm "act-opt.scm")

(define cell-file "")
(define top-level "")
(define out-file "out")
(define area-str "1.4")
(define ratio-str "1.0")
(define do-report #f)
(define share-stat #f)
(define do-pins #f)
(define do-spice #f)

;; -- display usage message and die --
(define usage
  (lambda ()
    (begin
      (echo "Usage:" (getargv 0) "-p procname [-sPSAR] [-o <name>] [-a <mult>] [-c <cell>] <file.act>")
      (echo " -p <proc>  : name of ACT process corresponding to the top-level of the design")
      (echo " -o <name>  : output files will be <name>.<extension> (default: out)")
      (echo " -s         : emit spice netlist")
      (echo " -P         : include PINS section in DEF file")
      (echo " -a <mult>  : use <mult> as the area multiplier for the DEF fie (default 1.4)")
      (echo " -r <ratio> : use this as the aspect ratio = x-size/y-size (default 1.0)")
      (echo " -c <cell>  : Read in the <cell> ACT file as a starting point for cells,\n\toverwriting it with an updated version with any new cells")
      (echo " -S         : share staticizers")
      (echo " -A         : report area")
      (echo " -R         : generate report")
      (exit 1)
      )
    )
  )

(act:get-options
 '(("c:" cell-file) ("p:" top-level) ("o:" out-file) ("a:" area-str)
   ("r:" ratio-str) ("R" do-report) ("S" share-stat) ("P" do-pins)
   ("s" do-spice))
 )

(if (=? (length act:remaining-args) 1) #t (usage))
(if (=? (string-length top-level) 0)
    (begin (echo "Missing process name") (usage))
    #t
    )

; read input
(act:read (car act:remaining-args))

; check for cells
(if (>? (string-length cell-file) 0) (act:merge cell-file) #t)

; expand design
(act:expand)

; set top level
(act:top top-level)

; map to cells
(ckt:cell-map)

; save updated cells
(if (>? (string-length cell-file) 0) (ckt:cell-save cell-file) #t)

; generate transistor-level impl
(ckt:map)

; save spice file, if needed
(if do-spice (ckt:save-sp (string-append out-file ".sp")) #t)

; read in layout generation passes
(load-scm "stk-pass.scm")


(act:layout:create)
(act:layout:lef (string-append out-file ".lef")
		(string-append out-file ".cell"))

(act:layout:def (string-append out-file ".def")
		do-pins
		(string->number area-str)
		(string->number ratio-str)
		)

(act:layout:rect)

(if do-report (act:layout:report) #t)
