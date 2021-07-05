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
  char buf[1024];
  
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
    int first = 1;
    pp_t *pp = pp_init (stdout, output_window_width);
    pp_puts (pp, "   ");
    pp_setb (pp);
    for (auto x : cyc) {
      pp_lazy (pp, 0);
      if (!first) {
	pp_printf (pp, " .. ");
      }
      first = 0;
      ActPin *p = (ActPin *) x.first;
      TransMode t = x.second;
      p->sPrintFullName (buf, 1024);
      pp_printf (pp, "%s%c", buf, (t == TransMode::TRANS_FALL ? '-' : '+'));
    }
    pp_endb (pp);
    pp_forced (pp, 0);
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



static struct LispCliCommand timer_cmds[] = {

  { NULL, "Timing and power analysis", NULL },
  { "lib-read", "<file> - read liberty timing file and return handle",
    process_read_lib },
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

#if defined(FOUND_phydb)

static int process_phydb_init (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.act_toplevel == NULL) {
    fprintf (stderr, "%s: no top-level process specified\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.cell_map != 1) {
    fprintf (stderr, "%s: phydb requires the design to be mapped to cells.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  ActNetlistPass *np = getNetlistPass();
  if (np->completed()) {
    F.ckt_gen = 1;
  }

  if (F.ckt_gen != 1) {
    fprintf (stderr, "%s: phydb requires the transistor netlist to be created.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.phydb != NULL) {
    fprintf (stderr, "%s: phydb already initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.phydb = new phydb::PhyDB();
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_phydb_close (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: no database!\n", argv[0]);
    return LISP_RET_ERROR;
  }
 
  delete F.phydb;
  F.phydb = NULL;

  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_phydb_read_lef (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);

  F.phydb->ReadLef (argv[1]);
  F.phydb_lef = 1;

  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static void _find_macro (void *cookie, Process *p)
{
  char buf[10240];

  if (!p) {
    return;
  }

  if (!p->isCell()) {
    return;
  }

  F.act_design->msnprintfproc (buf, 10240, p);

  Macro *m = F.phydb->GetMacroPtr (std::string (buf));
  if (!m) {
    return;
  }

  double w, h;
  w = m->GetWidth();
  h = m->GetHeight();

  w = w*1000.0/Technology::T->scale;
  h = h*1000.0/Technology::T->scale;

  LispAppendListStart ();
  LispAppendReturnString (buf);
  LispAppendReturnInt ((long)w);
  LispAppendReturnInt ((long)h);
  LispAppendListEnd ();
}

static int process_phydb_get_used_lef (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.phydb_lef == 0) {
    fprintf (stderr, "%s: no lef file in phydb!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  ActPass *ap = F.act_design->pass_find ("apply");
  ActApplyPass *app;
  if (!ap) {
    app = new ActApplyPass (F.act_design);
  }
  else {
    app = dynamic_cast<ActApplyPass *> (ap);
  }
  Assert (app, "Hmm");

  LispSetReturnListStart ();

  app->setCookie (NULL);
  app->setProcFn (_find_macro);
  app->setChannelFn (NULL);
  app->setDataFn (NULL);
  app->run_per_type (F.act_toplevel);
  app->setProcFn (NULL);

  save_to_log (argc, argv, "s");

  LispSetReturnListEnd ();

  return LISP_RET_LIST;
}


static int process_phydb_read_def (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);
  
  if (F.phydb_def) {
    fprintf (stderr, "%s: already read in DEF file! Command ignored.\n",
        argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.phydb->ReadDef (argv[1]);
  F.phydb_def = 1;
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_read_cell (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);

  if (F.phydb_cell) {
    fprintf (stderr, "%s: reading additional .cell file; continuing anyway\n",
        argv[0]);
  }
  
  F.phydb->ReadCell (argv[1]);
  F.phydb_cell = 1;
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_read_cluster (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open cluster file `%s' for reading\n", argv[0],
        argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);

  
  F.phydb->ReadCluster (argv[1]);
  F.phydb_cluster = 1;
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_write_def (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.phydb->WriteDef (argv[1]);
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static struct LispCliCommand phydb_cmds[] = {
  { NULL, "Physical database access", NULL },
  
  { "init", "- initialize physical database", process_phydb_init },
  { "read-lef", "<file> - read LEF and populate database",
    process_phydb_read_lef },
  { "get-used-lef", "- return list of macros used by the design",
    process_phydb_get_used_lef },
  { "read-def", "<file> - read DEF and populate database",
    process_phydb_read_def },
  { "read-cell", "<file> - read CELL file and populate database", 
    process_phydb_read_cell },
  { "read-cluster", "<file> - read Cluster file and populate database", 
    process_phydb_read_cluster },
  { "write-def", "<file> - write DEF from database",
    process_phydb_write_def},
  { "close", "- tear down physical database", process_phydb_close }

};

#endif //end FOUND_phydb

#if defined(FOUND_dali) 
static int process_dali_init (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<verbosity level>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.dali != NULL) {
    fprintf (stderr, "%s: dali already initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.dali = new dali::Dali(F.phydb, argv[1]);
  save_to_log (argc, argv, "i");

  return LISP_RET_TRUE;
}

static int process_dali_add_welltap (int argc, char **argv)
{
  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  bool res = F.dali->AddWellTaps(argc, argv);
  save_to_log (argc, argv, "s");

  if (!res) {
    return LISP_RET_ERROR;
  }

  return LISP_RET_TRUE;
}

static int process_dali_place_design (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<target_density>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  double density = -1;
  try {
    density = std::stod(argv[1]);
  } catch (...) {
    fprintf (stderr, "%s: invalid target density!\n", argv[1]);
    return LISP_RET_ERROR;
  }

  F.dali->StartPlacement(density);
  save_to_log (argc, argv, "f");

  return LISP_RET_TRUE;
}

static int process_dali_place_io (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<metal>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.dali->SimpleIoPinPlacement(argv[1]);
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_dali_global_place (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<target_density>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  double density = -1;
  try {
    density = std::stod(argv[1]);
  } catch (...) {
    fprintf (stderr, "%s: invalid target density!\n", argv[1]);
    return LISP_RET_ERROR;
  }

  F.dali->GlobalPlace(density);
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_dali_external_refine (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<engine>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.dali->ExternalDetailedPlaceAndLegalize(argv[1]);
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_dali_export_phydb (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.dali->ExportToPhyDB();
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_dali_close (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.dali != NULL) {
    F.dali->Close();
    delete F.dali;
    F.dali = NULL;
  }
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static struct LispCliCommand dali_cmds[] = {
  { NULL, "Placement", NULL },
  
  { "init", "<verbosity_level(0-5)> - initialize Dali placement engine", process_dali_init },
  { "add-welltap", "<-cell cell_name -interval max_microns> [-checker_board] - add well-tap cell", process_dali_add_welltap},
  { "place-design", "<target_density> - place design", process_dali_place_design },
  { "place-io", "<metal_name> - place I/O pins", process_dali_place_io },
  { "global-place", "<target_density> - global placement", process_dali_global_place},
  { "refine-place", "<engine> - refine placement using an external placer", process_dali_external_refine},
  { "export-phydb", "- export placement to phydb", process_dali_export_phydb },
  { "close", "- close Dali", process_dali_close }

};

#endif  

#if defined(FOUND_pwroute) 
  #include "pwroute_cmds.h"
#endif

void pandr_cmds_init (void)
{
#ifdef FOUND_galois_eda
  LispCliAddCommands ("timer", timer_cmds,
            sizeof (timer_cmds)/sizeof (timer_cmds[0]));
#endif

#if defined(FOUND_phydb)
  LispCliAddCommands ("phydb", phydb_cmds,
            sizeof (phydb_cmds)/sizeof (phydb_cmds[0]));
#endif

#if defined(FOUND_dali) 
  LispCliAddCommands ("dali", dali_cmds,
            sizeof (dali_cmds)/sizeof (dali_cmds[0]));
#endif

#if defined(FOUND_pwroute) 
  LispCliAddCommands ("pwroute", pwroute_cmds,
            sizeof (pwroute_cmds)/sizeof (pwroute_cmds[0]));
#endif
  
}
