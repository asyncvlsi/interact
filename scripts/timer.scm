;-------------------------------------------------------------------------
;
;  Copyright (c) 2023 Rajit Manohar
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
 (boolean? timer-loaded)
 #t
 (begin
   (act:pass:load "libacttpass.so" "taggedTG" "tgraph")
   (define timer:print (lambda () (act:pass:run "taggedTG" 1)))
   (define timer:break-unticked-cycles (lambda () (act:pass:run "taggedTG" 2)))
   (define timer-loaded #t)
   )
 )
