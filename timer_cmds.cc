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
#include "galois_cmds.h"
#include "ptr_manager.h"
#include "flow.h"
#include "actpin.h"
#include <act/tech.h>

#ifdef FOUND_galois_eda

#include "galois_cmds.h"

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


static int get_net_to_timing_vertex (char *cmd, char *name, int *vid)
{
  ActId *id = ActId::parseId (name);
  int goff;

  if (!F.tp) {
    fprintf (stderr, "%s: cannot run without creating a timing graph!", cmd);
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
  
  goff = F.sp->globalBoolOffset (id);
  
  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  
  *vid = 2*(tg->globOffset() + goff);

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
  const char *msg = timer_create_graph (F.act_design, F.act_toplevel);
  if (msg) {
    fprintf (stderr, "%s: %s\n", argv[0], msg);
    return LISP_RET_ERROR;
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

  int vid1, vid2;

  if (!get_net_to_timing_vertex (argv[0], argv[1], &vid1) ||
      !get_net_to_timing_vertex (argv[0], argv[2], &vid2)) {
    argv[1][len1-1] = dir1 ? '+' : '-';
    argv[2][len2-1] = dir2 ? '+' : '-';
    return LISP_RET_ERROR;
  }
  vid1 += dir1;
  vid2 += dir2;

  argv[1][len1-1] = dir1 ? '+' : '-';
  argv[2][len2-1] = dir2 ? '+' : '-';

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);

  /* find edge from vid1 to vid2 */
  AGvertexFwdIter fw(tg, vid1);
  for (fw = fw.begin(); fw != fw.end(); fw++) {
    AGedge *e = (*fw);
    if (e->dst != vid2) {
      continue;
    }
    TimingEdgeInfo *te = (TimingEdgeInfo *)e->getInfo();
    te->tickEdge();
    //printf ("tick %d -> %d\n", vid1, vid2);
    break;
  }
  if (fw == fw.end()) {
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
  
  A_DECL (int, args);
  A_INIT (args);
  for (int i=1; i < argc; i++) {
    A_NEW (args, int);
    A_NEXT (args) = atoi (argv[i]);
    A_INC (args);
  }
  
  const char *msg = timing_graph_init (F.act_design, F.act_toplevel,
				       args, A_LEN (args));
  
  if (msg) {
    fprintf (stderr, "%s: failed to initialize timer.\n -> %s\n", argv[0], msg);
    return LISP_RET_ERROR;
  }

  F.timer = TIMER_INIT;

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

  const char *msg = timer_run ();
  if (msg) {
    fprintf (stderr, "%s: error running timer\n -> %s\n", argv[0], msg);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "");

  F.timer = TIMER_RUN;
  
  return LISP_RET_LIST;
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

  list_t *l = timer_query (vid);

  if (l) {
    listitem_t *li;
    double p;
    int M;

    timer_get_period (&p, &M);

    for (li = list_first (l); li; li = list_next (li)) {
      struct timing_info *ti = (struct timing_info *) list_value (li);
      char buf[10240];

      ti->pin->sPrintFullName (buf, 10240);
      printf ("%s%c (%s)\n", buf, ti->dir ? '+' : '-',
	      ti->dir ? "rise" : "fall");
      for (int i=0; i < M; i++) {
	printf ("\titer %2d: arr: %g; req: %g; slk: %g\n", i,
		ti->arr[i], ti->req[i], my_round (ti->req[i]-ti->arr[i]));
      }
    }
  }
  timer_query_free (l);
  
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

  cyclone::TimingPath cyc = timer_get_crit ();

  if (cyc.empty()) {
    printf ("%s: No critical cycle.\n", argv[0]);
  }
  else {
    pp_t *pp = pp_init (stdout, output_window_width);
    timer_display_path (pp, cyc);
    pp_stop (pp);
  }
  save_to_log (argc, argv, "s*");
  
  return LISP_RET_TRUE;
}



int process_timer_addconstraint (int argc, char **argv)
{
  if (!std_argcheck (argc == 5 ? 4 : argc, argv, 4, "<root>+/- <fast>+/- <slow>+/- [margin]", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (!F.tp) {
    fprintf (stderr, "%s: need to build timing graph first.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (F.timer == TIMER_RUN) {
    fprintf (stderr, "%s: need to add constraints before running the timer.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  int dir[3];
  int len[3];
  int vid[3];
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
    if (!get_net_to_timing_vertex (argv[0], argv[i+1], &vid[i])) {
      for (int j=0; j <= i; j++) {
	argv[j+1][len[j]-1] = dir[j] ? '+' : '-';
      }
      return LISP_RET_ERROR;
    }
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
  tg->addConstraint (vid[0], vid[1], vid[2], margin);

  save_to_log (argc, argv, "sssi");
  
  return LISP_RET_TRUE;
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
    
  int M;
  double p;
  
  timer_get_period (&p, &M);

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  int tmp = tg->numConstraints();
  int nzeros = 0;
  while (tmp > 0) {
    nzeros++;
    tmp /= 10;
  }

  double delay_units;
  double timer_units;

  if (config_exists ("net.delay")) {
    delay_units = config_get_real ("net.delay");
  }
  else {
    /* rule of thumb: FO4 is roughly F/2 ps where F is in nm units */
    delay_units = config_get_real ("net.lambda")*1e-3;
  }

  if (config_exists ("xcell.units.time_conv")) {
    timer_units = config_get_real ("xcell.units.time_conv");
  }
  else {
    /* default: ps */
    timer_units = 1e-12;
  }

  for (int i=0; i < tg->numConstraints(); i++) {
    TaggedTG::constraint *c;
    c = tg->getConstraint (i);
    if (vid == -1 || (c->root == vid || c->from == vid || c->to == vid)) {
      /* found a constraint! */
      int from_dirs[2];
      int to_dirs[2];
      int nfrom, nto;
      char from_char, to_char;
	  
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
      l[0] = timer_query_driver (c->from);
      l[1] = timer_query_driver (c->to);
      
      if (l[0] && l[1]) {
	timing_info *ti[2][2];
	for (int i=0; i < 2; i++) {
	  ti[i][0] = timer_query_extract_fall (l[i]);
	  ti[i][1] = timer_query_extract_rise (l[i]);
	}
	
	double margin = c->margin*delay_units;
	char buf1[1024],  buf2[1024];
	ti[0][0]->pin->sPrintFullName (buf1, 1024);
	ti[1][0]->pin->sPrintFullName (buf2, 1024);

	printf ("[%*d/%*d] %s -> %s%s", nzeros, i+1, nzeros,
		tg->numConstraints(), buf1, buf2,
		c->error ? " *root-err*" : "");
	if (c->margin != 0) {
	  if (margin < 1e-9) {
	    printf (" [%g ps]", margin*1e12);
	  }
	  else if (margin < 1e-6) {
	    printf (" [%g ns]", margin*1e9);
	  }
	  else {
	    printf (" [%g us]", margin*1e6);
	  }
	}
	printf ("\n");


	for (int i=0; i < M; i++) {
	  int j;
	  double adj;

	  adj = 0;
	  if (c->from_tick == c->to_tick) {
	    j = i;
	  }
	  else if (c->from_tick) {
	    j = (i+M-1) % M;
	    if (j >= i) {
	      adj = -M*p;
	    }
	  }
	  else {
	    j = (i+1) % M;
	    if (j <= i) {
	      adj = M*p;
	    }
	  }
	  printf ("\titer %2d: ", i);
	  
	  for (int ii=0; ii < nfrom; ii++) {
	    for (int jj=0; jj < nto; jj++) {
	      double tsrc, tdst;

	      tsrc = ti[0][from_dirs[ii]]->arr[i];
	      tdst = ti[1][to_dirs[jj]]->arr[j];

	      double x = (tdst - tsrc + adj)*timer_units;
	      char c;
	      double amt;

	      amt = x-margin;

	      if (fabs(amt) < 1e-9) {
		amt *= 1e12;
		c = 'p';
	      }
	      else if (fabs(amt) < 1e-6) {
		c = 'n';
		amt *= 1e9;
	      }
	      else {
		c = 'u';
		amt *= 1e6;
	      }
	      printf ("[%g %cs]%c%c%s", my_round_2 (amt), c,
		      from_dirs[ii] ? '+' : '-',
		      to_dirs[jj] ? '+' : '-',
		      (x < margin ? "*ER" : ""));
	    }
	  }
	  printf ("\n");
	}
      }
      timer_query_free (l[0]);
      timer_query_free (l[1]);
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

  save_to_log (argc, argv, "s");
  LispSetReturnString (timer_get_time_string());
  
  return LISP_RET_STRING;
}

static struct LispCliCommand timer_cmds[] = {

  { NULL, "Timing and power analysis", NULL },
  { "lib-read", "<file> - read liberty timing file and return handle",
    process_read_lib },

  { "time-units", "- returns string for time units", process_lib_timeunits },
  
  { "build-graph", "- build timing graph", process_timer_build },
  { "tick", "<net1>+/- <net2-dir>+/- - add a tick (iteration boundary) to the timing graph", process_timer_tick },
  { "add-constraint", "<root>+/- <fast>+/- <slow>+/- [margin] - add a timing fork constraint", process_timer_addconstraint },
  { "init", "<l1> <l2> ... - initialize timer with specified liberty handles",
    process_timer_init },
  { "run", "- run timing analysis, and returns list (p M)",
    process_timer_run },

  { "crit", "- show critical cycle", process_timer_cycle },

  { "info", "<net> - display information about the net",
    process_timer_info },
  { "constraint", "[<net>] - display information about all timing forks that involve <net>",
    process_timer_constraint }
  
};

#endif

void timer_cmds_init (void)
{
#ifdef FOUND_galois_eda
  LispCliAddCommands ("timer", timer_cmds,
            sizeof (timer_cmds)/sizeof (timer_cmds[0]));
#endif
}
