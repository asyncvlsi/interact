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
#include <common/config.h>
#include <lispCli.h>
#include "all_cmds.h"
#include "ptr_manager.h"

#include "flow.h"

flow_state F;



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
  { NULL, "ACT core functions", NULL },
  { "read", "act:read <file> - read in the ACT design", process_read },
  { "merge", "act:merge <file> - merge in additional ACT file", process_merge },
  { "expand", "act:expand - expand/elaborate ACT design", process_expand },
  { "save", "act:save <file> - save current ACT to a file", process_save },
  { "top", "act:top <process> - set <process> as the design root",
    process_set_top },
  { "gettop", "act:gettop - returns the name of the top-level process",
    process_get_top },
  { "mangle", "act:mangle <string> - set characters to be mangled on output",
    process_set_mangle },

  { NULL, "ACT design query API", NULL },
  { "save-insts", "act:save-insts <file> - save circuit instance hierarchy to file",
    process_des_insts },

  { NULL, "ACT dynamic passes", NULL },
  { "pass:load", "act:pass:load <dylib> <pass-name> <prefix> - load a dynamic ACT pass",
    process_pass_dyn },
  { "pass:set_file", "act:pass:set_file <pass-name> <name> <filehandle> - set pass parameter to a file",
    process_pass_set_file_param },
  { "pass:set_int", "act:pass:set_int <pass-name> <name> <ival> - set pass parameter to an integer",
    process_pass_set_int_param },
  { "pass:get_int", "act:pass:get_int <pass-name> <name> - return int parameter from pass",
    process_pass_get_int },
  { "pass:set_real", "act:pass:set_real <pass-name> <name> <rval> - set pass parameter to a real number",
    process_pass_set_real_param },
  { "pass:get_real", "act:pass:get_real <pass-name> <name> - return real parameter from pass",
    process_pass_get_real },
  { "pass:run", "act:pass:run <pass-name> <mode> - run pass, with mode=0,...",
    process_pass_run }
};



void act_cmds_init (void)
{
  LispCliAddCommands ("act", act_cmds, sizeof (act_cmds)/sizeof (act_cmds[0]));
}
