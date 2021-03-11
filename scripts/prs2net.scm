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
;;------------------------------------------------------------------------
;;
;; prs2net as an interact program
;;
;;------------------------------------------------------------------------

(load-scm "act-opt.scm")		; options parsing

;; -- default options --
(define cell-file "")
(define top-level "")
(define out-file "-")
(define lvs-net #f)
(define only-top #f)
(define parasitics #f)
(define black-box-off #f)

;; -- display usage message and die --
(define usage
  (lambda ()
    (begin
      (echo "Usage:" (getargv 0) "[-dltBS] [-c <cells>] [-o <file>] -p <proc> <act>")
      (echo " -c <cells>  Use <cells> file")
      (echo " -p <proc>   Top-level process is <proc>")
      (echo " -o <file>   Save output to <file>")
      (echo " -t          Only emit top-level process")
      (echo " -d          Emit parasitic source/drain area/perimeter")
      (echo " -l          LVS netlist")
      (echo " -S          Long channel device sharing")
      (echo " -B          Turn off black-box mode")
      (exit)
      )
    )
  )

;; --  parse options --
(act:get-options
 '(("c:" cell-file) ("p:" top-level) ("o:" out-file)
   ("l" lvs-net) ("t" only-top) ("d" parasitics) ("B" black-box-off))
 )

;; -- we need a file name --
(if (=? (length act:remaining-args) 1) #t (usage))

;;
;; set defaults for config parameters that control netlist generation
;;
(act:conf:set_default_string "net.global_vdd" "Vdd")
(act:conf:set_default_string "net.global_gnd" "GND")
(act:conf:set_default_string "net.local_vdd" "VddN")
(act:conf:set_default_string "net.local_vdd" "GNDN")

(act:conf:set_default_int "net.ignore_loadcap" (act:bool-to-int lvs-net))
(act:conf:set_default_int "net.emit_parasitics" (act:bool-to-int parasitics))
(act:conf:set_default_int "net.black_box_mode"
			  (act:bool-to-int (not black-box-off)))
(act:conf:set_default_int "net.top_level_only" (act:bool-to-int
						only-top))

;; -- read act file --
(act:read (car act:remaining-args))

;; -- if netlist mangling options specified, use them --
(if (=? (act:conf:gettype "net.mangle_chars") 1)
    (act:mangle (act:conf:get_string "net.mangle_chars"))
    #t)

;; -- cell map if cells file specified --
(if (>? (string-length cell-file) 0)
    (begin 
      (act:merge cell-file)
      (act:cell:map)
      )
    #t)

;; -- expand design --
(act:expand)

;; -- check for process name --
(if (>? (string-length top-level) 0)
    (act:top top-level)
    (begin
      (echo "Missing process name!")
      (usage)
      )
    )

;; -- map circuit to transistors --
(act:ckt:map)

;; -- save SPICE file --
(act:ckt:save_sp out-file)
