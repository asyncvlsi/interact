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

(if
 (boolean? phydb-loaded)
 #t
 (begin
   (load-scm "stk-pass.scm")
   (define phydb:create			; populate phydb
     (lambda (area ratio lefout)
       (begin
	 (phydb:init)
	 (act:layout:create)
	 (act:layout:lef "_out.lef" "_out.cell")
	 (act:layout:def "_out.def" #t area ratio)
	 (phydb:read-lef "_out.lef")
	 (phydb:read-cell "_out.cell")
	 (phydb:read-def "_out.def")
	 (system "rm _out.def _out.cell")
	 (system (string-append "mv _out.lef " lefout))
       )
     )
   )

   (define phydb:create-stdcell		; populate phydb
     (lambda (area ratio lef_list)
       (begin
	 (phydb:init)
	 (act:layout:create)
	 (mapcar phydb:read-lef lef_list) ; read in external LEF
	 (mapcar act:layout:set-lef-bbox-helper (phydb:get-used-lef))
	 (act:layout:def "_out.def" #t area ratio)
	 (phydb:read-def "_out.def")
	 (system "rm _out.def")
	 )
       )
     )
 
   (define phydb:update-lef
      (lambda (outname)
        (begin (system (string-append (string-append "rect2lef.pl " (string-append (string-append outname "_ppnp circuitppnp >> ") (string-append outname ".lef "))) (number->string (conf:get_int "lefdef.micron_conversion"))))
               (system (string-append (string-append "rect2lef.pl " (string-append (string-append outname "_wells circuitwell >>") (string-append outname ".lef "))) (number->string (conf:get_int "lefdef.micron_conversion"))))
        )
      )
   )

   (define pydb-loaded #t)
   )
 )

(help-add "phydb:create"
	  "Create layout problem and populate the physical database."
	  (string-multiappend
	   (list
	    ">  Usage: (phydb:create area ratio lef-file)"
	    ">         area - the multiplier used to compute the die area from cell area"
	    ">        ratio - aspect ratio"
	    ">     lef-file - filename for LEF output file"
	    "|"
	    "|    This is used to take an ACT design, create the layout problem, and store"
	    "|    it in the physical database (phydb) used by the ACT tools."
	    ""
	    )
	   )
	  )

(help-add "phydb:create-stdcell"
   "Create a standard-cell layout problem and populate the physical database."
	  (string-multiappend
	   (list
	    ">  Usage: (phydb:create-stdcell area ratio lef_list)"
	    ">        area - the multiplier used to compute the die area from cell area"
	    ">       ratio - aspect ratio"
	    ">    lef_list - list of external LEF files used for the layout problem"
	    "|"
	    "|    This is used to take an ACT design, create the layout problem, and store"
	    "|    it in the physical database (phydb) used by the ACT tools."
	    "|    In this version, the assumption is that an external standard cell library"
	    "|    is being used, and the list of LEF files needed are provided to the"
	    "|    command."
	    ""
	    )
	   )
	  )


(help-add "phydb:update-lef"
    "Update LEF with definitions of well/select cells needed."
	  (string-multiappend
	   (list
	    ">  Usage: (phydb:update-lef lef-file)"
	    "|"
	    "|    After placement, new cells are created for well/select legalization."
	    "|    This command appends the LEF definitions of these cells to the LEF file."
	    ""
	    )
	   )
	  )
