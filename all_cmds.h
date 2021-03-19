/*************************************************************************
 *
 *  Copyright (c) 2021 Rajit Manohar
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *
 **************************************************************************
 */
#include <stdio.h>
#include <act/act.h>

void act_cmds_init (void);
void conf_cmds_init (void);
void misc_cmds_init (void);


/* -- functions exported -- */
FILE *sys_get_fileptr (int v);
void act_flatten_prs (Act *a, FILE *fp, Process *p, int mode);
void act_flatten_sim (Act *a, FILE *fps, FILE *fpa, Process *p);
void act_emit_verilog (Act *a, FILE *fp, Process *p);
