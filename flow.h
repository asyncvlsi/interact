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
#ifndef __INTERACT_FLOW_H__
#define __INTERACT_FLOW_H__

#include <act/act.h>
#include <act/passes.h>

enum design_state {
		   STATE_NONE,
		   STATE_EMPTY,
		   STATE_DESIGN,
		   STATE_EXPANDED,
		   STATE_ERROR
};

/* -- flow state -- */

#define TIMER_INIT 1
#define TIMER_RUN  2

struct flow_state {
  unsigned int cell_map:1;
  unsigned int ckt_gen:1;

  unsigned int timer:2;

  design_state s;

  Act *act_design;
  Process *act_toplevel;
};

extern flow_state F;

int std_argcheck (int argc, char **argv, int argnum, const char *usage,
		  design_state required);

FILE *std_open_output (const char *cmd, const char *s);
void std_close_output (FILE *fp);

#endif /* __INTERACT_FLOW_H__ */
