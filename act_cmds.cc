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
#include <string.h>
#include <act/act.h>
#include <act/passes.h>
#include <config.h>
#include <lispCli.h>
#include "all_cmds.h"

enum design_state {
		   STATE_NONE,
		   STATE_EMPTY,
		   STATE_DESIGN,
		   STATE_EXPANDED,
		   STATE_TOPLEVEL,
		   STATE_ANALYSIS_CELL,
		   STATE_ANALYSIS_CKT
};

static design_state current_state;

static design_state state_nonempty[] =
  {
   STATE_DESIGN,
   STATE_EXPANDED,
   STATE_TOPLEVEL,
   STATE_ANALYSIS_CELL,
   STATE_ANALYSIS_CKT,
   STATE_NONE
  };

static Act *act_design = NULL;
static Process *act_toplevel = NULL;

static const char *get_state_str (design_state d)
{
  switch (d) {
  case STATE_EMPTY:
    return "[no design]";
    break;
  case STATE_DESIGN:
    return "[unexpanded design]";
    break;
  case STATE_EXPANDED:
    return "[expanded design]";
    break;
  case STATE_TOPLEVEL:
    return "[top-level set]";
    break;
  case STATE_ANALYSIS_CELL:
    return "[analysis/cell]";
    break;
  case STATE_ANALYSIS_CKT:
    return "[analysis/ckt]";
  case STATE_NONE:
    return "Should not be here";
    break;
  }
}

static int std_argcheck (int argc, char **argv, int argnum, const char *usage,
			 design_state required)
{
  if (argc != argnum) {
    fprintf (stderr, "Usage: %s %s\n", argv[0], usage);
    return 0;
  }
  if (current_state != required) {
    warning ("%s: command failed.\n  Flow state: %s\n    Expected: %s.", argv[0],
	     get_state_str (current_state),
	     get_state_str (required));
    return 0;
  }
  return 1;
}

static int std_argcheck (int argc, char **argv, int argnum, const char *usage,
			  design_state *required)
{
  if (argc != argnum) {
    fprintf (stderr, "Usage: %s %s\n", argv[0], usage);
    return 0;
  }
  for (int i=0; required[i] != STATE_NONE; i++) {
    if (current_state == required[i]) {
      return 1;
    }
  }
  warning ("%s: command failed.\n  Flow state: %s", argv[0],
	   get_state_str (current_state));
  for (int i=0; required[i] != STATE_NONE; i++) {
    if (i > 0) {
      fprintf (stderr, ",");
    }
    else {
      fprintf (stderr, "    Expected:");
    }
    fprintf (stderr, " %s", get_state_str (required[i]));
  }
  fprintf (stderr, "\n");
  return 0;
}

static FILE *std_open_output (const char *cmd, const char *s)
{
  FILE *fp;
  if (strcmp (s, "-") == 0) {
    fp = stdout;
  }
  else {
    fp = fopen (s, "w");
    if (!fp) {
      return NULL;
    }
  }
  return fp;
}

static void std_close_output (FILE *fp)
{
  if (fp != stdout) {
    fclose (fp);
  }
}

static int process_read (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EMPTY)) {
    return 0;
  }
  fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
	     argv[1]);
    return 0;
  }
  fclose (fp);
  act_design = new Act (argv[1]);
  current_state = STATE_DESIGN;
  return 1;
}


static int process_merge (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_DESIGN)) {
    return 0;
  }
  fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
	     argv[1]);
    return 0;
  }
  fclose (fp);
  act_design->Merge (argv[1]);
  return 1;
}

static int process_save (int argc, char **argv)
{
  FILE *fp;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <file>\n", argv[0]);
    return 0;
  }
  if (current_state == STATE_EMPTY) {
    warning ("%s: no design", argv[0]);
    return 0;
  }

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return 0;
  }
  act_design->Print (fp);
  std_close_output (fp);

  return 1;
}

static int process_expand (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_DESIGN)) {
    return 0;
  }
  act_design->Expand ();
  current_state = STATE_EXPANDED;
  return 1;
}

static int process_set_top (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<process>", STATE_EXPANDED)) {
    return 0;
  }
  act_toplevel = act_design->findProcess (argv[1]);
  if (!act_toplevel) {
    fprintf (stderr, "%s: could not find process `%s'\n", argv[0], argv[1]);
    return 0;
  }
  if (!act_toplevel->isExpanded()) {
    int i = 0;
    int angle = 0;
    fprintf (stderr, "%s: process `%s' is not expanded\n", argv[0], argv[1]);
    while (argv[1][i]) {
      if (argv[1][i] == '<') {
	angle++;
      }
      else if (argv[1][i] == '>') {
	angle++;
      }
      i++;
    }
    if (angle != 2) {
      fprintf (stderr, "(Expanded processes have (possibly empty) template specifiers < and >)\n");
    }
    return 0;
  }
  current_state = STATE_TOPLEVEL;
  return 1;
}

static int process_set_mangle (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<string>", state_nonempty)) {
    return 0;
  }
  act_design->mangle (argv[1]);
  return 1;
}


/*************************************************************************
 *
 *  Netlist functions
 *
 *************************************************************************
 */


static ActNetlistPass *getNetlistPass()
{
  ActPass *p = act_design->pass_find ("prs2net");
  ActNetlistPass *np;
  if (p) {
    np = dynamic_cast<ActNetlistPass *> (p);
  }
  else {
    np = new ActNetlistPass (act_design);
  }
  return np;
}

int process_ckt_map (int argc, char **argv)
{
  design_state req[] = { STATE_TOPLEVEL, STATE_ANALYSIS_CELL, STATE_NONE };
  if (!std_argcheck (argc, argv, 1, "", req)) {
    return 0;
  }
  ActNetlistPass *np = getNetlistPass();
  if (!np->completed()) {
    np->run(act_toplevel);
  }
  current_state = STATE_ANALYSIS_CKT;
  return 1;
}

int process_ckt_save_sp (int argc, char **argv)
{
  FILE *fp;
  
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_ANALYSIS_CKT)) {
    return 0;
  }
  ActNetlistPass *np = getNetlistPass();
  Assert (np->completed(), "What?");
  
  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return 0;
  }
  np->Print (fp, act_toplevel);
  std_close_output (fp);
  return 1;
}

/*************************************************************************
 *
 *  Cell generation functions
 *
 *************************************************************************
 */

static ActCellPass *getCellPass()
{
  ActPass *p = act_design->pass_find ("prs2cells");
  ActCellPass *cp;
  if (p) {
    cp = dynamic_cast<ActCellPass *> (p);
  }
  else {
    cp = new ActCellPass (act_design);
  }
  return cp;
}

static int process_cell_map (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_TOPLEVEL)) {
    return 0;
  }
  
  ActCellPass *cp = getCellPass();
  if (!cp->completed()) {
    cp->run ();
  }
  else {
    printf ("%s: cell pass already executed; skipped", argv[0]);
  }
  current_state = STATE_ANALYSIS_CELL;
  return 1;
}

static int process_cell_save (int argc, char **argv)
{
  FILE *fp;
  
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_ANALYSIS_CELL)) {
    return 0;
  }
  ActCellPass *cp = getCellPass();
  if (!cp->completed()) {
    cp->run ();
  }

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return 0;
  }
  cp->Print (fp);
  std_close_output (fp);
  
  return 1;
}




/*------------------------------------------------------------------------
 *
 * All core ACT commands
 *
 *------------------------------------------------------------------------
 */

static struct LispCliCommand act_cmds[] = {
  { NULL, "ACT core functions (use `act:' prefix)", NULL },
  { "read", "read <file> - read in the ACT design", process_read },
  { "merge", "merge <file> - merge in additional ACT file", process_merge },
  { "expand", "expand - expand/elaborate ACT design", process_expand },
  { "save", "save <file> - save current ACT to a file", process_save },
  { "top", "top <process> - set <process> as the design root", process_set_top },
  { "mangle", "mangle <string> - set characters to be mangled on output", process_set_mangle },
  { NULL, "ACT circuits (use `act:' prefix)", NULL },
  { "ckt:map", "ckt:map - generate transistor-level description", process_ckt_map },
  { "ckt:save_sp", "ckt:save_sp <file> - save SPICE netlist to <file>", process_ckt_save_sp },
#if 0  
  { "ckt:save_vnet", "ckt:save_vnet <file> - save Verilog netlist to <file>", process_ckt_save_vnet },
  { "ckt:save_prs", "ckt:save_prs <file> - save flat production rule set to <file>", process_ckt_save_prs },
#endif
  { NULL, "ACT cells (use `act:' prefix)", NULL },
  { "cell:map", "cell:map - map gates to cell library", process_cell_map },
  { "cell:save", "cell:save <file> - save cells to file", process_cell_save },
};



void act_cmds_init (void)
{
  LispCliAddCommands ("act", act_cmds, sizeof (act_cmds)/sizeof (act_cmds[0]));
  act_design = NULL;
  current_state = STATE_EMPTY;
}
