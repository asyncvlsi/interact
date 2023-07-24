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
#include <common/list.h>
#include <common/pp.h>
#include <lispCli.h>
#include "all_cmds.h"
#include "ptr_manager.h"
#include "flow.h"
#include <act/tech.h>

#ifdef FOUND_timing_actpin

#include <act/timing/galois_api.h>

static double act_delay_units = -1.0;

static ActGaloisTiming *agt = NULL;

static void init (int mode = 0)
{
  static int first = 1;

  if (first) {
    agt = NULL;
  }
  first = 0;
  
  if (mode == 1) {
    if (agt) {
      delete agt;
      agt = NULL;
    }
    first = 1;
  }
  init_galois_shmemsys (mode);
}

static ActId *my_parse_id (const char *name)
{
  return ActId::parseId (name);
}

/*------------------------------------------------------------------------
 *
 *  Read liberty file, return handle
 *
 *------------------------------------------------------------------------
 */
static void *read_lib_file (const char *file)
{
  init();
  
  FILE *fp = fopen (file, "r");
  if (!fp) {
    return NULL;
  }
  fclose (fp);

  galois::eda::liberty::CellLib *lib = new galois::eda::liberty::CellLib;
  lib->parse(file);

  return (void *)lib;
}

/*------------------------------------------------------------------------
 *
 *  Read in a .lib file for timing/power analysis
 *
 *------------------------------------------------------------------------
 */
int process_read_lib (int argc, char **argv)
{
  void *lib;
  if (argc != 2) {
    fprintf (stderr, "Usage: %s <liberty-file>\n", argv[0]);
    return LISP_RET_ERROR;
  }

  lib = read_lib_file (argv[1]);
  if (!lib) {
    fprintf (stderr, "%s: could not open liberty file `%s'\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  LispSetReturnInt (ptr_register ("liberty", lib));

  return LISP_RET_INT;
}


/*------------------------------------------------------------------------
 *
 *  Read in a .lib file for timing/power analysis
 *
 *------------------------------------------------------------------------
 */
int process_merge_lib (int argc, char **argv)
{
  int lh;
  galois::eda::liberty::CellLib *cl;
  FILE *fp;
  
  if (argc != 3) {
    fprintf (stderr, "Usage: %s <lh> <liberty-file>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  lh = atoi (argv[1]);
  cl = (galois::eda::liberty::CellLib *) ptr_get ("liberty", lh);
  if (!cl) {
    fprintf (stderr, "%s: specified liberty file handle (%d) not found!\n", argv[0], lh);
    return LISP_RET_ERROR;
  }

  fp = fopen (argv[2], "r");
  if (!fp) {
    fprintf (stderr, "%s: liberty file `%s' not found!\n", argv[0], argv[2]);
    return LISP_RET_ERROR;
  }
  fclose (fp);
  
  cl->parse (argv[2]);

  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}


static int get_net_to_timing_vertex (char *cmd, char *name, int *vid, char **pin = NULL)
{
  ActId *id = my_parse_id (name);
  int goff;

  if (!F.tp) {
    fprintf (stderr, "%s: cannot run without creating a timing graph!", cmd);
    return 0;
  }

  if (!F.sp) {
    ActPass *ap = F.act_design->pass_find ("collect_state");
    if (!ap) {
      fprintf (stderr, "Internal error: state pass missing but timer present?\n");
      return 0;
    }
    F.sp = dynamic_cast<ActStatePass *> (ap);
    Assert (F.sp, "What?");
  }

  if (!id) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", cmd, name);
    return 0;
  }

  Array *x;
  InstType *itx;

  /* -- validate the type of this identifier -- */

  itx = F.act_toplevel->CurScope()->FullLookup (id, &x);
  if (itx == NULL) {
    fprintf (stderr, "%s: could not find identifier `%s'\n", cmd, name);
    return 0;
  }
  if (!TypeFactory::isBoolType (itx)) {
    fprintf (stderr, "%s: identifier `%s' is not a signal (", cmd, name);
    itx->Print (stderr);
    fprintf (stderr, ")\n");
    return 0;
  }
  if (itx->arrayInfo() && (!x || !x->isDeref())) {
    fprintf (stderr, "%s: identifier `%s' is an array.\n", cmd, name);
    return 0;
  }

  /* -- check all the de-references are valid -- */
  if (!id->validateDeref (F.act_toplevel->CurScope())) {
    fprintf (stderr, "%s: `%s' contains an invalid array reference.\n", cmd, name);
    return 0;
  }

  Assert (F.sp && F.tp, "Hmm");

  if (!F.sp->checkIdExists (id)) {
    id->Print (stderr);
    fprintf (stderr, ": identifier not found in the timing graph!\n");
    return 0;
  }

  goff = F.sp->globalBoolOffset (id);
  
  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  stateinfo_t *si = F.sp->getStateInfo (F.act_toplevel);

  Assert (tg && si, "What?");
  
  *vid = 2*(tg->globOffset() + si->ports.numBools() + goff);

  if (pin) {
    char *tmp;
    int len = 0;
    *pin = name + strlen (name);
    while (*pin > name && *((*pin)-1) != '.') {
      *pin = *pin - 1;
      len++;
    }
    *pin = Strdup (*pin);
    (*pin)[len] = '\0';
  }
  return 1;
}


/*------------------------------------------------------------------------
 *
 *  process_timer_build -
 *
 *------------------------------------------------------------------------
 */
static int process_timer_build (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (!F.act_toplevel) {
    fprintf (stderr, "%s: need top level of design set\n", argv[0]);
    return LISP_RET_ERROR;
  }

  ActPass *ap = F.act_design->pass_find ("taggedTG");

  if (!ap) {
    fprintf (stderr, "%s: no timing graph construction pass found\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.tp = dynamic_cast<ActDynamicPass *> (ap);
  Assert (F.tp, "Hmm");

  /* -- create timing graph -- */
  if (!F.tp->completed()) {
    F.tp->run (F.act_toplevel);
  }

  save_to_log (argc, argv, "s");
  return LISP_RET_TRUE;
}


static int process_timer_tick (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 3, "<net1>+/- <net2>+/-", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (!F.tp) {
    fprintf (stderr, "%s: need to build timing graph first.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (F.timer == TIMER_RUN) {
    fprintf (stderr, "%s: need to mark edges before running the timer.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  int dir1, dir2;
  int len1, len2;
  len1 = strlen (argv[1]);
  len2 = strlen (argv[2]);
  if (argv[1][len1-1] == '+') {
    dir1 = 1;
  }
  else if (argv[1][len1-1] == '-') {
    dir1 = 0;
  }
  else {
    fprintf (stderr, "%s: need a direction (+/-) for %s\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  if (argv[2][len2-1] == '+') {
    dir2 = 1;
  }
  else if (argv[2][len2-1] == '-') {
    dir2 = 0;
  }
  else {
    fprintf (stderr, "%s: need a direction (+/-) for %s\n", argv[0], argv[2]);
    return LISP_RET_ERROR;
  }
  argv[1][len1-1] = '\0';
  argv[2][len2-1] = '\0';

  char *tmp1 = Strdup (argv[1]);
  char *tmp2 = Strdup (argv[2]);

  argv[1][len1-1] = dir1 ? '+' : '-';
  argv[2][len2-1] = dir2 ? '+' : '-';

  int vid1, vid2;

  if (!get_net_to_timing_vertex (argv[0], tmp1, &vid1) ||
      !get_net_to_timing_vertex (argv[0], tmp2, &vid2)) {
    FREE (tmp1);
    FREE (tmp2);
    return LISP_RET_ERROR;
  }
  FREE (tmp1);
  FREE (tmp2);
  vid1 += dir1;
  vid2 += dir2;

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);


  /* find *all* edges from vid1 to vid2 */
  AGvertexFwdIter fw(tg, vid1);
  int found = 0;
  for (fw = fw.begin(); fw != fw.end(); fw++) {
    AGedge *e = (*fw);
    if (e->dst != vid2) {
      continue;
    }
    TimingEdgeInfo *te = (TimingEdgeInfo *)e->getInfo();
    te->tickEdge();
    found = 1;
  }
  if (found == 0) {
    fprintf (stderr, "%s: could not find timing edge %s -> %s\n", argv[0],
	     argv[1], argv[2]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s*");
  
  return LISP_RET_TRUE;
}

/*------------------------------------------------------------------------
 *
 *  Create timing graph and push it to the timing analysis engine
 *
 *------------------------------------------------------------------------
 */
int process_timer_init (int argc, char **argv)
{
  if (!std_argcheck ((argc > 2 ? 2 : argc), argv, 2, "<lib-id1> <lib-id2> ...", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (!F.act_toplevel) {
    fprintf (stderr, "%s: need top level of design set\n", argv[0]);
    return LISP_RET_ERROR;
  }

  /* -- add cell library -- */
  galois::eda::liberty::CellLib **libs;

  MALLOC (libs, galois::eda::liberty::CellLib *, argc-1);
  
  for (int i=1; i < argc; i++) {
    libs[i-1] = (galois::eda::liberty::CellLib *) ptr_get ("liberty", atoi(argv[i]));
    if (!libs[i-1]) {
      fprintf (stderr, "%s: timing lib file #%d (`%s') not found\n", argv[0],
	       i-1, argv[i]);
      FREE (libs);
      return LISP_RET_ERROR;
    }
  }

  if (agt) {
    delete agt;
  }
  agt = new ActGaloisTiming (F.act_design,
			     F.act_toplevel,
			     argc-1, libs);

  if (agt->tgError()) {
    fprintf (stderr, "%s: failed to initialize timer.\n", argv[0]);
    if (agt->getError()) {
      fprintf (stderr, " -> %s\n", agt->getError());
    }
    return LISP_RET_ERROR;
  }

  F.timer = TIMER_INIT;

  ActPass *ap = F.act_design->pass_find ("taggedTG");
  if (!ap) {
    fprintf (stderr, "%s: no timing graph construction pass found\n", argv[0]);
    return LISP_RET_ERROR;
  }
  F.tp = dynamic_cast<ActDynamicPass *> (ap);
  Assert (F.tp, "Hmm");


  save_to_log (argc, argv, "i*");
  
  return LISP_RET_TRUE;
}


/*------------------------------------------------------------------------
 *
 *  Run the timing analysis engine
 *
 *------------------------------------------------------------------------
 */
int process_timer_run (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (F.timer == TIMER_NONE) {
    fprintf (stderr, "%s: timer needs to be initialized\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (!agt) {
    fprintf (stderr, "%s: timer needs to be initialized (inconsistency?)\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (!agt->runFullTiming ()) {
    fprintf (stderr, "%s: error running timer\n", argv[0]);
    if (agt->getError()) {
      fprintf (stderr, " -> %s\n", agt->getError());
    }
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "");

  F.timer = TIMER_RUN;

  double p = 0.0;
  int M = 0;

  agt->getPeriod (&p, &M);

  LispSetReturnListStart ();
  LispAppendReturnFloat (p);
  LispAppendReturnInt (M);
  LispSetReturnListEnd ();
  
  return LISP_RET_LIST;
}

/*------------------------------------------------------------------------
 *
 *  Read in SPEF file
 *
 *------------------------------------------------------------------------
 */
int process_timer_spef (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (F.timer == TIMER_NONE) {
    fprintf (stderr, "%s: timer needs to be initialized\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (!agt) {
    fprintf (stderr, "%s: timer needs to be initialized (inconsistency?)\n", argv[0]);
    return LISP_RET_ERROR;
  }

  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: file `%s' not found\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);

  try {
    if (!agt->readSPEF (argv[1])) {
      fprintf (stderr, "%s: could not read SPEF `%s'\n", argv[0], argv[1]);
      if (agt->getError()) {
	fprintf (stderr, " -> %s\n", agt->getError());
      }
      return LISP_RET_ERROR;
    }
  } catch (galois::eda::parasitics::spef_exc &e) {
    fprintf (stderr, "%s: resetting SPEF information\n", argv[0]);
    agt->resetSPEF ();
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}



static double my_round (double d)
{
  d = d*1e5 + 0.5;
  d = ((int)d)/1.0e5;
  return d;
}

static double my_round_2 (double d)
{
  d = d*1e2 + 0.5;
  d = ((int)d)/1.0e2;
  return d;
}
  
int process_timer_info (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<net>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (!F.sp) {
    ActPass *ap = F.act_design->pass_find ("collect_state");
    if (!ap) {
      fprintf (stderr, "Internal error: state pass missing but timer present?\n");
      return LISP_RET_ERROR;
    }
    F.sp = dynamic_cast<ActStatePass *> (ap);
    Assert (F.sp, "What?");
  }

  int vid;
  if (!get_net_to_timing_vertex (argv[0], argv[1], &vid)) {
    return LISP_RET_ERROR;
  }
				 
  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  Assert (tg, "What?");
  Assert (agt, "What?!");

  list_t *l = agt->queryAll (vid);

  if (l) {
    listitem_t *li;
    double p;
    int M;
    int count = 0;

    agt->getPeriod (&p, &M);

    if (M == 0) {
      warning ("No critical cycle found; is the circuit acyclic?");
      return LISP_RET_ERROR;
    }

    for (li = list_first (l); li; li = list_next (li)) {
      struct timing_info *ti = (struct timing_info *) list_value (li);
      char buf[10240];

      ti->pin->sPrintFullName (buf, 10240);
      printf ("%s%c (%s) [%s] slew: %g\n", buf, ti->dir ? '+' : '-',
	      ti->getDir() ? "rise" : "fall", (count < 2 ? "driver" : "driven pin"),
	      ti->getSlew ());
      for (int i=0; i < M; i++) {
	printf ("\titer %2d: arr: %g; req: %g; slk: %g\n", i,
		ti->getArrv (i), ti->getReq (i),
		my_round (ti->getReq(i) - ti->getArrv(i)));
      }
      count++;
    }
  }

  agt->queryFree (l);
  
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}


int process_timer_cycle (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return LISP_RET_ERROR;
  }

  Assert (agt, "What?");

  cyclone::TimingPath cyc = agt->getCritCycle ();

  if (cyc.empty()) {
    printf ("%s: No critical cycle.\n", argv[0]);
  }
  else {
    pp_t *pp = pp_init (stdout, output_window_width);
    agt->displayPath (pp, cyc, 1);
    pp_stop (pp);
  }
  save_to_log (argc, argv, "s*");
  
  return LISP_RET_TRUE;
}



int process_timer_addconstraint (int argc, char **argv)
{
  if (!std_argcheck (argc == 5 ? 4 : argc, argv, 4, "<root>+/- [*]<fast>+/- [*]<slow>+/- [margin]", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (!F.tp) {
    fprintf (stderr, "%s: need to build timing graph first.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (F.timer == TIMER_INIT || F.timer == TIMER_RUN) {
    fprintf (stderr, "%s: need to add constraints before initializing the timer.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  int dir[3];
  int len[3];
  int vid[3];

  int tick[3];

  tick[0] = 0;

  for (int i=0; i < 3; i++) {
    len[i] = strlen (argv[i+1]);
    if (argv[i+1][len[i]-1] == '+') {
      dir[i] = 1;
    }
    else if (argv[i+1][len[i]-1] == '-') {
      dir[i] = 0;
    }
    else {
      fprintf (stderr, "%s: need a direction (+/-) for %s\n", argv[0],
	       argv[i+1]);
      return LISP_RET_ERROR;
    }
    argv[i+1][len[i]-1] = '\0';

    char *tmpname = Strdup (argv[i+1]);

    argv[i+1][len[i]-1] = dir[i] ? '+' : '-';

    if (i > 0) {
      if (argv[i+1][0] == '*') {
	tick[i] = 1;
      }
      else {
	tick[i] = 0;
      }
    }
    
    if (!get_net_to_timing_vertex (argv[0], tick[i] + tmpname, &vid[i])) {
      FREE (tmpname);
      return LISP_RET_ERROR;
    }
    FREE (tmpname);
    vid[i] += dir[i];
  }

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  int margin;
  
  if (argc == 5) {
    margin = atoi (argv[5]);
  }
  else {
    margin = 0;
  }
      
  /* add constraint! */
  int cid = tg->addConstraint (vid[0], vid[1], vid[2], margin);

  TaggedTG::constraint *c = tg->getConstraint (cid);
  Assert (c, "Hmm");
  c->from_tick = tick[1];
  c->to_tick = tick[2];

  save_to_log (argc, argv, "sssi");
  
  return LISP_RET_TRUE;
}

static void _set_delay_units (void)
{
  if (act_delay_units == -1.0) {
    if (config_exists ("net.delay")) {
      act_delay_units = config_get_real ("net.delay");
    }
    else {
      /* rule of thumb: FO4 is roughly F/2 ps where F is in nm units */
      act_delay_units = config_get_real ("net.lambda")*1e-3;
    }
  }
}

/*
  Return slack of a timing constraint in timer units

  margin : c->margin, but in timer units

  p, M : cycle period and ticks on critical cycle

  iteration : 0 <= iteration < M : the iteration to check
*/


static double get_slack (TaggedTG::constraint *c,
			 timing_info *from, timing_info *to,
			 int iteration,
			 double margin,
			 double p,
			 int M)
{
  int j;
  double adj = 0;
    
  if (c->from_tick == c->to_tick) {
    j = iteration;
  }
  else if (c->from_tick) {
    j = (iteration+M-1) % M;
    if (j >= iteration) {
      adj = -M*p;
    }
  }
  else {
    j = (iteration+1) % M;
    if (j <= iteration) {
      adj = M*p;
    }
  }

  double x = (to->arr[j] - from->arr[iteration] + adj);

  return (x - margin);
}

int process_timer_constraint (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, argc == 1 ? 1 : 2, "[<net>]", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (!F.sp) {
    ActPass *ap = F.act_design->pass_find ("collect_state");
    if (!ap) {
      fprintf (stderr, "Internal error: state pass missing but timer present?\n");
      return LISP_RET_ERROR;
    }
    F.sp = dynamic_cast<ActStatePass *> (ap);
    Assert (F.sp, "What?");
  }

  int vid;
  
  if (argc == 2) {
    if (!get_net_to_timing_vertex (argv[0], argv[1], &vid)) {
      return LISP_RET_ERROR;
    }
  }
  else {
    vid = -1;
  }

  Assert (agt, "What?");
    
  int M;
  double p;

  agt->getPeriod (&p, &M);

  if (M == 0) {
    warning ("No critical cycle found; is the circuit acyclic?");
    return LISP_RET_ERROR;
  }

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  int tmp = tg->numConstraints();
  int nzeros = 0;
  while (tmp > 0) {
    nzeros++;
    tmp /= 10;
  }

  _set_delay_units ();
  double timer_units = agt->getTimeUnits ();

  for (int i=0; i < tg->numConstraints(); i++) {
    TaggedTG::constraint *c;
    c = tg->getConstraint (i);
    if (vid == -1 || (c->root == vid || c->from == vid || c->to == vid)) {
      /* found a constraint! */
      int from_dirs[2];
      int to_dirs[2];
      int nfrom, nto;
      char from_char, to_char;
      double margin = c->margin*act_delay_units/timer_units;

      if (c->iso) {
	/* XXX: isochronic fork. check this differently */
	if (c->root_dir == 0) {
	  AGvertex *v;
	  warning ("isochronic fork needs a direction on the root of the fork");
	  v = tg->getVertex (c->root);
	  if (!v) {
	    warning ("root vertex not found!");
	  }
	  else {
	    TimingVertexInfo *inf = (TimingVertexInfo *)v->getInfo();
	    char *s = inf->getFullInstPath ();
	    fprintf (stderr, "   root: %s\n", s);
	    FREE (s);
	  }
	}
	else {
	  int myvid = c->root + (c->root_dir == 1 ? 1 : 0);
	  AGvertexFwdIter fw(tg, myvid);
	  timing_info *root_ti =
	    agt->queryTransition (c->root, c->root_dir == 1 ? 1 : 0);

	  if (!root_ti) {
	    continue;
	  }

	  int min_set = 0;
	  int iso_set = 0;
	  double *min_delay;
	  double *isofork_delay;

	  MALLOC (min_delay, double, M);
	  MALLOC (isofork_delay, double, M);

	  for (fw = fw.begin(); fw != fw.end(); fw++) {
	    AGedge *e = (*fw);
	    timing_info *pi;

	    if ((e->dst & ~1) == c->from) {
	      /* isochronic fork leg */
	      ActPin *dp = agt->getDstPin (e);
	      if (dp) {
		pi = new timing_info (agt, dp, c->root_dir == 1 ? 1 : 0);
		if (pi) {
		  iso_set = 1;
		  for (int i=0; i < M; i++) {
		    isofork_delay[i] = pi->arr[i];
		  }
		  delete pi;
		}
	      }
	    }
	    else {
	      ActPin *gate_drive = agt->tgVertexToPin (e->dst);
	      if (gate_drive) {
		pi = new timing_info (agt, gate_drive, (e->dst & 1) ? 1 : 0);
		if (pi) {
		  if (!min_set) {
		    for (int i=0; i < M; i++) {
		      min_delay[i] = pi->arr[i];
		    }
		  }
		  else {
		    for (int i=0; i < M; i++) {
		      if (pi->arr[i] < min_delay[i]) {
			min_delay[i] = pi->arr[i];
		      }
		    }
		  }
		  min_set = 1;
		}
		delete pi;
	      }
	    }
	  }

	  AGvertex *v;
	  TimingVertexInfo *vi;
	  char *buf1, *buf2;
	  char *net1, *net2;
	  v = tg->getVertex (c->root);
	  Assert (v, "What?");
	  vi = (TimingVertexInfo *) v->getInfo();
	  Assert (vi, "Hmm");
	  buf1 = (char *)vi->info();
	  if (buf1) {
	    buf1[strlen(buf1)-1] = '\0';
	  }
          else {
            buf1 = Strdup ("-unknown-");
          }

	  v = tg->getVertex (c->from);
	  Assert (v, "What?");
	  vi = (TimingVertexInfo *) v->getInfo();
	  Assert (vi, "Hmm");
	  buf2 = (char *)vi->info();
	  if (buf2) {
	    buf2[strlen(buf2)-1] = '\0';
	  }
          else {
            buf2 = Strdup ("-unknown-");
          }
	  
	  printf ("[%*d/%*d] iso %s%c -> %s", nzeros, i+1, nzeros,
		  tg->numConstraints(), buf1,
		  c->root_dir == 1 ? '+' : '-', buf2);
	  FREE (buf1);
	  FREE (buf2);
	  if (c->margin != 0) {
	    if (margin*timer_units < 1e-9) {
	      printf (" [%g ps]", margin*timer_units*1e12);
	    }
	    else if (margin*timer_units < 1e-6) {
	      printf (" [%g ns]", margin*timer_units*1e9);
	    }
	    else {
	      printf (" [%g us]", margin*timer_units*1e6);
	    }
	  }
	  printf ("\n");
	  
	  if (iso_set && min_set) {
	    /* check! */
	    for (int i=0; i < M; i++) {
	      double slack;
	      char unit;
	      printf ("\titer %2d: ", i);

	      slack = min_delay[i] - isofork_delay[i] - margin;
	      slack *= timer_units;
	      if (fabs (slack) < 1e-9) {
		slack *= 1e12;
		unit = 'p';
	      }
	      else if (fabs (slack) < 1e-6) {
		slack *= 1e9;
		unit = 'n';
	      }
	      else {
		unit = 'u';
		slack *= 1e6;
	      }
	      printf ("[%g %cs]%s\n", my_round_2 (slack), unit,
		      slack < 0 ? "*ER" : "");
	    }
	  }
	  else {
	    printf (" ** no fork information\n");
	  }
	  FREE (isofork_delay);
	  FREE (min_delay);
	}
	continue;
      }
	  
      if (c->from_dir == 0) {
	from_dirs[0] = 0;
	from_dirs[1] = 1;
	nfrom = 2;
	from_char = ' ';
      }
      else {
	nfrom = 1;
	from_dirs[0] = (c->from_dir == 1 ? 1 : 0);
	from_char = (c->from_dir == 1 ? '+' : '-');
      }
      if (c->to_dir == 0) {
	to_dirs[0] = 0;
	to_dirs[1] = 1;
	nto = 2;
	to_char = ' ';
      }
      else {
	nto = 1;
	to_dirs[0] = (c->to_dir == 1 ? 1 : 0);
	to_char = (c->to_dir == 1 ? '+' : '-');
      }
      
      list_t *l[2];
      l[0] = agt->queryDriver (c->from);
      l[1] = agt->queryDriver (c->to);
      
      if (l[0] && l[1]) {
	timing_info *ti[2][2];
	for (int i=0; i < 2; i++) {
	  ti[i][0] = timer_query_extract_fall (l[i]);
	  ti[i][1] = timer_query_extract_rise (l[i]);
	}
	
	char buf1[1024],  buf2[1024];
	ti[0][0]->pin->sPrintFullName (buf1, 1024);
	ti[1][0]->pin->sPrintFullName (buf2, 1024);

	printf ("[%*d/%*d] %s%s -> %s%s%s", nzeros, i+1, nzeros,
		tg->numConstraints(),
		c->from_tick ? "*" : "", buf1,
		c->to_tick ? "*" : "", buf2,
		c->error ? " *root-err*" : "");
	if (c->margin != 0) {
	  if (margin*timer_units < 1e-9) {
	    printf (" [%g ps]", margin*timer_units*1e12);
	  }
	  else if (margin*timer_units < 1e-6) {
	    printf (" [%g ns]", margin*timer_units*1e9);
	  }
	  else {
	    printf (" [%g us]", margin*timer_units*1e6);
	  }
	}
	printf ("\n");
	if (c->error) {
	  timing_info *root_ti =
	    agt->queryTransition (c->root, c->root_dir == 1 ? 1 : 0);
	  Assert (root_ti, "What?!");
	  root_ti->pin->sPrintFullName (buf1, 1024);
	  printf (" >> root: %s%c\n   ", buf1, c->root_dir == 1 ? '+' : '-');
	  root_ti->Print (stdout);
	  printf ("\n");
	}

	if (c->error) {
	  for (int ii=0; ii < nfrom; ii++) {
	    ti[0][from_dirs[ii]]->pin->sPrintFullName (buf1, 1024);
	    printf (" >> from: %s%c\n   ", buf1, from_dirs[ii] ? '+' : '-');
	    ti[0][from_dirs[ii]]->Print (stdout);
	    printf ("\n");
	  }
	  for (int ii=0; ii < nto; ii++) {
	    ti[1][to_dirs[ii]]->pin->sPrintFullName (buf1, 1024);
	    printf (" >>   to: %s%c\n   ", buf1, to_dirs[ii] ? '+' : '-');
	    ti[1][to_dirs[ii]]->Print (stdout);
	    printf ("\n");
	  }
	}
	else {
	  for (int i=0; i < M; i++) {
	    printf ("\titer %2d: ", i);
	    for (int ii=0; ii < nfrom; ii++) {
	      for (int jj=0; jj < nto; jj++) {
		double slack = get_slack (c,
					  ti[0][from_dirs[ii]],
					  ti[1][to_dirs[jj]],
					  i,
					  margin,
					  p,
					  M);

		double amt = slack*timer_units;
		char unit;

		if (fabs(amt) < 1e-9) {
		  amt *= 1e12;
		  unit = 'p';
		}
		else if (fabs(amt) < 1e-6) {
		  unit = 'n';
		  amt *= 1e9;
		}
		else {
		  unit = 'u';
		  amt *= 1e6;
		}
		printf ("[%g %cs]%c%c%s", my_round_2 (amt), unit,
			from_dirs[ii] ? '+' : '-',
			to_dirs[jj] ? '+' : '-',
			(slack < 0) ? "*ER " : " ");
	      }
	    }
	    printf ("\n");
	  }
	}
      }
      agt->queryFree (l[0]);
      agt->queryFree (l[1]);
    }
  }
  
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

int process_lib_timeunits (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer == TIMER_NONE) {
    fprintf (stderr, "%s: timer needs to be initialized.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  Assert (agt, "What?");

  save_to_log (argc, argv, "s");
  LispSetReturnString (agt->getTimeString());
  
  return LISP_RET_STRING;
}

/*
  PhyDB callbacks
*/

static int num_constraint_callback (void)
{
  return agt->getNumConstraints ();
}

static int num_perf_tags (void)
{
  return 1;
}

static double get_perf_weight (int id)
{
  return 1.0;
}

static double get_perf_slack (int id)
{
  return 0.0;
}

static void get_violated_perf (std::vector<int> &v)
{
  v.clear();
}

static void get_violated_perf_witness (int id, std::vector<phydb::ActEdge> &path)
{
  
}


static void set_global_topK (int k)
{
  agt->setTopK (k);
}

static void set_constraint_topK (int id, int k)
{
  agt->setTopK_id (id, k);
}


static void incremental_update_timer (void)
{
  agt->incrementalUpdate ();
}

static double get_worst_slack (int constraint_id)
{
  double timer_units;
  TaggedTG *tg;
  cyclone_constraint *cyc;

  _set_delay_units ();
  timer_units = agt->getTimeUnits ();
  tg = agt->getTaggedTG ();

  cyc = agt->_getConstraint (constraint_id);
  if (!cyc) {
    return 0.0;
  }

  timing_info *t_from, *t_to;
  TaggedTG::constraint *c;

  c = tg->getConstraint (cyc->tg_id);

  if (c->error) {
    /* error in constraint */
    return 0.0;
  }

  t_from = agt->queryTransition (c->from, cyc->from_dir);
  t_to = agt->queryTransition (c->to, cyc->to_dir);

  double p;
  int M;
  agt->getPeriod (&p, &M);

  int slack_set = 0;
  double slack = 0;
  double margin = c->margin*act_delay_units/timer_units;
  
  for (int i=0; i < M; i++) {
    double amt = get_slack (c, t_from, t_to, i, margin, p, M);

    if (!slack_set) {
      slack = amt;
      slack_set = 1;
    }
    else if (amt < slack) {
      slack = amt;
    }
  }

  /* return slack in timer units */
  return slack;
}

static std::vector<double> get_slack_callback (const std::vector<int> &ids)
{
  std::vector<double> slk;

  slk.clear();
  
  for (int i=0; i < ids.size(); i++) {
    slk.push_back (get_worst_slack (ids[i]));
    agt->addCheck (ids[i]);
  }
  return slk;
}

#if defined (FOUND_phydb)
static void get_witness_callback (int constraint,
				  std::vector<phydb::ActEdge> &patha,
				  std::vector<phydb::ActEdge> &pathb)
{
  TaggedTG *tg = agt->getTaggedTG ();
  cyclone_constraint *cyc = agt->_getConstraint (constraint);
  if (!cyc) {
    return;
  }
  if (cyc->witness_ready == 0) {
    /* error */
    return;
  }
  if (cyc->witness_ready == 1) {
    agt->computeWitnesses ();
  }
  /* a < b : a should be fast, b should be slow */
  cyclone::TimingPath pa, pb;

  agt->getFastEndPaths (constraint, pa);
  agt->getSlowEndPaths (constraint, pb);

  agt->convertPath (pa, patha);
  agt->convertPath (pb, pathb);
}

static void get_slow_witness_callback (int constraint,
				       std::vector<phydb::ActEdge> &path)
{
  TaggedTG *tg = agt->getTaggedTG ();
  cyclone_constraint *cyc = agt->_getConstraint (constraint);
  if (!cyc) {
    return;
  }
  if (cyc->witness_ready == 0) {
    /* error */
    return;
  }
  if (cyc->witness_ready == 1) {
    agt->computeWitnesses ();
  }
  /* a < b : a should be fast, b should be slow */
  cyclone::TimingPath p;
  agt->getSlowEndPaths (constraint, p);
  agt->convertPath (p, path);
}

static void get_fast_witness_callback (int constraint,
				       std::vector<phydb::ActEdge> &path)
{
  TaggedTG *tg = agt->getTaggedTG ();
  cyclone_constraint *cyc = agt->_getConstraint (constraint);
  if (!cyc) {
    return;
  }
  if (cyc->witness_ready == 0) {
    /* error */
    return;
  }
  if (cyc->witness_ready == 1) {
    agt->computeWitnesses ();
  }
  /* a < b : a should be fast, b should be slow */
  cyclone::TimingPath p;
  agt->getFastEndPaths (constraint, p);
  agt->convertPath (p, path);
}


#endif

struct _violation_pair {
  int idx;
  double slack;
};

static int _sortviolationsfn (char *a, char *b)
{
  _violation_pair *p1 = (_violation_pair *)a;
  _violation_pair *p2 = (_violation_pair *)b;
  if (p1->slack < p2->slack) {
    return -1;
  }
  else if (p1->slack == p2->slack) {
    return 0;
  }
  else {
    return 1;
  }
}

static void get_violated_constraints (std::vector<int> &violations)
{
  int nc = agt->getNumConstraints ();
  TaggedTG *tg = agt->getTaggedTG ();

  violations.clear();

  A_DECL (_violation_pair, v);
  A_INIT (v);
  
  for (int i=0; i < nc; i++) {
    double slack = get_worst_slack (i);
    if (slack < 0) {
      A_NEW (v, _violation_pair);
      A_NEXT (v).idx = i;
      A_NEXT (v).slack = slack;
      A_INC (v);
    }
  }

  if (A_LEN (v) > 0) {
    mygenmergesort ((char *)v, sizeof (_violation_pair), A_LEN (v),
		    _sortviolationsfn);
    for (int i=0; i < A_LEN (v); i++) {
      violations.push_back (v[i].idx);
    }
  }

  A_FREE (v);

  return;
}

int process_timer_num_constraints (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return LISP_RET_ERROR;
  }

  int n = num_constraint_callback ();
  LispSetReturnInt (n);
  
  save_to_log (argc, argv, "s");

  return LISP_RET_INT;
}

int process_timer_get_violations (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return LISP_RET_ERROR;
  }

  std::vector<int> res;

  get_violated_constraints (res);
  
  LispSetReturnListStart ();

  for (int i=0; i < res.size(); i++) {
    LispAppendReturnInt (res[i]);
  }

  LispSetReturnListEnd ();
  
  return LISP_RET_LIST;
}


static int _path_search (TaggedTG *tg,
			 AGvertex *root,
			 AGvertex *v1, int *tick1,
			 AGvertex *v2, int *tick2,
			 int tick_limit)
{
  struct iHashtable *H[2];
  ihash_bucket_t *b;
  ihash_iter_t it;
  int change, cur;

  H[0] = ihash_new (8);
  H[1] = ihash_new (8);

  b = ihash_add (H[0], root->vid);
  b->i = 0; // no ticks

  cur = 1;
  int warn = 0;

#if 0  
  int round = 0;
#endif
  
  do {
    int found = 0;

#if 0    
    printf ("\n~~~~~~ Round %d ~~~~~~\n", round++);
#endif
    
    ihash_clear (H[cur]);
    cur = 1 - cur;
    change = 0;

    ihash_iter_init (H[cur], &it);
    while ((b = ihash_iter_next (H[cur], &it))) {
      int vid = (int) b->key;

#if 0
      const char *tmp = tg->getVertex (vid)->getInfo()->info();
      printf ("> %s [ticks=%d]\n", tmp, b->i);
#endif

      if (vid == v1->vid && (b->i == *tick1)) {
#if 0
	printf ("  >> found from\n");
#endif
	found++;
      }
      else if (vid == v1->vid) {
	//warning (">> found path from root to lhs, tick mismatch");
      }

      if (vid == v2->vid && (b->i == *tick2)) {
#if 0
	printf ("  >> found to\n");
#endif
	found++;
      }
      else if (vid == v2->vid) {
	//warning (">> found path from root to rhs, tick mismatch");
      }

      if (found == 2) {
	ihash_free (H[0]);
	ihash_free (H[1]);
	return 0;
      }

      if (!ihash_lookup (H[1-cur], b->key)) {
	ihash_add (H[1-cur], b->key)->i = b->i;
      }
      
      AGvertexFwdIter fw(tg, vid);
      for (fw = fw.begin(); fw != fw.end(); fw++) {
	AGedge *e = (*fw);
	TimingEdgeInfo *ei = (TimingEdgeInfo *) e->getInfo();
	ihash_bucket_t *newb;
	if (ei->isTicked() && (b->i + 1 > tick_limit)) {
	  // double tick
#if 0
	  printf ("  >> pruned %s\n",
		  tg->getVertex (e->dst)->getInfo()->info());
#endif
	  continue;
	}

#if 0
	printf ("  >> explore %s [arctick=%d]\n", tg->getVertex (e->dst)->getInfo()->info(), ei->isTicked());
#endif
	
	// did we add this already?
	newb = ihash_lookup (H[1-cur], e->dst);
	if (newb) {
	  if (newb->i != (b->i + ei->isTicked())) {
	    warn++;
	    newb->i = MAX(newb->i, b->i + ei->isTicked());
	  }
	}
	else {
	  newb = ihash_add (H[1-cur], e->dst);
	  newb->i = b->i + ei->isTicked();
	}

	// did it exist alraedy?
	newb = ihash_lookup (H[cur], e->dst);
	if (!newb) {
	  change = 1;
	}
      }
    }
  } while (change);

  warn = 0;
  
  b = ihash_lookup (H[cur], v1->vid);
  if (!b) {
    warn |= 1;
  }
  else if (b->i != *tick1) {
    *tick1 = b->i;
    warn |= 2;
  }
  b = ihash_lookup (H[cur], v2->vid);
  if (!b) {
    warn |= (1 << 2);
  }
  else if (b->i != *tick2) {
    *tick2 = b->i;
    warn |= (2 << 2);
  }
    
  ihash_free (H[0]);
  ihash_free (H[1]);

  return warn;
}


int process_timer_check_constraint (int argc, char **argv)
{
  if (!std_argcheck (argc == 2 ? 3 : argc, argv, 3, "cid [ticks]", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return LISP_RET_ERROR;
  }

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);

  int tick_lim = 1;

  if (argc == 3) {
    tick_lim = atoi (argv[2]);
  }

  cyclone_constraint *cyc = agt->_getConstraint (atoi(argv[1]));

  if (!cyc) {
    fprintf (stderr, "%s: unknown constraint #%s\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }

  TaggedTG::constraint *tgc = tg->getConstraint (cyc->tg_id);

  printf ("### Constraint-id: %d [elaborated-id: %d] ###\n", cyc->tg_id + 1,
	  atoi (argv[1]));
  
  char buf[1024];
  ActPin *xp;

  AGvertex *rootv, *fromv, *tov;

  xp = agt->tgVertexToPin (tgc->root);
  xp->sPrintFullName (buf, 1024);
  printf ("  %s%c : ", buf, cyc->root_dir ? '+' : '-');

  rootv = xp->getNetVertex();
  Assert ((rootv->vid & 1) == 0, "What?");
  if (cyc->root_dir) {
    /* add one to vertex */
    rootv = tg->getVertex (rootv->vid + 1);
  }
  
  xp = agt->tgVertexToPin (tgc->from);
  xp->sPrintFullName (buf, 1024);
  printf ("%s%c%s < ", buf, cyc->from_dir ? '+' : '-',
	  tgc->from_tick ? "*" : "");

  fromv = xp->getNetVertex();
  Assert ((fromv->vid & 1) == 0, "What?");
  if (cyc->from_dir) {
    /* add one to vertex */
    fromv = tg->getVertex (fromv->vid + 1);
  }
  
  xp = agt->tgVertexToPin (tgc->to);
  xp->sPrintFullName (buf, 1024);
  printf ("%s%c%s ", buf, cyc->to_dir ? '+' : '-', tgc->to_tick ? "*" : "");
  printf ("\n");

  tov = xp->getNetVertex();
  Assert ((tov->vid & 1) == 0, "What?");
  if (cyc->to_dir) {
    /* add one to vertex */
    tov = tg->getVertex (tov->vid + 1);
  }

  int tick1 = tgc->from_tick;
  int tick2 = tgc->to_tick;
  int res = _path_search (tg, rootv, fromv, &tick1, tov, &tick2, tick_lim);
  if (res) {
    if (res & 0x3) {
      printf (">> could not find path from root to lhs");
      if ((res & 0x3) == 2) {
	printf (" (wrong ticks, got %d)", tick1);
      }
      printf ("\n");
    }
    if (res >> 2) {
      printf (">> could not find path from root to rhs");
      if (((res>> 2) & 0x3) == 2) {
	printf (" (wrong ticks, got %d)", tick2);
      }
      printf ("\n");
    }
    return LISP_RET_FALSE;
  }
  return LISP_RET_TRUE;
}



int process_timer_get_slack (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "cid", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return LISP_RET_ERROR;
  }

  std::vector<int> inp;
  std::vector<double> res;

  inp.clear();
  inp.push_back (atoi (argv[1]));

  res = get_slack_callback (inp);

  LispSetReturnFloat (res[0]);

  return LISP_RET_FLOAT;
}


int process_timer_save (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (!F.tp) {
    fprintf (stderr, "%s: need to build the timing graph!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  
  if (!tg) {
    fprintf (stderr, "%s: no timing graph\n", argv[0]);
    return LISP_RET_ERROR;
  }

  FILE *fp = fopen (argv[1], "w");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for writing\n", argv[0],
	     argv[1]);
    return LISP_RET_ERROR;
  }

  tg->printDot (fp, NULL);

  fclose (fp);

  return LISP_RET_TRUE;
}

#if defined (FOUND_phydb)

static void print_act_edge (phydb::ActEdge &e)
{
  ((ActPin *)e.source)->Print (stdout);
  printf (" -> ");
  ((ActPin *)e.target)->Print (stdout);
  printf (" {delay: %g}", e.delay);
}

int process_timer_get_witness (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "cid", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return LISP_RET_ERROR;
  }

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);


  std::vector<phydb::ActEdge> patha, pathb;

  get_witness_callback (atoi (argv[1]), patha, pathb);

  cyclone_constraint *cyc = agt->_getConstraint (atoi(argv[1]));
  TaggedTG::constraint *tgc = tg->getConstraint (cyc->tg_id);

  printf ("### Constraint-id: %d [elaborated-id: %d] ###\n", cyc->tg_id + 1,
	  atoi (argv[1]));
  
  char buf[1024];
  ActPin *xp;

  xp = agt->tgVertexToPin (tgc->root);
  xp->sPrintFullName (buf, 1024);
  printf (" %s%c : ", buf, cyc->root_dir ? '+' : '-');
  xp = agt->tgVertexToPin (tgc->from);
  xp->sPrintFullName (buf, 1024);
  printf ("%s%c%s < ", buf, cyc->from_dir ? '+' : '-',
	  tgc->from_tick ? "*" : "");
  xp = agt->tgVertexToPin (tgc->to);
  xp->sPrintFullName (buf, 1024);
  printf ("%s%c%s ", buf, cyc->to_dir ? '+' : '-', tgc->to_tick ? "*" : "");
  printf ("\n");

  printf ("Fast path that is too slow:\n");
  for (int i=0; i < patha.size(); i++) {
    print_act_edge (patha[i]);
    printf ("\n");
  }
  if (patha.size() == 0) {
    printf (" *** WARNING: empty path!\n");
  }
  printf ("-----\n");
  printf ("Slow path that is too fast:\n");
  for (int i=0; i < pathb.size(); i++) {
    print_act_edge (pathb[i]);
    printf ("\n");
  }
  if (pathb.size() == 0) {
    printf (" *** WARNING: empty path!\n");
  }
  printf ("-----\n");

  return LISP_RET_TRUE;
}

void timer_phydb_link (phydb::PhyDB *phydb);

static int process_timer_phydb (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (F.timer == TIMER_NONE) {
    fprintf (stderr, "%s: timer not initialized.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb not initialized.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  timer_phydb_link (F.phydb);

  return LISP_RET_TRUE;
}

#endif

static struct LispCliCommand timer_cmds[] = {

  { NULL, "Timing and power analysis", NULL },
  
  { "lib-read", "<file> - read liberty timing file and return handle",
    process_read_lib },

  { "lib-merge", "<lh> <file> - merge <file> into liberty file handle <lh>",
    process_merge_lib },

  { "time-units", "- returns string for time units", process_lib_timeunits },
  
  { "build-graph", "- build timing graph", process_timer_build },

  { "tick", "<net1>+/- <net2>+/- - add a tick (iteration boundary) to the timing graph", process_timer_tick },

  { "add-constraint", "<root>+/- <fast>+/- <slow>+/- [margin] - add a timing fork constraint", process_timer_addconstraint },
  
  { "init", "<l1> <l2> ... - initialize analysis engine with specified liberty handles",
    process_timer_init },

  { "spef", "<file> - read in SPEF parasitics from <file>",
    process_timer_spef },

#if defined(FOUND_phydb)
  { "phydb-link",
    "- link timer to phydb for timing-driven physical design flow",
    process_timer_phydb 
  },
#endif

  { "run", "- run timing analysis, and returns list (p M)",
    process_timer_run },

  { "crit", "- show critical cycle", process_timer_cycle },

  { "info", "<net> - display information about the net",
    process_timer_info },
  { "constraint", "[<net>] - display information about all timing forks that involve <net>",
    process_timer_constraint },

  { "num-constraints", "- returns the number of constraints in the design",
    process_timer_num_constraints },

  { "get-violations", "- returns a list of constraint ids (cids) that have violations",
    process_timer_get_violations },

  { "check-constraint", "cid - does a path analysis to check paths for timing fork #<cid> exist",
    process_timer_check_constraint },

  { "get-slack", "cid - returns the slack of the violating constraint id #cid",
    process_timer_get_slack },

#if defined(FOUND_phydb)  
  { "get-witness", "cid - displays the witness for the violation",
    process_timer_get_witness },
#endif  

  { "save", "<file> - save abstract timing graph to file in graphviz format",
    process_timer_save }

};


#if defined(FOUND_phydb)

void timer_phydb_link (phydb::PhyDB *phydb)
{
  /* find out how many constraints there are */
  phydb->SetGetNumConstraintsCB (num_constraint_callback);

  /* set the k-value for top-k paths for correctness */
  phydb->SetSpecifyTopKsCB (set_global_topK);

  /* set the k-value for top-k paths for performance */
  phydb->SetSpecifyTopKCB (set_constraint_topK);

  /* incremental update */
  phydb->SetUpdateTimingIncrementalCB (incremental_update_timer);

  /* return the slack for the specified constraints */
  phydb->SetGetSlackCB (get_slack_callback);

  /* callback to get the ids of violated constraints */
  phydb->SetGetViolatedTimingConstraintsCB (get_violated_constraints);

  /* get first witness */
  //phydb->SetGetWitnessCB (get_witness_callback);

  /* get next witnesses */
  phydb->SetGetSlowWitnessCB (get_slow_witness_callback);
  phydb->SetGetFastWitnessCB (get_fast_witness_callback);
  
  /* get # of performance tags */
  phydb->SetGetNumPerformanceConstraintsCB (num_perf_tags);

  /* weight of each tag */
  phydb->SetGetPerformanceConstraintWeightCB (get_perf_weight);

  /* slack of each tag */
  phydb->SetGetPerformanceSlack(get_perf_slack);

  /* violated constraints */
  phydb->SetGetViolatedPerformanceConstraintsCB (get_violated_perf);
  
  /* violated constraints */
  phydb->SetGetPerformanceWitnessCB (get_violated_perf_witness);

  agt->linkPhyDB (phydb);
}

#endif


#endif

void timer_cmds_init (void)
{
#ifdef FOUND_timing_actpin
  LispCliAddCommands ("timer", timer_cmds,
            sizeof (timer_cmds)/sizeof (timer_cmds[0]));
#endif
}
