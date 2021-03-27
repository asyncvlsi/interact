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
#include <act/iter.h>
#include <config.h>
#include <lispCli.h>
#include "all_cmds.h"

enum design_state {
		   STATE_NONE,
		   STATE_EMPTY,
		   STATE_DESIGN,
		   STATE_EXPANDED,
		   STATE_ERROR
};

/* -- flow state -- */

struct flow_state {
  unsigned int cell_map:1;
  unsigned int ckt_gen:1;

  design_state s;

  Act *act_design;
  Process *act_toplevel;
};

static flow_state F;


/*************************************************************************
 *
 *
 *  Utility functions
 *
 *
 *************************************************************************
 */
static const char *get_state_str (void)
{
  static char buf[1024];
  const char *s = NULL;
  switch (F.s) {
  case STATE_NONE:
    s = "[none]";
    break;
    
  case STATE_EMPTY:
    s = "[no design]";
    break;

  case STATE_DESIGN:
    s = "[unexpanded design]";
    break;

  case STATE_EXPANDED:
    s = "[expanded design]";
    break;

  case STATE_ERROR:
    s = "[error]";
    break;
    
  }
  snprintf (buf, 1024, "%s", s);
  if (F.ckt_gen) {
    snprintf (buf + strlen (buf), 1000, " ckt:yes");
  }
  else {
    snprintf (buf + strlen (buf), 1000, " ckt:no");
  }

  if (F.cell_map) {
    snprintf (buf + strlen (buf), 1000, " cells:yes");
  }
  else {
    snprintf (buf + strlen (buf), 1000, " cells:no");
  }

  if (F.act_toplevel) {
    snprintf (buf + strlen (buf), 1000, " top:set");
  }
  else {
    snprintf (buf + strlen (buf), 1000, " top:unset");
  }
  return buf;
}


/*--------------------------------------------------------------------------

  Check that # args match, and that we are in a valid flow state
  for the command.

--------------------------------------------------------------------------*/

static int std_argcheck (int argc, char **argv, int argnum, const char *usage,
			 design_state required)
{
  if (argc != argnum) {
    fprintf (stderr, "Usage: %s %s\n", argv[0], usage);
    return 0;
  }
  if (F.s == required) {
    return 1;
  }

  warning ("%s: command failed; incorrect flow state.\n Current flow state: %s", argv[0], get_state_str ());
  fprintf (stderr, "\n");
  return 0;
}


/*--------------------------------------------------------------------------

  Open/close output file, interpreting "-" as stdout

--------------------------------------------------------------------------*/
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



/*------------------------------------------------------------------------
 *
 *   ACT read/write/merge
 *
 *------------------------------------------------------------------------
 */
static int process_read (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EMPTY)) {
    return 0;
  }
  save_to_log (argc, argv, "s");
  
  fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
	     argv[1]);
    return 0;
  }
  fclose (fp);
  F.act_design = new Act (argv[1]);
  new ActApplyPass (F.act_design);
  F.s = STATE_DESIGN;
  return 1;
}

static int process_merge (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_DESIGN)) {
    return 0;
  }
  save_to_log (argc, argv, "s");

  fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
	     argv[1]);
    return 0;
  }
  fclose (fp);
  F.act_design->Merge (argv[1]);
  return 1;
}

static int process_save (int argc, char **argv)
{
  FILE *fp;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <file>\n", argv[0]);
    return 0;
  }
  save_to_log (argc, argv, "s");
  
  if (F.s == STATE_EMPTY) {
    warning ("%s: no design", argv[0]);
    return 0;
  }

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return 0;
  }
  F.act_design->Print (fp);
  std_close_output (fp);

  return 1;
}

static int process_set_mangle (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<string>", F.s)) {
    return 0;
  }
  save_to_log (argc, argv, "s");
  
  F.act_design->mangle (argv[1]);
  return 1;
}


/*------------------------------------------------------------------------
 *
 *  Expand design & specify top level
 *
 *------------------------------------------------------------------------
 */
static int process_expand (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_DESIGN)) {
    return 0;
  }
  save_to_log (argc, argv, NULL);
  
  F.act_design->Expand ();
  F.s = STATE_EXPANDED;
  return 1;
}

static int process_set_top (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<process>", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "s");

  F.act_toplevel = F.act_design->findProcess (argv[1]);
  
  if (!F.act_toplevel) {
    fprintf (stderr, "%s: could not find process `%s'\n", argv[0], argv[1]);
    return 0;
  }
  if (!F.act_toplevel->isExpanded()) {
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
  return 1;
}

static int process_get_top (int argc, char **argv)
{
  char s[1];
  s[0] = '\0';
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, NULL);
  
  if (!F.act_toplevel) {
    LispSetReturnString (s);
  }
  else {
    LispSetReturnString (F.act_toplevel->getName());
  }
  return 3;
}


/*************************************************************************
 *
 *  Netlist functions
 *
 *************************************************************************
 */

static ActNetlistPass *getNetlistPass()
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
    printf ("%s: cell pass already executed; skipped", argv[0]);
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


/*************************************************************************
 *
 * Dynamic passes
 *
 *************************************************************************
 */

static int process_pass_dyn (int argc, char **argv)
{

  if (!std_argcheck (argc, argv, 4, "<dylib> <pass-name> <prefix>",
		     STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "s*");
  
  /* -- special cases here for passes that require other things done -- */
  if (strcmp (argv[2], "net2stk") == 0) {
    ActNetlistPass *np = getNetlistPass ();
    if (!np->completed()) {
      np->run (F.act_toplevel);
      F.ckt_gen = 1;
    }
  }
  
  new ActDynamicPass (F.act_design, argv[2], argv[1], argv[3]);
  return 1;
}

static ActDynamicPass *getDynamicPass (const char *cmd, const char *name)
{
  ActPass *p;
  ActDynamicPass *dp;
  
  p = F.act_design->pass_find (name);
  if (!p) {
    fprintf (stderr, "%s: pass `%s' not found\n", cmd, name);
    return NULL;
  }
  dp = dynamic_cast<ActDynamicPass *> (p);
  if (!dp) {
    fprintf (stderr, "%s: pass `%s' is not dynamically loaded\n", cmd, name);
    return NULL;
  }
  return dp;
}

static int process_pass_set_file_param (int argc, char **argv)
{
  ActDynamicPass *dp;
  FILE *fp;
  int v;
  
  if (!std_argcheck (argc, argv, 4, "<pass-name> <name> <file-handle>",
		     STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "ssi");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return 0; }
  v = atoi (argv[3]);
  fp = sys_get_fileptr (v);
  if (!fp) {
    fprintf (stderr, "%s: file handle <#%d> is not an open file", argv[0], v);
    return 0;
  }
  dp->setParam (argv[2], (void *)fp);
  return 1;
}

static int process_pass_set_int_param (int argc, char **argv)
{
  int v;
  ActDynamicPass *dp;
  
  if (!std_argcheck (argc, argv, 4, "<pass-name> <name> <ival>",
		     STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "ssi");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return 0; }
  v = atoi (argv[3]);
  dp->setParam (argv[2], v);
  return 1;
}

static int process_pass_set_real_param (int argc, char **argv)
{
  double v;
  ActDynamicPass *dp;
  
  if (!std_argcheck (argc, argv, 4, "<pass-name> <name> <rval>",
		     STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "ssf");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return 0; }
  v = atof (argv[3]);
  dp->setParam (argv[2], v);
  return 1;
}

static int process_pass_get_real (int argc, char **argv)
{
  double v;
  ActDynamicPass *dp;

  if (!std_argcheck (argc, argv, 3, "<pass-name> <name>", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "s*");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return 0; }
  LispSetReturnFloat (dp->getRealParam (argv[2]));
  return 4;
}

static int process_pass_get_int (int argc, char **argv)
{
  double v;
  ActDynamicPass *dp;

  if (!std_argcheck (argc, argv, 3, "<pass-name> <name>", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "s*");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return 0; }
  LispSetReturnInt (dp->getIntParam (argv[2]));
  return 2;
}

static int process_pass_run (int argc, char **argv)
{
  ActDynamicPass *dp;
  int v;

  if (!std_argcheck (argc, argv, 3, "<pass-name> <mode>", STATE_EXPANDED)) {
    return 0;
  }
  save_to_log (argc, argv, "si");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return 0; }
  v = atoi (argv[2]);
  if (v < 0) {
    fprintf (stderr, "%s: mode has to be >= 0 (%d)\n", argv[0], v);
    return 0;
  }
  if (v == 0) {
    dp->run (F.act_toplevel);
  }
  else {
    dp->run_recursive (F.act_toplevel, v);
  }
  return 1;
}



/*************************************************************************
 *
 *  Design queries
 *
 *************************************************************************
 */
class ActDesignHier : public ActPass {
 public:
  ActDesignHier (Act *a, FILE *fp) : ActPass (a, "-hier-") { _fp = fp; }
  
  int run(Process *p = NULL) { return ActPass::run (p); }
  void *local_op (Process *p, int mode = 0);
  void *local_op (Channel *c, int mode = 0);
  void *local_op (Data *d, int mode = 0);
  void free_local (void *v) { }
  FILE *_fp;
};

static void _dump_insts (FILE *fp, Scope *sc, int chkself)
{
  ActInstiter it(sc);
  
  for (it = it.begin(); it != it.end(); it++) {
    ValueIdx *vx = (*it);
    if (TypeFactory::isParamType (vx->t)) continue;
    if (chkself && (strcmp (vx->getName(), "self") == 0)) continue;
    fprintf (fp, "  %s", vx->getName());
    if (vx->t->arrayInfo()) {
      vx->t->arrayInfo()->Print (fp);
    }
    fprintf (fp, " : %s\n", vx->t->BaseType()->getName());
  }
}

void *ActDesignHier::local_op (Channel *c, int mode)
{
  fprintf (_fp, "chan %s {\n", c->getName());
  _dump_insts (_fp, c->CurScope(), 1);
  fprintf (_fp, "};\n\n");
  return NULL;
}

void *ActDesignHier::local_op (Data *d, int mode)
{
  fprintf (_fp, "data %s {\n", d->getName());
  _dump_insts (_fp, d->CurScope(), 1);
  fprintf (_fp, "};\n\n");
  return NULL;
}

void *ActDesignHier::local_op (Process *p, int mode)
{
  if (p) {
    fprintf (_fp, "proc %s {\n", p->getName());
  }
  else {
    fprintf (_fp, "::Global {\n");
  }
  _dump_insts (_fp, p ? p->CurScope () : a->Global()->CurScope(), 0);
  
  fprintf (_fp, "};\n\n");
  return NULL;
}

int process_des_insts (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return 0;
  }
  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return 0;
  }

  ActDesignHier *dh = new ActDesignHier (F.act_design, fp);

  dh->run (F.act_toplevel);
  
  std_close_output (fp);

  delete dh;

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
  { "top", "top <process> - set <process> as the design root",
    process_set_top },
  { "gettop", "gettop - returns the name of the top-level process",
    process_get_top },
  { "mangle", "mangle <string> - set characters to be mangled on output",
    process_set_mangle },


  { NULL, "ACT circuit generation (use `act:' prefix)", NULL },
  { "ckt:map", "ckt:map - generate transistor-level description",
    process_ckt_map },
  { "ckt:save_sp", "ckt:save_sp <file> - save SPICE netlist to <file>",
    process_ckt_save_sp },
  { "ckt:mk-nets", "ckt:mk-nets - preparation for DEF generation",
    process_ckt_mknets },
  { "ckt:save_prs", "ckt:save_prs <file> - save flat production rule set to <file> for simulation",
    process_ckt_save_prs },
  { "ckt:save_lvp", "ckt:save_lprs <file> - save flat production rule set to <file> for lvp",
    process_ckt_save_lvp },
  { "ckt:save_sim", "ckt:save_sim <file-prefix> - save flat .sim/.al file",
    process_ckt_save_sim },

  { "ckt:save_v", "ckt:save_v <file> - save Verilog netlist to <file>",
    process_ckt_save_v },
  
#if 0  
  { "ckt:save_vnet", "ckt:save_vnet <file> - save Verilog netlist to <file>", process_ckt_save_vnet },
#endif

  { NULL, "ACT cells (use `act:' prefix)", NULL },
  { "cell:map", "cell:map - map gates to cell library", process_cell_map },
  { "cell:save", "cell:save <file> - save cells to file", process_cell_save },

  
  { NULL, "ACT dynamic passes (use `act:` prefix)", NULL },
  { "pass:load", "pass:load <dylib> <pass-name> <prefix> - load a dynamic ACT pass",
    process_pass_dyn },
  { "pass:set_file", "pass:set_file <pass-name> <name> <filehandle> - set pass parameter to a file",
    process_pass_set_file_param },
  { "pass:set_int", "pass:set_int <pass-name> <name> <ival> - set pass parameter to an integer",
    process_pass_set_int_param },
  { "pass:get_int", "pass:get_int <pass-name> <name> - return int parameter from pass",
    process_pass_get_int },
  { "pass:set_real", "pass:set_real <pass-name> <name> <rval> - set pass parameter to a real number",
    process_pass_set_real_param },
  { "pass:get_real", "pass:get_real <pass-name> <name> - return real parameter from pass",
    process_pass_get_real },
  { "pass:run", "pass:run <pass-name> <mode> - run pass, with mode=0,...",
    process_pass_run },

  { NULL, "ACT design query API (use `act:' prefix)", NULL },
  { "des:insts", "des:insts <file> - save circuit instance hierarchy to file",
    process_des_insts }

};



void act_cmds_init (void)
{
  LispCliAddCommands ("act", act_cmds, sizeof (act_cmds)/sizeof (act_cmds[0]));

  F.s = STATE_EMPTY;
  F.cell_map = 0;
  F.ckt_gen = 0;
  F.act_design = NULL;
  F.act_toplevel = NULL;
}
