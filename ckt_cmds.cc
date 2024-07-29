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

static ActCellPass *getCellPass();

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


static ActId *my_parse_id (const char *name)
{
  return ActId::parseId (name);
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

int process_ckt_save_flatsp (int argc, char **argv)
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

  ActCellPass *cp = getCellPass();
  Assert (cp, "What?");
  if (!cp->completed()) {
    fprintf (stderr, "%s: flat spice output is only used after cell mapping\n",
	     argv[0]);
    return LISP_RET_ERROR;
  }
  
  fp = std_open_output (argv[0], argv[1]);
  if (!fp) {
    return LISP_RET_ERROR;
  }
  np->printFlat (fp);
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
    fprintf (stderr, "%s: internal error\n", argv[0]);
    return LISP_RET_ERROR;
  }
  ActBooleanizePass *bp = dynamic_cast<ActBooleanizePass *>(p);
  if (!bp) {
    fprintf (stderr, "%s: internal error-2\n", argv[0]);
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

  if (!std_argcheck ((argc == 3 ? 2 : argc), argv, 2, "[-nocell] <file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s*");

  if (!F.act_toplevel) {
    fprintf (stderr,  "%s: top-level module is unspecified.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (argc == 3) {
    if (strcmp (argv[1], "-nocell") != 0) {
      fprintf (stderr, "%s: only -nocell is a supported argument", argv[0]);
      return LISP_RET_ERROR;
    }
    else {
      config_set_int ("act2v.emit_cells", 0);
    }
  }
  else {
    config_set_int ("act2v.emit_cells", 1);
  }

  fp = std_open_output (argv[0], argv[argc-1]);
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

  ActId *tmp = my_parse_id (argv[2]);
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

  tmp = my_parse_id (argv[3]);
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
  if ((nm = proc->addBuffer (argv[2], tmp, buftype, true))) {
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

  ActId *tmp = my_parse_id (argv[2]);
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

static int validate_signal (const char *cmd, ActId *id)
{
  Array *x;
  
  InstType *itx = F.act_toplevel->CurScope()->FullLookup (id, &x);

  if (itx == NULL) {
    fprintf (stderr, "%s: could not find identifier `", cmd);
    id->Print (stderr);
    fprintf (stderr, "'\n");
    return 0;
  }
  
  if (!TypeFactory::isBoolType (itx)) {
    fprintf (stderr, "%s: identifier `", cmd);
    id->Print (stderr);
    fprintf (stderr, "' is not a signal (");
    itx->Print (stderr);
    fprintf (stderr, ")\n");
    return 0;
  }
  
  if (itx->arrayInfo() && (!x || !x->isDeref())) {
    fprintf (stderr, "%s: identifier `", cmd);
    id->Print (stderr);
    fprintf (stderr, "' is an array.\n");
    return 0;
  }

  /* -- check all the de-references are valid -- */
  if (!id->validateDeref (F.act_toplevel->CurScope())) {
    fprintf (stderr, "%s: `", cmd);
    id->Print (stderr);
    fprintf (stderr, "' contains an invalid array reference.\n");
    return 0;
  }

  return 1;
}

static InstType *getoptparentuser (act_connection *c)
{
  int t;
  t = c->getctype();
  if (t == 1 || t == 3) {
    c = c->parent;
  }
  if (!c->parent) return NULL;
  return c->parent->getvx()->t;
}

static int visited_inst (list_t *l, ActId *prefix, ActId *id)
{
  for (listitem_t *li = list_first (l); li; li = list_next (li)) {
    ActId *p1 = (ActId *) list_value (li);
    li = list_next (li);
    ActId *p2 = (ActId *) list_value (li);
    if (!prefix) {
      if (p1 == NULL && id->isEqual (p2)) {
	return 1;
      }
    }
    else if (!p1) {
      return 0;
    }
    else if (prefix->isEqual (p1) && id->isEqual (p2)) {
      return 1;
    }
  }
  return 0;
}

static int found_pin (list_t *l, ActId *pin)
{
  for (listitem_t *li = list_first (l); li; li = list_next (li)) {
    ActId *x = (ActId *) list_value (li);
    if (pin->isEqual (x)) {
      return 1;
    }
  }
  return 0;
}
  

static int process_net_to_pins (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<net>",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    return LISP_RET_ERROR;
  }

  ActCellPass *cp = getCellPass();
  Assert (cp && cp->completed(), "What?");

  ActId *tmp = my_parse_id (argv[1]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }

  if (!validate_signal (argv[0], tmp)) {
    return LISP_RET_ERROR;
  }

  // 1. foo.bar.baz.q[3].p : check that this exists, and is a Boolean
  // 2. split this into <instance-prefix>.<local-signal>
  // Use nonProcSuffix!

  list_t *l = list_new ();
  list_append (l, F.act_toplevel); // type
  list_append (l, tmp);  // name
  list_append (l, NULL); // prefix

  // XXX: need to track visited instances!
  list_t *visited = list_new ();

  list_t *ret_pins = list_new ();

  while (!list_isempty (l)) {
    Process *top;
    Process *q;
    ActId *prefix;

    prefix = (ActId *) list_delete_tail (l);
    tmp = (ActId *) list_delete_tail (l);
    top = (Process *) list_delete_tail (l);

#if 0
    printf ("Looking-for [prefix: ");
    if (prefix) {
      prefix->Print (stdout);
    }
    else {
      printf ("-");
    }
    printf (" ]: ");
    tmp->Print (stdout);
    printf (" @ %s\n", top->getName());
#endif    

    /*
     * Take the instance ID and split it into a non-process suffix
     */
    ActId *x = tmp->nonProcSuffix (top, &q);

    /*
     * x is the instance within the process 
     * q is the process type for the instance "x"
     * if it is a cell, then we don't need to do anything
     */
    if (q->isCell()) {
#if 0
      printf (" > cell; skip\n");
#endif      
      continue;
    }

    //
    // get the canonical connection for this within the process
    //
    act_connection *c = x->Canonical (q->CurScope());
    act_connection *tmpc = c;

    //
    // strip out the "x" part from the path to the instance
    // This, combined with "prefix" is the new prefix.
    //
    ActId *suffix = NULL;
    {
      ActId *del = tmp;
      while (del && del->Rest() != x) {
	del = del->Rest();
      }
      if (del) {
	del->prune ();
	suffix = tmp->Clone ();
	del->Append (x);
      }
      else {
	suffix = NULL;
      }
    }
#if 0    
    printf (" > suffix: ");
    if (tmp) {
      tmp->Print (stdout);
    }
    else {
      printf ("-");
    }
    printf ("\n");
#endif    

    if (prefix) {
      if (suffix) {
	prefix->Tail()->Append (suffix);
      }
    }
    else {
      prefix = suffix;
    }

#if 0    
    printf (" > new prefix: ");
    if (prefix) {
      prefix->Print (stdout);
    }
    else {
      printf ("-");
    }
    printf ("\n");
#endif    

    int first = 1;
    do {
#if 0
      printf ("  >> looking-at: ");
      tmpc->Print (stdout);
      printf ("\n");
#endif
      x = tmpc->toid();

      if (first) {
	list_append (visited, prefix);
	list_append (visited, x->Clone());
      }
      first = 0;
      
      // x = the non-proc suffix, q = process
      ActId *y = x->Rest();
      x->prune ();
      InstType *it = q->Lookup (x);
      Assert (it, "This should have failed much earlier!");
      
      if (TypeFactory::isProcessType (it)) {
	Process *newq = dynamic_cast<Process *>(it->BaseType());
	Assert (newq, "hmm");

	ActId *newpref = prefix;
	if (newpref) {
	  newpref = newpref->Clone ();
	  newpref->Tail()->Append (x);
	}
	else {
	  newpref = x;
	}

	if (newq->isCell()) {
	  if (newpref) {
	    newpref->Tail()->Append (y);
	  }
	  else {
	    newpref = y;
	  }
	  if (!found_pin (ret_pins, newpref)) {
	    list_append (ret_pins, newpref);
	  }
#if 0	  
	  printf ("  << ");
	  if (newpref) {
	    newpref->Print (stdout);
	  }
	  printf ("/");
	  y->Print (stdout);

	  int pid = newq->FindPort (y->getName());

	  if (pid > 0) {
	    InstType *pt = newq->getPortType (pid-1);
	    Assert (TypeFactory::isBoolType (pt), "What?!");
	    printf (" [");
	    if (pt->getDir() == Type::IN) {
	      printf ("?");
	    }
	    else {
	      printf ("!");
	    }
	    printf ("]");
	  }
	  
	  printf (">>\n");
#endif
	}
	else if (!visited_inst (visited, newpref, y)) {
#if 0
	  printf ("  >>> add to search! ");
	  if (newpref) {
	    newpref->Print (stdout);
	  }
	  printf(" : ");
	  y->Print (stdout);
	  printf ("\n");
#endif	  
	  // add this if not visited
	  list_append (l, newq);
	  list_append (l, y);
	  list_append (l, newpref);
	}
	else {
#if 0	  
	  printf (" already visited: ");
	  if (newpref) {
	    newpref->Print (stdout);
	  }
	  printf (" : ");
	  y->Print (stdout);
	  printf ("\n");
#endif
		  
	  delete newpref;
	  delete y;
	}
      }
      else {
	if (q->isPort (x->getName()) && prefix) {
#if 0
	  // go find the parent!
	  printf (" << if this is a port, do something! >>\n");
#endif	  

	  if (y) {
	    x->Append (y);
	  }

	  ActId *newid = prefix->Clone();
	  newid->Tail()->Append (x);
#if 0	  
	  printf ("   path is now: ");
	  newid->Print (stdout);
	  printf ("\n");
#endif
	  
	  if (!visited_inst (visited, NULL, newid)) {
	    list_append (l, F.act_toplevel);
	    list_append (l, newid);
	    list_append (l, NULL);
	    
	    list_append (visited, NULL);
	    list_append (visited, newid->Clone());
	  }
	  else {
	    delete newid;
	  }
	}
	else {
	  delete x;
	  if (y) {
	    delete y;
	  }
	}
      }
      tmpc = tmpc->next;
    } while (tmpc != c);
    if (tmp) {
      delete tmp;
    }
  }
  list_free (l);

  for (listitem_t *li = list_first (visited); li; li = list_next (li)) {
    ActId *x = (ActId *) list_value (li);
    if (x) {
      delete x;
    }
    li = list_next (li);
    x = (ActId *) list_value (li);
    if (x) {
      delete x;
    }
  }
  list_free (visited);

  LispSetReturnListStart ();


  Type::direction dir[] = { Type::OUT, Type::IN, Type::NONE };
  
  for (int i=0; i < 3; i++) {
    LispAppendListStart ();
    for (listitem_t *li = list_first (ret_pins); li; li = list_next (li)) {
      ActId *x = (ActId *) list_value (li);
      InstType *itx = F.act_toplevel->CurScope()->FullLookup (x, NULL);
      Assert (itx, "What?");
      if (itx->getDir() == dir[i]) {
	char buf[10240];
	ActId *pin, *xprev;

	pin = x;
	xprev = NULL;
	while (pin->Rest()) {
	  xprev = pin;
	  pin = pin->Rest();
	}
	if (!xprev) {
	  x->sPrint (buf, 10240);
	  LispAppendReturnString (buf);
	  warning ("%s: list of pins has an unexpected issue (pin: %s)",
		   argv[0], buf);
	}
	else {
	  LispAppendListStart ();
	  xprev->prune();
	  x->sPrint (buf, 10240);
	  LispAppendReturnString (buf);
	  xprev->Append (pin);
	  pin->sPrint (buf, 10240);
	  LispAppendReturnString (buf);
	  LispAppendListEnd ();
	}
      }
      if (i == 2) {
	delete x;
      }
    }
    LispAppendListEnd ();
  }
  LispSetReturnListEnd ();

  list_free (ret_pins);

  save_to_log (argc, argv, "s");
  
  return LISP_RET_LIST;
}

static int process_cell_to_pins (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<net>",
		     F.cell_map ? STATE_EXPANDED : STATE_ERROR)) {
    return LISP_RET_ERROR;
  }

  ActCellPass *cp = getCellPass();
  Assert (cp && cp->completed(), "What?");

  ActId *tmp = my_parse_id (argv[1]);
  if (!tmp) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }

  Array *a;
  InstType *itx = F.act_toplevel->CurScope()->FullLookup (tmp, &a);

  if (itx == NULL) {
    fprintf (stderr, "%s: could not find identifier `", argv[0]);
    tmp->Print (stderr);
    fprintf (stderr, "'\n");
    delete tmp;
    return LISP_RET_ERROR;
  }

  if (itx->arrayInfo() && (!a || !a->isDeref())) {
    fprintf (stderr, "%s: identifier `", argv[0]);
    tmp->Print (stderr);
    fprintf (stderr, "' is an array.\n");
    delete tmp;
    return LISP_RET_ERROR;
  }

  Process *p = dynamic_cast <Process *> (itx->BaseType());
  if (!p) {
    fprintf (stderr, "%s: identifier `", argv[0]);
    tmp->Print (stderr);
    fprintf (stderr, "' is not a process type.\n");
    delete tmp;
    return LISP_RET_ERROR;
  }
  if (!p->isCell()) {
    fprintf (stderr, "%s: identifier `", argv[0]);
    tmp->Print (stderr);
    fprintf (stderr, "' is not a cell.\n");
    delete tmp;
    return LISP_RET_ERROR;
  }

  delete tmp;
  
  ActPass *pass = F.act_design->pass_find ("booleanize");
  if (!pass) {
    fprintf (stderr, "%s: internal error\n", argv[0]);
    return LISP_RET_ERROR;
  }
  ActBooleanizePass *bp = dynamic_cast<ActBooleanizePass *>(pass);
  if (!bp) {
    fprintf (stderr, "%s: internal error-2\n", argv[0]);
    return LISP_RET_ERROR;
  }

  act_boolean_netlist_t *nl = bp->getBNL (p);
  if (!nl) {
    fprintf (stderr, "%s: could not find netlist for `%s'\n", argv[0],
	     p->getName());
    return LISP_RET_ERROR;
  }

  LispSetReturnListStart ();


  for (int k=0; k < 3; k++) {
    LispAppendListStart ();
    for (int i=0; i < A_LEN (nl->ports); i++) {
      if (nl->ports[i].omit) continue;
      /* output = 0, input = 1, bidir = 2 */
      if (nl->ports[i].input + nl->ports[i].bidir == k) {
	ActId *tid;
	char buf[10240];
	tid = nl->ports[i].c->toid();
	tid->sPrint (buf, 10240);
	LispAppendReturnString (buf);
	delete tid;
      }
    }
    LispAppendListEnd ();
  }
  
  LispSetReturnListEnd ();

  save_to_log (argc, argv, "s");
  
  return LISP_RET_LIST;
}


static struct LispCliCommand ckt_cmds[] = {
  { NULL, "ACT circuit generation", NULL },
  { "map", "- generate transistor-level description",
    process_ckt_map },
  { "save-sp", "<file> - save SPICE netlist to <file>",
    process_ckt_save_sp },

  { "save-flatsp", "<file> - save flattened spice netlist to file after cell mapping", process_ckt_save_flatsp },
  
  { "mk-nets", "- preparation for DEF generation",
    process_ckt_mknets },
  { "save-prs", "<file> - save flat production rule set to <file> for simulation",
    process_ckt_save_prs },
  { "save-lvp", "<file> - save flat production rule set to <file> for lvp",
    process_ckt_save_lvp },
  { "save-sim", "<file-prefix> - save flat .sim/.al file",
    process_ckt_save_sim },
  { "save-vnet", "[-nocell] <file> - save Verilog netlist to <file>",
    process_ckt_save_v },
  

  { NULL, "ACT cell mapping and editing", NULL },
  { "cell-map", "- map gates to cell library", process_cell_map },
  { "cell-save", "<file> - save cells to file", process_cell_save },
  { "cell-addbuf", "<proc> <inst> <pin> <buf> - add buffer to the pin within the process",
    process_add_buffer },
  { "cell-edit", "<proc> <inst> <newcell> - replace cell for instance within <proc>",
    process_edit_cell },

  { "cell-update", "- take the design back to the clean state",
    process_update_cell },

  { "net->pins", "<net> - return pins that a net is connected to",
    process_net_to_pins },

  { "cell->pins", "<inst> - return pins for a cell",
    process_cell_to_pins }
  
};

void ckt_cmds_init (void)
{
  LispCliAddCommands ("ckt", ckt_cmds, sizeof (ckt_cmds)/sizeof (ckt_cmds[0]));
}
