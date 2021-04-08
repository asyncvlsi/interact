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
#include "ptr_manager.h"
#include "flow.h"

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


int process_timer_info (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<net>", STATE_EXPANDED)) {
    return 0;
  }

  if (F.timer != TIMER_RUN) {
    fprintf (stderr, "%s: timer needs to be run first\n", argv[0]);
    return 0;
  }

  ActId *id = act_string_to_id (argv[1]);
  if (!id) {
    fprintf (stderr, "%s: could not parse identifier `%s'", argv[0], argv[1]);
    return 0;
  }

  Array *x;
  if (F.act_toplevel->CurScope()->FullLookup (id, &x) == NULL) {
    fprintf (stderr, "%s: could not find identifier `%s'", argv[0], argv[1]);
    return 0;
  }

  /* -- find connection pointer and scope combo -- */

  save_to_log (argc, argv, "s");

  return 1;
}


static struct LispCliCommand timer_cmds[] = {

  { NULL, "Timing and power analysis", NULL },
  { "lib-read", "timer:lib-read <file> - read liberty timing file and return handle",
    process_read_lib },
  { "init", "timer:init <l1> <l2> ... - initialize timer with specified liberty handles",
    process_timer_init },
  { "run", "timer:run - run timing analysis, and returns list (p M)",
    process_timer_run },

  { "info", "timer:info <net> - display information about the net",
    process_timer_info }

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
  
  { "init", "phydb:init - initialize physical database", process_phydb_init },
  { "read-lef", "phydb:read-lef <file> - read LEF and populate database",
    process_phydb_read_lef },
  { "read-def", "phydb:read-def <file> - read DEF and populate database",
    process_phydb_read_def },
  { "read-cell", "phydb:read-cell <file> - read CELL file and populate database", 
    process_phydb_read_cell },
  { "close", "phydb:close - tear down physical database", process_phydb_close }

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
  
  { "init", "dali:init <verbosity_level(0-5)> - initialize placement engine", process_dali_init },
  { "place-design", "dali:place-design <target_density> - place design", process_dali_place_design },
  { "place-io", "dali:place-io <metal_name> - place I/O pins", process_dali_place_io },
  { "export-phydb", "dali:export-phydb - export placement to phydb", process_dali_export_phydb },
  { "close", "dali:close - close dali", process_dali_close }

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
