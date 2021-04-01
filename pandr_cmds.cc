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

#ifdef GALOIS_EDA

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

  F.timer = TIMER_RUN;
  
  return 1;
}


static struct LispCliCommand timer_cmds[] = {

  { NULL, "Timing and power analysis", NULL },
  { "lib-read", "timer:lib-read <file> - read liberty timing file and return handle",
    process_read_lib },
  { "init", "timer:init <l1> <l2> ... - initialize timer with specified liberty handles",
    process_timer_init },
  { "run", "timer:run - run timing analysis",
    process_timer_run }

};

#if 0

static struct LispCliCommand dali_cmds[] = {
  { NULL, "Placement", NULL },
  
  { "init", "dali:init - initialize placement engine", process_dali_init },
  { "set_int", "dali:set_int <name> <ival> - set Dali parameter", process_dali_setint },
  { "set_real", "dali:set_real <name> <rval> - set Dali parameter",
    process_dali_setreal },
  { "run", "dali:run - initialize placement engine", process_dali_init },
};

#endif  
  

#endif

void pandr_cmds_init (void)
{
#ifdef GALOIS_EDA
  LispCliAddCommands ("timer", timer_cmds,
		      sizeof (timer_cmds)/sizeof (timer_cmds[0]));
#endif  
}
