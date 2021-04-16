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
#include <lispCli.h>
#include "all_cmds.h"
#include "ptr_manager.h"
#include "flow.h"
#include "actpin.h"

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
    return 0;
  }

  lib = read_lib_file (argv[1]);
  if (!lib) {
    fprintf (stderr, "%s: could not open liberty file `%s'\n", argv[0], argv[1]);
    return 0;
  }
  save_to_log (argc, argv, "s");

  LispSetReturnInt (ptr_register ("liberty", lib));

  return 2;
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
    return 0;
  }
  if (!F.act_toplevel) {
    fprintf (stderr, "%s: need top level of design set\n", argv[0]);
    return 0;
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
    return 0;
  }

  F.timer = TIMER_INIT;

  save_to_log (argc, argv, "i*");
  
  return 1;
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
    return 0;
  }
  if (F.timer != TIMER_INIT) {
    fprintf (stderr, "%s: timer needs to be initialized\n", argv[0]);
    return 0;
  }

  const char *msg = timer_run ();
  if (msg) {
    fprintf (stderr, "%s: error running timer\n -> %s\n", argv[0], msg);
    return 0;
  }
  save_to_log (argc, argv, "");

  F.timer = TIMER_RUN;
  
  return 5;
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
    return 0;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
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

  ActId *id = ActId::parseId (argv[1]);
  if (!id) {
    fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[1]);
    return 0;
  }

  Array *x;
  InstType *itx;

  /* -- validate the type of this identifier -- */
  itx = F.act_toplevel->CurScope()->FullLookup (id, &x);
  if (itx == NULL) {
    fprintf (stderr, "%s: could not find identifier `%s'\n", argv[0], argv[1]);
    return 0;
  }
  if (!TypeFactory::isBoolType (itx)) {
    fprintf (stderr, "%s: identifier `%s' is not a signal (", argv[0], argv[1]);
    itx->Print (stderr);
    fprintf (stderr, ")\n");
    return 0;
  }
  if (itx->arrayInfo() && (!x || !x->isDeref())) {
    fprintf (stderr, "%s: identifier `%s' is an array.\n", argv[0], argv[1]);
    return 0;
  }

  /* -- check all the de-references are valid -- */
  if (!id->validateDeref (F.act_toplevel->CurScope())) {
    fprintf (stderr, "%s: `%s' contains an array reference.\n", argv[0], argv[1]);
    return 0;
  }

  int goff = F.sp->globalBoolOffset (id);

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  Assert (tg, "What?");

  int vid = 2*(tg->globOffset() + goff);

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

  return 1;
}


int process_timer_constraint (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, argc == 1 ? 1 : 2, "[<net>]", STATE_EXPANDED)) {
    return 0;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
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

  int goff;
  
  if (argc == 2) {
    ActId *id = ActId::parseId (argv[1]);
    if (!id) {
      fprintf (stderr, "%s: could not parse identifier `%s'\n", argv[0], argv[1]);
      return 0;
    }

    Array *x;
    InstType *itx;

    /* -- validate the type of this identifier -- */
    itx = F.act_toplevel->CurScope()->FullLookup (id, &x);
    if (itx == NULL) {
      fprintf (stderr, "%s: could not find identifier `%s'\n", argv[0], argv[1]);
      return 0;
    }
    if (!TypeFactory::isBoolType (itx)) {
      fprintf (stderr, "%s: identifier `%s' is not a signal (", argv[0], argv[1]);
      itx->Print (stderr);
      fprintf (stderr, ")\n");
      return 0;
    }
    if (itx->arrayInfo() && (!x || !x->isDeref())) {
      fprintf (stderr, "%s: identifier `%s' is an array.\n", argv[0], argv[1]);
      return 0;
    }

    /* -- check all the de-references are valid -- */
    if (!id->validateDeref (F.act_toplevel->CurScope())) {
      fprintf (stderr, "%s: `%s' contains an array reference.\n", argv[0], argv[1]);
      return 0;
    }

    goff = F.sp->globalBoolOffset (id);
  }
  else {
    goff = 0;
  }

  TaggedTG *tg = (TaggedTG *) F.tp->getMap (F.act_toplevel);
  Assert (tg, "What?");

  int vid;
  if (argc != 1) {
    vid = 2*(tg->globOffset() + goff);
  }
  else {
    vid = -1;
  }

  int M;
  double p;
  
  timer_get_period (&p, &M);

  int tmp = tg->numConstraints();
  int nzeros = 0;
  while (tmp > 0) {
    nzeros++;
    tmp /= 10;
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

	char buf1[1024],  buf2[1024];
	ti[0][0]->pin->sPrintFullName (buf1, 1024);
	ti[1][0]->pin->sPrintFullName (buf2, 1024);

	printf ("[%*d/%*d] %s -> %s%s\n", nzeros, i+1, nzeros,
		tg->numConstraints(), buf1, buf2,
		c->error ? " *root-err*" : "");
	
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

	      /* XXX: this isn't right... */
	      double x = tdst - tsrc + adj;

	      printf (" %g%c%c", my_round_2 (x),
		      from_dirs[ii] ? '+' : '-',
		      to_dirs[jj] ? '+' : '-');
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

  return 1;
}



static struct LispCliCommand timer_cmds[] = {

  { NULL, "Timing and power analysis", NULL },
  { "lib-read", "<file> - read liberty timing file and return handle",
    process_read_lib },
  { "init", "<l1> <l2> ... - initialize timer with specified liberty handles",
    process_timer_init },
  { "run", "- run timing analysis, and returns list (p M)",
    process_timer_run },

  { "info", "<net> - display information about the net",
    process_timer_info },
  { "constraint", "[<net>] - display information about all timing forks that involve <net>",
    process_timer_constraint }
  
};

#endif

#if defined(FOUND_dali) && defined(FOUND_phydb)

static int process_phydb_init (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }

  if (F.act_toplevel == NULL) {
    fprintf (stderr, "%s: no top-level process specified\n", argv[0]);
    return 0;
  }

  if (F.cell_map != 1) {
    fprintf (stderr, "%s: phydb requires the design to be mapped to cells.\n", argv[0]);
    return 0;
  }

  if (F.ckt_gen != 1) {
    fprintf (stderr, "%s: phydb requires the transistor netlist to be created.\n", argv[0]);
    return 0;
  }

  if (F.phydb != NULL) {
    fprintf (stderr, "%s: phydb already initialized!\n", argv[0]);
    return 0;
  }

  F.phydb = new phydb::PhyDB();
  save_to_log (argc, argv, "");

  return 1;
}

static int process_phydb_close (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: no database!\n", argv[0]);
    return 0;
  }
 
  delete F.phydb;
  F.phydb = NULL;

  save_to_log (argc, argv, "");

  return 1;
}

static int process_phydb_read_lef (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return 0;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return 0;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return 0;
  }
  fclose (fp);

  if (F.phydb_lef) {
    fprintf (stderr, "%s: already read in LEF; continuing anyway.\n",
        argv[0]);
  }
  
  F.phydb->ReadLef (argv[1]);
  F.phydb_lef = 1;

  save_to_log (argc, argv, "s");

  return 1;
}

static int process_phydb_read_def (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return 0;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return 0;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return 0;
  }
  fclose (fp);
  
  if (F.phydb_def) {
    fprintf (stderr, "%s: already read in DEF file! Command ignored.\n",
        argv[0]);
    return 0;
  }
  
  F.phydb->ReadDef (argv[1]);
  F.phydb_def = 1;
  save_to_log (argc, argv, "s");

  return 1;
}

static int process_phydb_read_cell (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return 0;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return 0;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return 0;
  }
  fclose (fp);

  if (F.phydb_cell) {
    fprintf (stderr, "%s: reading additional .cell file; continuing anyway\n",
        argv[0]);
  }
  
  F.phydb->ReadCell (argv[1]);
  F.phydb_cell = 1;
  save_to_log (argc, argv, "s");

  return 1;
}

static struct LispCliCommand phydb_cmds[] = {
  { NULL, "Physical database access", NULL },
  
  { "init", "- initialize physical database", process_phydb_init },
  { "read-lef", "<file> - read LEF and populate database",
    process_phydb_read_lef },
  { "read-def", "<file> - read DEF and populate database",
    process_phydb_read_def },
  { "read-cell", "<file> - read CELL file and populate database", 
    process_phydb_read_cell },
  { "close", "- tear down physical database", process_phydb_close }

};

static int process_dali_init (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<verbosity level>", STATE_EXPANDED)) {
    return 0;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return 0;
  }

  if (F.dali != NULL) {
    fprintf (stderr, "%s: dali already initialized!\n", argv[0]);
    return 0;
  }

  F.dali = new dali::Dali(F.phydb, argv[1]);
  save_to_log (argc, argv, "i");

  return 1;
}

static int process_dali_place_design (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<target density>", STATE_EXPANDED)) {
    return 0;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return 0;
  }

  double density = -1;
  try {
    density = std::stod(argv[1]);
  } catch (...) {
    fprintf (stderr, "%s: invalid target density!\n", argv[1]);
    return 0;
  }

  F.dali->StartPlacement(density);
  save_to_log (argc, argv, "f");

  return 1;
}

static int process_dali_place_io (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<metal>", STATE_EXPANDED)) {
    return 0;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return 0;
  }

  F.dali->SimpleIoPinPlacement(argv[1]);
  save_to_log (argc, argv, "s");

  return 1;
}

static int process_dali_export_phydb (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return 0;
  }

  F.dali->ExportToPhyDB();
  save_to_log (argc, argv, "");

  return 1;
}

static int process_dali_close (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }

  if (F.dali != NULL) {
    F.dali->Close();
    delete F.dali;
    F.dali = NULL;
  }
  save_to_log (argc, argv, "");

  return 1;
}

static struct LispCliCommand dali_cmds[] = {
  { NULL, "Placement", NULL },
  
  { "init", "<verbosity_level(0-5)> - initialize Dali placement engine", process_dali_init },
  { "place-design", "<target_density> - place design", process_dali_place_design },
  { "place-io", "<metal_name> - place I/O pins", process_dali_place_io },
  { "export-phydb", "- export placement to phydb", process_dali_export_phydb },
  { "close", "- close Dali", process_dali_close }

};

#endif  

void pandr_cmds_init (void)
{
#ifdef FOUND_galois_eda
  LispCliAddCommands ("timer", timer_cmds,
            sizeof (timer_cmds)/sizeof (timer_cmds[0]));
#endif

#if defined(FOUND_dali) && defined(FOUND_phydb)
  LispCliAddCommands ("phydb", phydb_cmds,
            sizeof (phydb_cmds)/sizeof (phydb_cmds[0]));

  LispCliAddCommands ("dali", dali_cmds,
            sizeof (dali_cmds)/sizeof (dali_cmds[0]));
#endif
  
}
