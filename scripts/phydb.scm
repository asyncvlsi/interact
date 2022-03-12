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

   (define pydb-loaded #t)
   )
 )
