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
#include <act/passes.h>
#include <lispCli.h>
#include "all_cmds.h"
#include "flow.h"


/*************************************************************************
 *
 *  Netlist functions
 *
 *************************************************************************
 */

ActNetlistPass *getNetlistPass()
{
  ActPass *p = F.act_design->pass_find ("prs2net");
  ActNetlistPass *np;
  if (p) {
    np = dynamic_cast<ActNetlistPass *> (p);
  }
  else {
    np = new ActNetlistPass (F.act_design);
  }
  return np;
}

int process_ckt_map (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, NULL);
  
  ActNetlistPass *np = getNetlistPass();
  if (!np->completed()) {
    np->run(F.act_toplevel);
  }
  F.ckt_gen = 1;
  return 1;
}

int process_ckt_save_sp (int argc, char **argv)
{
  FILE *fp;
  
  if (!std_argcheck (argc, argv, 2, "<file>",
		     F.ckt_gen ? STATE_EXPANDED : STATE_ERROR)) {
    return 0;
  }
  save_to_log (argc, argv, "s");

  if (!F.act_toplevel) {
    fprintf (stderr, "%s: needs a top-level process specified", argv[0]);
    return 0;
  }
  
  ActNetlistPass *np = getNetlistPass();
  Assert (np->completed(), "What?");
  
  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return 0;
  }
  np->Print (fp, F.act_toplevel);
  std_close_output (fp);
  return 1;
}


int process_ckt_mknets (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "",
		     F.ckt_gen ? STATE_EXPANDED : STATE_ERROR)) {
    return 0;
  }
  save_to_log (argc, argv, NULL);
  
  ActPass *p = F.act_design->pass_find ("booleanize");
  if (!p) {
    fprintf (stderr, "%s: internal error", argv[0]);
    return 0;
  }
  ActBooleanizePass *bp = dynamic_cast<ActBooleanizePass *>(p);
  if (!bp) {
    fprintf (stderr, "%s: internal error-2", argv[0]);
    return 0;
  }
  bp->createNets (F.act_toplevel);
  return 1;
}

static int _process_ckt_save_flat (int argc, char **argv, int mode)
{
  FILE *fp;

  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "s");

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return 0;
  }
  act_flatten_prs (F.act_design, fp, F.act_toplevel, mode);
  fclose (fp);
  return 1;
}

static int process_ckt_save_prs (int argc, char **argv)
{
  return _process_ckt_save_flat (argc, argv, 0);
}

static int process_ckt_save_lvp (int argc, char **argv)
{
  return _process_ckt_save_flat (argc, argv, 1);
}

static int process_ckt_save_sim (int argc, char **argv)
{
  FILE *fps, *fpa;
  char buf[1024];

  if (!std_argcheck (argc, argv, 2, "<file-prefix>",
		     F.ckt_gen ? STATE_EXPANDED : STATE_ERROR)) {
    return 0;
  }
  save_to_log (argc, argv, "s");

  snprintf (buf, 1024, "%s.sim", argv[1]);
  fps = fopen (buf, "w");
  if (!fps) {
    fprintf (stderr, "%s: could not open file `%s' for writing", argv[0], buf);
    return 0;
  }
  snprintf (buf, 1024, "%s.al", argv[1]);
  fpa = fopen (buf, "w");
  if (!fpa) {
    fprintf (stderr, "%s: could not open file `%s' for writing", argv[0], buf);
    fclose (fps);
    return 0;
  }
  
  act_flatten_sim (F.act_design, fps, fpa, F.act_toplevel);
  
  fclose (fps);
  fclose (fpa);
  
  return 1;
}

static int process_ckt_save_v (int argc, char **argv)
{
  FILE *fp;

  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "s");

  if (!F.act_toplevel) {
    fprintf (stderr,  "%s: top-level module is unspecified.\n", argv[0]);
    return 0;
  }

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return 0;
  }
  
  act_emit_verilog (F.act_design, fp, F.act_toplevel);
  
  fclose (fp);
  
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
  ActPass *p = F.act_design->pass_find ("prs2cells");
  ActCellPass *cp;
  if (p) {
    cp = dynamic_cast<ActCellPass *> (p);
  }
  else {
    cp = new ActCellPass (F.act_design);
  }
  return cp;
}

static int process_cell_map (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, NULL);
  
  ActCellPass *cp = getCellPass();
  if (!cp->completed()) {
    cp->run ();
  }
  else {
    printf ("%s: cell pass already executed; skipped\n", argv[0]);
  }
  F.cell_map = 1;
  return 1;
}

static int process_cell_save (int argc, char **argv)
{
  FILE *fp;
  
  if (!std_argcheck (argc, argv, 2, "<file>",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    return 0;
  }
  save_to_log (argc, argv, "s");
  
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


static int process_add_buffer (int argc, char **argv)
{
  FILE *fp;

  if (!std_argcheck (argc, argv, 5, "<proc> <inst> <pin> <buf>",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    return 0;
  }

  ActCellPass *cp = getCellPass();
  Assert (cp && cp->completed(), "What?");

  Process *proc = F.act_design->findProcess (argv[1]);
  if (!proc) {
    fprintf (stderr, "%s: could not find process `%s'\n", argv[0], argv[1]);
    return 0;
  }

  if (!proc->isExpanded()) {
    fprintf (stderr, "%s: Process `%s' is not expanded\n", argv[0], argv[1]);
    return 0;
  }

  ActId *tmp = ActId::parseId (argv[2]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[2]);
    return 0;
  }

  if (tmp->Rest() || tmp->arrayInfo()) {
    delete tmp;
    fprintf (stderr, "%s: `%s' needs to be a simple instance name\n",
	     argv[0], argv[2]);
    return 0;
  }
  delete tmp;

  tmp = ActId::parseId (argv[3]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[3]);
    return 0;
  }

  if (tmp->Rest()) {
    delete tmp;
    fprintf (stderr, "%s: `%s' needs to be a simple pin name\n",
	     argv[0], argv[3]);
    return 0;
  }

  Process *buftype = F.act_design->findProcess (argv[4]);
  if (!buftype) {
    fprintf (stderr, "%s: could not find buffer type `%s'\n", argv[0], argv[4]);
    return 0;
  }

  if (!buftype->isExpanded()) {
    buftype = buftype->Expand (ActNamespace::Global(),
			       buftype->CurScope(), 0, NULL);
  }
  Assert (buftype->isExpanded(), "What?");

  const char *nm;
  if ((nm = proc->addBuffer (argv[2], tmp, buftype))) {
    save_to_log (argc, argv, "s*");
    LispSetReturnString (nm);
    return 3;
  }
  else {
    return 0;
  }
}


static int process_edit_cell (int argc, char **argv)
{
  FILE *fp;

  if (!std_argcheck (argc, argv, 4, "<proc> <inst> <newcell>",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    return 0;
  }

  ActCellPass *cp = getCellPass();
  Assert (cp && cp->completed(), "What?");

  Process *proc = F.act_design->findProcess (argv[1]);
  if (!proc) {
    fprintf (stderr, "%s: could not find process `%s'\n", argv[0], argv[1]);
    return 0;
  }

  if (!proc->isExpanded()) {
    fprintf (stderr, "%s: Process `%s' is not expanded\n", argv[0], argv[1]);
    return 0;
  }

  ActId *tmp = ActId::parseId (argv[2]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[2]);
    return 0;
  }

  if (tmp->Rest() || tmp->arrayInfo()) {
    delete tmp;
    fprintf (stderr, "%s: `%s' needs to be a simple instance name\n",
	     argv[0], argv[2]);
    return 0;
  }
  delete tmp;

  Process *celltype = F.act_design->findProcess (argv[3]);
  if (!celltype) {
    fprintf (stderr, "%s: could not find cell type `%s'\n", argv[0], argv[3]);
    return 0;
  }

  if (!celltype->isExpanded()) {
    celltype = celltype->Expand (ActNamespace::Global(),
				 celltype->CurScope(), 0, NULL);
  }
  Assert (celltype->isExpanded(), "What?");

  if (proc->updateInst (argv[2], celltype)) {
    save_to_log (argc, argv, "s*");
    return 1;
  }
  else {
    return 0;
  }
}



static struct LispCliCommand ckt_cmds[] = {
  { NULL, "ACT circuit generation", NULL },
  { "map", "- generate transistor-level description",
    process_ckt_map },
  { "save-sp", "<file> - save SPICE netlist to <file>",
    process_ckt_save_sp },
  { "mk-nets", "- preparation for DEF generation",
    process_ckt_mknets },
  { "save-prs", "<file> - save flat production rule set to <file> for simulation",
    process_ckt_save_prs },
  { "save-lvp", "<file> - save flat production rule set to <file> for lvp",
    process_ckt_save_lvp },
  { "save-sim", "<file-prefix> - save flat .sim/.al file",
    process_ckt_save_sim },
  { "save-vnet", "<file> - save Verilog netlist to <file>",
    process_ckt_save_v },
  

  { NULL, "ACT cell mapping and editing", NULL },
  { "cell-map", "- map gates to cell library", process_cell_map },
  { "cell-save", "<file> - save cells to file", process_cell_save },
  { "cell-addbuf", "<proc> <inst> <pin> <buf> - add buffer to the pin within the process",
    process_add_buffer },
  { "cell-edit", "<proc> <inst> <newcell> - replace cell for instance within <proc>",
    process_edit_cell }
};

void ckt_cmds_init (void)
{
  LispCliAddCommands ("ckt", ckt_cmds, sizeof (ckt_cmds)/sizeof (ckt_cmds[0]));
}
