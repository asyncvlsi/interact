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
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, NULL);
  
  ActNetlistPass *np = getNetlistPass();
  if (!np->completed()) {
    np->run(F.act_toplevel);
  }
  F.ckt_gen = 1;
  return LISP_RET_TRUE;
}

int process_ckt_save_sp (int argc, char **argv)
{
  FILE *fp;
  
  if (!std_argcheck (argc, argv, 2, "<file>",
		     F.ckt_gen ? STATE_EXPANDED : STATE_ERROR)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  if (!F.act_toplevel) {
    fprintf (stderr, "%s: needs a top-level process specified", argv[0]);
    return LISP_RET_ERROR;
  }
  
  ActNetlistPass *np = getNetlistPass();
  Assert (np->completed(), "What?");
  
  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return LISP_RET_ERROR;
  }
  np->Print (fp, F.act_toplevel);
  std_close_output (fp);
  return LISP_RET_TRUE;
}


int process_ckt_mknets (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "",
		     F.ckt_gen ? STATE_EXPANDED : STATE_ERROR)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, NULL);
  
  ActPass *p = F.act_design->pass_find ("booleanize");
  if (!p) {
    fprintf (stderr, "%s: internal error", argv[0]);
    return LISP_RET_ERROR;
  }
  ActBooleanizePass *bp = dynamic_cast<ActBooleanizePass *>(p);
  if (!bp) {
    fprintf (stderr, "%s: internal error-2", argv[0]);
    return LISP_RET_ERROR;
  }
  bp->createNets (F.act_toplevel);
  return LISP_RET_TRUE;
}

static int _process_ckt_save_flat (int argc, char **argv, int mode)
{
  FILE *fp;

  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return LISP_RET_ERROR;
  }
  act_flatten_prs (F.act_design, fp, F.act_toplevel, mode);
  fclose (fp);
  return LISP_RET_TRUE;
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
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  snprintf (buf, 1024, "%s.sim", argv[1]);
  fps = fopen (buf, "w");
  if (!fps) {
    fprintf (stderr, "%s: could not open file `%s' for writing", argv[0], buf);
    return LISP_RET_ERROR;
  }
  snprintf (buf, 1024, "%s.al", argv[1]);
  fpa = fopen (buf, "w");
  if (!fpa) {
    fprintf (stderr, "%s: could not open file `%s' for writing", argv[0], buf);
    fclose (fps);
    return LISP_RET_ERROR;
  }
  
  act_flatten_sim (F.act_design, fps, fpa, F.act_toplevel);
  
  fclose (fps);
  fclose (fpa);
  
  return LISP_RET_TRUE;
}

static int process_ckt_save_v (int argc, char **argv)
{
  FILE *fp;

  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  if (!F.act_toplevel) {
    fprintf (stderr,  "%s: top-level module is unspecified.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return LISP_RET_ERROR;
  }
  
  act_emit_verilog (F.act_design, fp, F.act_toplevel);
  
  fclose (fp);
  
  return LISP_RET_TRUE;
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
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, NULL);
  
  ActCellPass *cp = getCellPass();
  if (!cp->completed()) {
    list_t *l;
    cp->run (F.act_toplevel);
    l = cp->getNewCells ();
    if (list_length (l) > 0) {
      printf ("WARNING: new cells generated; please update your cell library.\n(Use ckt:cell-save to see the new cells.) New cell names are:\n");
      for (listitem_t *li = list_first (l); li; li = list_next (li)) {
	printf ("   %s\n", ((Process *)list_value (li))->getName());
      }
    }
  }
  else {
    printf ("%s: cell pass already executed; skipped\n", argv[0]);
  }
  F.cell_map = 1;
  return LISP_RET_TRUE;
}

static int process_cell_save (int argc, char **argv)
{
  FILE *fp;
  
  if (!std_argcheck (argc, argv, 2, "<file>",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");
  
  ActCellPass *cp = getCellPass();
  if (!cp->completed()) {
    cp->run (F.act_toplevel);
  }

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return LISP_RET_ERROR;
  }
  cp->Print (fp);
  std_close_output (fp);
  
  return LISP_RET_TRUE;
}


static int process_add_buffer (int argc, char **argv)
{
  FILE *fp;
  design_state tmp_s;

  tmp_s = F.s;
  if (F.s == STATE_DIRTY) {
    F.s = STATE_EXPANDED;
  }

  if (!std_argcheck (argc, argv, 5, "<proc> <inst> <pin> <buf>",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    F.s = tmp_s;
    return LISP_RET_ERROR;
  }
  F.s = tmp_s;

  ActCellPass *cp = getCellPass();
  Assert (cp && cp->completed(), "What?");

  Process *proc = F.act_design->findProcess (argv[1]);
  if (!proc) {
    fprintf (stderr, "%s: could not find process `%s'\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }

  if (!proc->isExpanded()) {
    fprintf (stderr, "%s: Process `%s' is not expanded\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }

  ActId *tmp = ActId::parseId (argv[2]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[2]);
    return LISP_RET_ERROR;
  }

  if (tmp->Rest() || tmp->arrayInfo()) {
    delete tmp;
    fprintf (stderr, "%s: `%s' needs to be a simple instance name\n",
	     argv[0], argv[2]);
    return LISP_RET_ERROR;
  }
  delete tmp;

  tmp = ActId::parseId (argv[3]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[3]);
    return LISP_RET_ERROR;
  }

  if (tmp->Rest()) {
    delete tmp;
    fprintf (stderr, "%s: `%s' needs to be a simple pin name\n",
	     argv[0], argv[3]);
    return LISP_RET_ERROR;
  }

  Process *buftype = F.act_design->findProcess (argv[4], true);
  if (!buftype) {
    fprintf (stderr, "%s: could not find buffer type `%s'\n", argv[0], argv[4]);
    return LISP_RET_ERROR;
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
    F.s = STATE_DIRTY;
    return LISP_RET_STRING;
  }
  else {
    return LISP_RET_ERROR;
  }
}

static int process_edit_cell (int argc, char **argv)
{
  FILE *fp;
  design_state tmp_s;

  tmp_s = F.s;
  if (F.s == STATE_DIRTY) {
    F.s = STATE_EXPANDED;
  }

  if (!std_argcheck (argc, argv, 4, "<proc> <inst> <newcell>",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    F.s = tmp_s;
    return LISP_RET_ERROR;
  }
  F.s = tmp_s;

  ActCellPass *cp = getCellPass();
  Assert (cp && cp->completed(), "What?");

  Process *proc = F.act_design->findProcess (argv[1]);
  if (!proc) {
    fprintf (stderr, "%s: could not find process `%s'\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }

  if (!proc->isExpanded()) {
    fprintf (stderr, "%s: Process `%s' is not expanded\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }

  ActId *tmp = ActId::parseId (argv[2]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[2]);
    return LISP_RET_ERROR;
  }

  if (tmp->Rest() || tmp->arrayInfo()) {
    delete tmp;
    fprintf (stderr, "%s: `%s' needs to be a simple instance name\n",
	     argv[0], argv[2]);
    return LISP_RET_ERROR;
  }
  delete tmp;

  Process *celltype = F.act_design->findProcess (argv[3]);
  if (!celltype) {
    fprintf (stderr, "%s: could not find cell type `%s'\n", argv[0], argv[3]);
    return LISP_RET_ERROR;
  }

  if (!celltype->isExpanded()) {
    celltype = celltype->Expand (ActNamespace::Global(),
				 celltype->CurScope(), 0, NULL);
  }
  Assert (celltype->isExpanded(), "What?");

  if (proc->updateInst (argv[2], celltype)) {
    F.s = STATE_DIRTY;
    save_to_log (argc, argv, "s*");
    return LISP_RET_TRUE;
  }
  else {
    return LISP_RET_ERROR;
  }
}


static int process_update_cell (int argc, char **argv)
{
  design_state tmp_s;

  tmp_s = F.s;
  if (F.s == STATE_DIRTY) {
    F.s = STATE_EXPANDED;
  }

  if (!std_argcheck (argc, argv, 1, "",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    F.s = tmp_s;
    return LISP_RET_ERROR;
  }
  F.s = tmp_s;

  if (tmp_s == STATE_DIRTY) {
    ActPass::refreshAll (F.act_design, F.act_toplevel);
  }
  save_to_log (argc, argv, "s");
  F.s = STATE_EXPANDED;
  
  return LISP_RET_TRUE;
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
    process_edit_cell },

  { "cell-update", "- take the design back to the clean state",
    process_update_cell }
};

void ckt_cmds_init (void)
{
  LispCliAddCommands ("ckt", ckt_cmds, sizeof (ckt_cmds)/sizeof (ckt_cmds[0]));
}
