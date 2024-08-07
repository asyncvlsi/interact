;-------------------------------------------------------------------------
;
;  Copyright (c) 2024 Rajit Manohar
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
(load-scm "more-string.scm")
(if
 (boolean? annotate-loaded)
 #t
 (begin
   (act:pass:load "annotate_pass.so" "annotate" "annotate_pass")
   (define annotate:spef (lambda () (act:pass:run "annotate" 0)))

   (help-add "annotate:spef"
	     "Load SPEF files for netlist back-annotation."
	     (string-multiappend
	      (list
	       ">  Usage: (annotate:spef)"
	       "|"
	       "|    Use this command to read in SPEF back-annotation files."
	       "|    Saving the netlist after this command will include any"
	       "|    resistors and capacitors found in the SPEF file."
	       "|"
	       "|    If the spef.<process> configuration string is defined, then"
	       "|    that is used as the SPEF file for the ACT process. Otherwise,"
	       "|    <process>.spef is used as the SPEF file name. If neither"
	       "|    exist, then it is assumed that no back-annotation files"
	       "|    are available for the process."
	       "|"
	       )
	      )
	     )
   
   (define annotate-loaded #t)
   )
 )
