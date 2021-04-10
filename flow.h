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
#include "config_pkg.h"

#ifdef FOUND_dali
#include <dali/dali.h>
#endif

#ifdef FOUND_phydb
#include <phydb/phydb.h>
#endif

enum design_state {
		   STATE_EMPTY,    /* no act file/design has been
				      specified */
		   
		   STATE_DESIGN,   /* design specified, but not
				      expanded */
		   
		   STATE_EXPANDED, /* design expanded */
		   
		   STATE_ERROR	/* something bad happened: never
				   reached, used to make sure there
				   is no match to the current flow state */
};

/* -- flow state -- */

#define TIMER_NONE 0
#define TIMER_INIT 1
#define TIMER_RUN  2

struct flow_state {
  design_state s;		/* current design state */

  unsigned int cell_map:1;	/* 1 if design has been mapped to
				   cells */
  
  unsigned int ckt_gen:1;	/* 1 if circuit transistor netlist has
				   been created */

  unsigned int timer:2;		/* state of the timer */

#ifdef FOUND_dali
  dali::Dali *dali;
#endif

#ifdef FOUND_phydb
  phydb::PhyDB *phydb;

  unsigned int phydb_lef:1;	/* read in LEF */
  unsigned int phydb_def:1;	/* read in DEF */
  unsigned int phydb_cell:1;	/* read in CELL */
  
#endif  

  Act *act_design;		/* Act: entire design */
  
  Process *act_toplevel;	/* Top-level module */

  ActStatePass *sp;
  ActDynamicPass *tp;		/* timing, if exists */
};

extern flow_state F;

int std_argcheck (int argc, char **argv, int argnum, const char *usage,
		  design_state required);

FILE *std_open_output (const char *cmd, const char *s);
void std_close_output (FILE *fp);
void flow_init (void);

#endif /* __INTERACT_FLOW_H__ */
