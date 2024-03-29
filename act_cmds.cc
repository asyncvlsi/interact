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
#include <act/tech.h>
#include <act/passes.h>
#include <act/iter.h>
#include <common/config.h>
#include <lispCli.h>
#include "all_cmds.h"
#include "ptr_manager.h"

#include "flow.h"

flow_state F;

static ActId *my_parse_id (const char *name)
{
  return ActId::parseId (name);
}

/*------------------------------------------------------------------------
 * 
 *  Set ACT pint
 *
 *------------------------------------------------------------------------
 */
static int process_defpint (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 3, "<name> <int>", STATE_EMPTY)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "si");

  act_add_global_pint (argv[1], atoi (argv[2]));
  
  return LISP_RET_TRUE;
}

/*------------------------------------------------------------------------
 * 
 *  Set ACT pbool
 *
 *------------------------------------------------------------------------
 */
static int process_defpbool (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 3, "<name> #t|#f", STATE_EMPTY)) {
    return LISP_RET_ERROR;
  }

  if (strcmp (argv[2], "#t") == 0) {
    act_add_global_pbool (argv[1], 1);
  }
  else if (strcmp (argv[2], "#f") == 0) {
    act_add_global_pbool (argv[1], 0);
  }
  else {
    fprintf (stderr, "%s: pbool value must be #t or #f\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "sb");
  return LISP_RET_TRUE;
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
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");
  
  fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
	     argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);
  Assert (F.act_design, "What?");
  F.act_design->Merge (argv[1]);
  F.s = STATE_DESIGN;
  return LISP_RET_TRUE;
}

static int process_merge (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_DESIGN)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
	     argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);
  F.act_design->Merge (argv[1]);
  return LISP_RET_TRUE;
}

static int process_save (int argc, char **argv)
{
  FILE *fp;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <file>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");
  
  if (F.s == STATE_EMPTY) {
    warning ("%s: no design", argv[0]);
    return LISP_RET_ERROR;
  }

  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return LISP_RET_ERROR;
  }
  F.act_design->Print (fp);
  std_close_output (fp);

  return LISP_RET_TRUE;
}

static int process_set_mangle (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<string>", F.s)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");
  
  F.act_design->mangle (argv[1]);
  return LISP_RET_TRUE;
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
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, NULL);
  
  F.act_design->Expand ();
  F.s = STATE_EXPANDED;
  return LISP_RET_TRUE;
}


/*------------------------------------------------------------------------
 *
 *  Localize a global signal
 *
 *------------------------------------------------------------------------
 */
static int process_localize (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<sig>", STATE_DESIGN)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, NULL);

  if (F.act_design->LocalizeGlobal (argv[1])) {
    return LISP_RET_TRUE;
  }
  else {
    return LISP_RET_ERROR;
  }
}


static int process_set_top (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<process>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  F.act_toplevel = F.act_design->findProcess (argv[1]);

  if (!F.act_toplevel) {
    int j, i = 0;
    while (argv[1][i] && argv[1][i] != '<') {
      i++;
    }
    if (argv[1][i]) {
      Process *unexp;

      Assert (argv[1][i] == '<', "Hmm?");
      argv[1][i] = '\0';
      unexp = F.act_design->findProcess (argv[1]);
      if (!unexp) {
	fprintf (stderr, "%s: could not find process `%s'\n", argv[0],
		 argv[1]);
	return LISP_RET_ERROR;
      }
      argv[1][i] = '<';

      int nargs = 0;
      i++;
      j = i;
      if (argv[1][j] != '>') {
	nargs = 1;
	while (argv[1][j] && argv[1][j] != '>') {
	  if (argv[1][j] == ',') {
	    nargs++;
	  }
	  j++;
	}
      }
      if (argv[1][j] == '>' && argv[1][j+1] == '\0') {
	/* try and construct template arguments */
	inst_param *u;
	if (nargs > 0) {
	  MALLOC (u, inst_param, nargs);
	  for (int k=0; k < nargs; k++) {
	    u[k].isatype = 0;
	    u[k].u.tp = NULL;
	  }
	}
	else {
	  u = NULL;
	}
	int pos, k = 0;
	while (nargs > 0) {
	  pos = i;
	  if (strncmp (&argv[1][i], "true", 4) == 0) {
	    u[k].u.tp = new AExpr (const_expr_bool (1));
	  }
	  else if (strncmp (&argv[1][i], "false", 5) == 0) {
	    u[k].u.tp = new AExpr (const_expr_bool (0));
	  }
	  else {
	    while (i < j && argv[1][i] != ',') {
	      if (isdigit (argv[1][i])) {
		i++;
	      }
	      else {
		fprintf (stderr, "%s: could not parse template parameters in `%s'\n",
			 argv[0], argv[1]);
		for (int l=0; l < nargs; l++) {
		  if (u[l].u.tp) {
		    delete u[l].u.tp;
		  }
		}
		FREE (u);
		return LISP_RET_ERROR;
	      }
	    }
	    int val;
	    if (sscanf (&argv[1][pos], "%d", &val) != 1) {
		fprintf (stderr, "%s: could not parse template parameters in `%s'\n",
			 argv[0], argv[1]);
		for (int l=0; l < nargs; l++) {
		  if (u[l].u.tp) {
		    delete u[l].u.tp;
		  }
		}
		FREE (u);
		return LISP_RET_ERROR;
	    }
	    u[k].u.tp = new AExpr (const_expr (val));
	    i++;
	  }
	  k++;
	  nargs--;
	}
	/* -- here -- */

	F.act_toplevel = unexp->Expand (F.act_design->Global(),
					F.act_design->Global()->CurScope(),
					nargs,
					u);

	if (F.cell_map || F.ckt_gen) {
	  if (F.cell_map) {
	    ActCellPass *cp =
	      dynamic_cast<ActCellPass *>(F.act_design->pass_find ("prs2cells"));
	    delete cp;
	    cp = new ActCellPass (F.act_design);
	    cp->run (F.act_toplevel);
	  }
	  if (!F.cell_map && F.ckt_gen) {
	    ActNetlistPass *np =
	      dynamic_cast<ActNetlistPass *>(F.act_design->pass_find ("prs2net"));
	    delete np;
	    np = new ActNetlistPass (F.act_design);
	    np->run (F.act_toplevel);
	  }
	}

	for (int l=0; l < nargs; l++) {
	  if (u[l].u.tp) {
	    delete u[l].u.tp;
	  }
	}
	FREE (u);
      }
    }
  }
  else {
    if (!F.act_toplevel->isExpanded()) {
      if (F.act_toplevel->getRemainingParams() != 0) {
	fprintf (stderr, "%s: unexpanded process `%s' specified, but it requires template parameters\n", argv[0], argv[1]);
	return LISP_RET_ERROR;
      }
      F.act_toplevel =
	F.act_toplevel->Expand (ActNamespace::Global(),
				F.act_toplevel->CurScope(), 0, NULL);
    }
  }
  
  if (!F.act_toplevel) {
    fprintf (stderr, "%s: could not find process `%s'\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
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
    return LISP_RET_ERROR;
  }
  return LISP_RET_TRUE;
}

static int process_get_top (int argc, char **argv)
{
  char s[1];
  s[0] = '\0';
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, NULL);
  
  if (!F.act_toplevel) {
    LispSetReturnString (s);
  }
  else {
    LispSetReturnString (F.act_toplevel->getName());
  }
  return LISP_RET_STRING;
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
		     STATE_ANY)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s*");
  
#if 0  
  /* -- special cases here for passes that require other things done -- */
  /* 
     We no longer need this as this pass should be automatically
     called through dependencies.
  */
  if (strcmp (argv[2], "net2stk") == 0) {
    ActNetlistPass *np = getNetlistPass ();
    if (!np->completed()) {
      np->run (F.act_toplevel);
      F.ckt_gen = 1;
    }
  }
#endif
  
  ActDynamicPass *dp =
    new ActDynamicPass (F.act_design, argv[2], argv[1], argv[3]);
  
  if (dp->loaded()) {
    return LISP_RET_TRUE;
  }
  else {
    delete dp;
    return LISP_RET_ERROR;
  }
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
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "ssi");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return LISP_RET_ERROR; }
  v = atoi (argv[3]);
  fp = sys_get_fileptr (v);
  if (!fp) {
    fprintf (stderr, "%s: file handle <#%d> is not an open file", argv[0], v);
    return LISP_RET_ERROR;
  }
  dp->setParam (argv[2], (void *)fp);
  return LISP_RET_TRUE;
}

static int process_pass_set_string_param (int argc, char **argv)
{
  ActDynamicPass *dp;
  char *s;

  if (!std_argcheck (argc, argv, 4, "<pass-name> <name> <string>",
		     STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "sss");

  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return LISP_RET_ERROR; }

  if ((s = (char *)dp->getPtrParam (argv[2]))) {
    FREE (s);
  }
  dp->setParam (argv[2], (void *)Strdup (argv[3]));
  return LISP_RET_TRUE;
}


static int process_pass_set_int_param (int argc, char **argv)
{
  int v;
  ActDynamicPass *dp;
  
  if (!std_argcheck (argc, argv, 4, "<pass-name> <name> <ival>",
		     STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "ssi");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return LISP_RET_ERROR; }
  v = atoi (argv[3]);
  dp->setParam (argv[2], v);
  return LISP_RET_TRUE;
}

static int process_pass_set_real_param (int argc, char **argv)
{
  double v;
  ActDynamicPass *dp;
  
  if (!std_argcheck (argc, argv, 4, "<pass-name> <name> <rval>",
		     STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "ssf");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return LISP_RET_ERROR; }
  v = atof (argv[3]);
  dp->setParam (argv[2], v);
  return LISP_RET_TRUE;
}

static int process_pass_get_real (int argc, char **argv)
{
  double v;
  ActDynamicPass *dp;

  if (!std_argcheck (argc, argv, 3, "<pass-name> <name>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s*");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return LISP_RET_ERROR; }
  LispSetReturnFloat (dp->getRealParam (argv[2]));
  return LISP_RET_FLOAT;
}

static int process_pass_get_int (int argc, char **argv)
{
  double v;
  ActDynamicPass *dp;

  if (!std_argcheck (argc, argv, 3, "<pass-name> <name>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s*");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return LISP_RET_ERROR; }
  LispSetReturnInt (dp->getIntParam (argv[2]));
  return LISP_RET_INT;
}

static int process_pass_run (int argc, char **argv)
{
  ActDynamicPass *dp;
  int v;

  if (!std_argcheck (argc, argv, 3, "<pass-name> <mode>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "si");
  
  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return LISP_RET_ERROR; }
  v = atoi (argv[2]);
  if (v < 0) {
    fprintf (stderr, "%s: mode has to be >= 0 (%d)\n", argv[0], v);
    return LISP_RET_ERROR;
  }
  if (v == 0) {
    dp->run (F.act_toplevel);
  }
  else {
    dp->run_recursive (F.act_toplevel, v);
  }
  return LISP_RET_TRUE;
}

static int process_pass_runcmd (int argc, char **argv)
{
  ActDynamicPass *dp;
  int v;

  if (!std_argcheck (argc, argv, 3, "<pass-name> <cmd>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  dp = getDynamicPass (argv[0], argv[1]);
  if (!dp) { return LISP_RET_ERROR; }
  v = dp->runcmd (argv[2]);
  if (v == 1) {
    return LISP_RET_TRUE;
  }
  else if (v == 0) {
    return LISP_RET_FALSE;
  }
  else {
    return LISP_RET_ERROR;
  }
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

/*------------------------------------------------------------------------
 *
 * Save design hierarchy to a file
 *
 *------------------------------------------------------------------------
 */
int process_des_insts (int argc, char **argv)
{
  FILE *fp;
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  ActDesignHier *dh = new ActDesignHier (F.act_design, fp);

  dh->run (F.act_toplevel);
  
  std_close_output (fp);

  delete dh;

  return LISP_RET_TRUE;
}


/*------------------------------------------------------------------------
 *
 *  Display type signature
 *
 *------------------------------------------------------------------------
 */
static int process_type_info (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<typename>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  UserDef *u = F.act_design->findUserdef (argv[1]);

  if (!u) {
    fprintf (stderr, "%s: could not find user-defined type `%s'", argv[0],
	     argv[1]);
    return LISP_RET_ERROR;
  }

  if (TypeFactory::isProcessType (u)) {
    u->PrintHeader (stdout, "defproc");
  }
  else if (TypeFactory::isChanType (u)) {
    u->PrintHeader (stdout, "defchan");
  }
  else if (TypeFactory::isDataType (u)) {
    u->PrintHeader (stdout, "deftype");
  }
  else if (TypeFactory::isFuncType (u)) {
    Function *f = dynamic_cast<Function *> (u);
    Assert (f, "Hmm");
    u->PrintHeader (stdout, "function");
    printf (" : ");
    f->getRetType()->Print (stdout);
  }
  else {
    fatal_error ("What happened?");
  }
  printf ("\n");

  if (u->isExpanded()) {
    printf (" --> expanded type\n");
  }
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}


static int process_namespace_list (int argc, char **argv)
{
  if (argc == 2) {
    if (!std_argcheck (argc, argv, 2, "[<ns>]", STATE_EXPANDED)) {
      return LISP_RET_ERROR;
    }
  }
  else {
    if (!std_argcheck (argc, argv, 1, "[<ns>]", STATE_EXPANDED)) {
      return LISP_RET_ERROR;
    }
  }
  ActNamespace *g = F.act_design->Global();
  if (argc == 2) {
    g = F.act_design->findNamespace (argv[1]);
    if (!g) {
      fprintf (stderr, "%s: could not find namespace `%s'\n", argv[0], argv[1]);
      return LISP_RET_ERROR;
    }
  }
  list_t *l = g->getSubNamespaces();
  listitem_t *li;

  save_to_log (argc, argv, "s");
  
  LispSetReturnListStart ();

  for (li = list_first (l); li; li = list_next (li)) {
    char *tmp = (char *) list_value (li);
    LispAppendReturnString (tmp);
  }

  LispSetReturnListEnd ();

  list_free (l);
  
  return LISP_RET_LIST;
}

static int process_getproc (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, argc == 1 ? 1 : 2, "[<ns>]", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  ActNamespace *g = F.act_design->Global();
  if (argc == 2) {
    g = F.act_design->findNamespace (argv[1]);
    if (!g) {
      fprintf (stderr, "%s: could not find namespace `%s'\n", argv[0], argv[1]);
      return LISP_RET_ERROR;
    }
  }
  list_t *l;
  char x = argv[0][strlen(argv[0])-1];
  
  if (x == 'c') {
    l = g->getProcList ();
  }
  else if (x == 'a') {
    l = g->getDataList ();
  }
  else {
    l = g->getChanList ();
  }

  save_to_log (argc, argv, "s");
  
  listitem_t *li;

  LispSetReturnListStart ();

  for (li = list_first (l); li; li = list_next (li)) {
    char *tmp = (char *) list_value (li);
    LispAppendReturnString (tmp);
  }

  LispSetReturnListEnd ();

  list_free (l);
  
  return LISP_RET_LIST;
}


static int process_show_type (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, argc == 2 ? 2 : 3, "[<proc>] <name>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  Process *x;
  if (argc == 2) {
    if (!F.act_toplevel) {
      fprintf (stderr, "%s: default process is -top-level-, but that is not set\n", argv[0]);
      return LISP_RET_ERROR;
    }
    x = F.act_toplevel;
  }
  else {
    x = F.act_design->findProcess (argv[1]);
    if (!x) {
      fprintf (stderr, "%s: process `%s' not found\n", argv[0], argv[1]);
      return LISP_RET_ERROR;
    }
  }

  ActId *tmp = my_parse_id (argv[argc-1]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[argc-1]);
  }

  Array *ar;
  InstType *it;
  it = x->CurScope()->FullLookup (tmp, &ar);
  if (!it) {
    fprintf (stderr, "%s: could not find id `%s' in `%s'\n", argv[0], argv[argc-1], x->getName());
    return LISP_RET_ERROR;
  }

  printf ("In `%s', %s: ", x->getName(), argv[argc-1]);
  if (ar) {
    printf ("deref: ");
    ar->Print (stdout);
    printf ("; ");
  }
  it->Print (stdout);
  printf ("\n");
    
  save_to_log (argc, argv, "ss");
  
  return LISP_RET_TRUE;
}


/*------------------------------------------------------------------------
 *
 * All core ACT commands
 *
 *------------------------------------------------------------------------
 */

static struct LispCliCommand act_cmds[] = {
  { NULL, "ACT core functions", NULL },
  { "defpint", "<name> <int> - create and set a global pint prior to reading",
    process_defpint },
  { "defpbool", "<name> #t|#f - create and set a global pbool prior to reading",
    process_defpbool },
  { "read", "<file> - read in the ACT design", process_read },
  { "merge", "<file> - merge in additional ACT file", process_merge },
  { "localize", "<sig> - localize global signal <sig> in the design; must be called before expansion.",
    process_localize },
  { "expand", "- expand/elaborate ACT design", process_expand },
  { "save", "<file> - save current ACT to a file", process_save },
  { "top", "<process> - set <process> as the design root",
    process_set_top },
  { "gettop", "- returns the name of the top-level process",
    process_get_top },
  { "mangle", "<string> - set characters to be mangled on output",
    process_set_mangle },

  { NULL, "ACT design query API", NULL },
  { "save-insts", "<file> - save circuit instance hierarchy to file",
    process_des_insts },
  { "typesig", "<name> - print type signature of a user-defined type",
    process_type_info },
  { "getns", "[<ns>] - returns list of sub-namespaces within <ns>",
    process_namespace_list },
  { "getproc", "[<ns>] - returns list of process types in <ns>",
    process_getproc },
  { "getdata", "[<ns>] - returns list of data types in <ns>",
    process_getproc },
  { "getchan", "[<ns>] - returns list of channel types in <ns>",
    process_getproc },

  { "display-type", "[<proc>] <name> - display type of instance <name> in <proc>",
    process_show_type },

  { NULL, "ACT dynamic pass management", NULL },
  { "pass:load", "<dylib> <pass-name> <prefix> - load a dynamic ACT pass",
    process_pass_dyn },
  { "pass:set_file", "<pass-name> <name> <filehandle> - set pass parameter to a file",
    process_pass_set_file_param },
  { "pass:set_int", "<pass-name> <name> <ival> - set pass parameter to an integer",
    process_pass_set_int_param },
  { "pass:set_string", "<pass-name> <string> - set pass parameter to a string",
    process_pass_set_string_param },
  { "pass:get_int", "<pass-name> <name> - return int parameter from pass",
    process_pass_get_int },
  { "pass:set_real", "<pass-name> <name> <rval> - set pass parameter to a real number",
    process_pass_set_real_param },
  { "pass:get_real", "<pass-name> <name> - return real parameter from pass",
    process_pass_get_real },
  { "pass:run", "<pass-name> <mode> - run pass, with mode=0,...",
    process_pass_run },

  { "pass:runcmd", "<pass-name> <cmd> - run pass command",
    process_pass_runcmd }
};


static int process_tech_uscale (int argc, char **argv)
{
  if (argc != 1) {
    fprintf (stderr, "Usage: %s\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (!Technology::T) {
    fprintf (stderr, "%s: technology information not initialized!", argv[0]);
    return LISP_RET_ERROR;
  }
  LispSetReturnFloat (Technology::T->scale/1000.0);
  return LISP_RET_FLOAT;
}

static int process_tech_scale (int argc, char **argv)
{
  if (argc != 1) {
    fprintf (stderr, "Usage: %s\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (!Technology::T) {
    fprintf (stderr, "%s: technology information not initialized!", argv[0]);
    return LISP_RET_ERROR;
  }
  LispSetReturnFloat (Technology::T->scale);
  return LISP_RET_FLOAT;
}

static int process_tech_getpitch (int argc, char **argv)
{
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <layer>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (!Technology::T) {
    fprintf (stderr, "%s: technology information not initialized!", argv[0]);
    return LISP_RET_ERROR;
  }
  int layer = atoi (argv[1]);
  if (layer < 1) {
    fprintf (stderr, "%s: layer number has to be >= 1.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (layer > Technology::T->nmetals) {
    fprintf (stderr, "%s: specified layer %d is too large (max=%d).\n",
	     argv[0], layer, Technology::T->nmetals);
    return LISP_RET_ERROR;
  }
  LispSetReturnInt (Technology::T->metal[layer-1]->getPitch());
  return LISP_RET_INT;
}

static struct LispCliCommand tech_cmds[] = {
  { NULL, "Technology parameters (rules specified in units of scale)", NULL },
  { "uscale", "- returns scale factor in microns", process_tech_uscale },
  { "scale", "- returns scale factor in nanometers", process_tech_scale },
  { "getpitch", "<layer> - returns pitch of metal layer <layer>", process_tech_getpitch }
};

void act_cmds_init (void)
{
  LispCliAddCommands ("act", act_cmds, sizeof (act_cmds)/sizeof (act_cmds[0]));
  LispCliAddCommands ("tech", tech_cmds, sizeof (tech_cmds)/sizeof (tech_cmds[0]));
}
