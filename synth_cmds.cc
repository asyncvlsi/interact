/*************************************************************************
 *
 *  Copyright (c) 2026 Rajit Manohar
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
#include <act/synth/synth.h>
#include <act/synth/engines.h>
#include <act/expropt.h>


/*************************************************************************
 *
 * Functions to support logic synthesis
 *
 *************************************************************************
 */


/* anonymous namespace to avoid lots of static */
namespace {


ActDynamicPass *getSynthPass ()
{
  ActPass *p = F.act_design->pass_find ("synth");
  ActDynamicPass *dp;
  if (!p) {
    dp = new ActDynamicPass (F.act_design, "synth",
			     "libactchp2prspass.so", "synthesis");
  }
  else {
    dp = dynamic_cast<ActDynamicPass *> (p);
  }
  if (!dp || (dp->loaded() == false)) {
    return NULL;
  }
  return dp;
}

bool _set_synth_defaults (ActDynamicPass *dp, const char *family)
{
  const char *nm = (const char *) dp->getPtrParam ("engine_name");
  if (strcmp (family , "qdi") == 0) {
    if (dp->getPtrParam ("engine") == (void *) gen_ring_engine) {
      warning ("qdi synthesis is a work-in-progress for maelstrom.");
    }
    dp->setParam ("bundled_dpath", false);
    dp->setParam ("bundled_dpath_2phase", false);
    dp->setParam ("bundled_dpath_pulsed", false);
    dp->setParam ("di_dpath", false);
    dp->setParam ("ditest_dpath", false);
  }
  else if (strcmp (family, "bd") == 0) {
    dp->setParam ("bundled_dpath", true);
    dp->setParam ("bundled_dpath_2phase", false);
    dp->setParam ("bundled_dpath_pulsed", false);
    dp->setParam ("di_dpath", false);
    dp->setParam ("ditest_dpath", false);
  }
  else if (strcmp (family, "bd2") == 0) {
    if (strcmp (nm, "sdt") == 0) {
      fprintf (stderr, "bd2 family not supported with sdt engine.\n");
      return false;
    }
    dp->setParam ("bundled_dpath", true);
    dp->setParam ("bundled_dpath_2phase", true);
    dp->setParam ("bundled_dpath_pulsed", false);
    dp->setParam ("di_dpath", false);
    dp->setParam ("ditest_dpath", false);
  }
  else if (strcmp (family, "bdp") == 0) {
    if (strcmp (nm, "sdt") == 0) {
      fprintf (stderr, "bdp family not supported with sdt engine.\n");
      return false;
    }
    dp->setParam ("bundled_dpath", true);
    dp->setParam ("bundled_dpath_2phase", false);
    dp->setParam ("bundled_dpath_pulsed", true);
    dp->setParam ("di_dpath", false);
    dp->setParam ("ditest_dpath", false);
  }
  else if (strcmp (family, "di") == 0) {
    if (strcmp (nm, "sdt") == 0) {
      fprintf (stderr, "di family not supported with sdt engine.\n");
      return false;
    }
    dp->setParam ("bundled_dpath", false);
    dp->setParam ("bundled_dpath_2phase", false);
    dp->setParam ("bundled_dpath_pulsed", false);
    dp->setParam ("di_dpath", true);
    dp->setParam ("ditest_dpath", false);
  }
  else if (strcmp (family, "ditest") == 0) {
    if (strcmp (nm, "sdt") == 0) {
      fprintf (stderr, "ditest family not supported with sdt engine.\n");
      return false;
    }
    dp->setParam ("bundled_dpath", false);
    dp->setParam ("bundled_dpath_2phase", false);
    dp->setParam ("bundled_dpath_pulsed", false);
    dp->setParam ("di_dpath", false);
    dp->setParam ("ditest_dpath", true);
  }
  else {
    return false;
  }
  return true;
}
  

bool _set_engine (ActDynamicPass *dp, const char *engine)
{
  bool found = false;
  static struct {
    const char *nm;
    const char *prefix;
    ActSynthesize *(*f) (const char *, char *, char *, char *);
  } engines[] = {
    { "dataflow", "df", gen_df_engine },
    { "ring", "ring", gen_ring_engine },
    { "maelstrom", "ring", gen_ring_engine },
    { "sdt", "sdt", gen_sdt_engine },
    { "decomp", "decomp", gen_decomp_engine }
  };
  static const char *default_synth = "abc";
  static const char *default_expr = "expr.act";

  for (int i=0; !found && i < sizeof (engines)/sizeof(engines[0]); i++) {
    if (strcmp (engines[i].nm, engine) == 0) {
      /* set the engine */
      dp->setParam ("engine_name", (void *) engines[i].nm);
      dp->setParam ("engine", (void *) engines[i].f);

      /* prefix for output process names */
      dp->setParam ("prefix", (void *) engines[i].prefix);

      /* for original SDT, there are basic unoptimized designs; but
	 not a great idea to use them! */
      dp->setParam ("external_opt", 1);

      /* combinational logic synthesis */
      dp->setParam ("externopt_toolname", (void *) default_synth);

      /* expression file */
      dp->setParam ("expr", (void *) default_expr);

      /* track execution time */
      dp->setParam ("run_time", false);
      found = true;
    }
  }
  if (found) {
    // set the default options
    if (strcmp (engine, "ring") == 0 || strcmp (engine, "maelstrom") == 0) {
      /* ring params */
      
      dp->setParam ("datapath_style", false); /* SSA style is set */
      dp->setParam ("delay_margin", 110);     /* default 10% margin */
    }
    else if (strcmp (engine, "decomp") == 0) {
      /* decomp params */

      /* high cycle time target, so don't do anything by default */
      dp->setParam ("cycle_time_target", 1000000.0);

      /* no projection by default */
      dp->setParam ("project", false);
    }
    return true;
  }
  fprintf (stderr, "Error; valid synthesis engines: ");
  for (int i=0; i < sizeof (engines)/sizeof (engines[0]); i++) {
    if (i != 0) {
      fprintf (stderr, ", ");
    }
    fprintf (stderr, "%s", engines[i].nm);
  }
  fprintf (stderr, "\n");
  return false;
}

int process_synth_init (int argc, char **argv)
{
  ActDynamicPass *dp;
  if (!std_argcheck ((argc == 2 ? 1 : argc), argv, 1, "[engine]",
		     STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  dp = getSynthPass ();
  if (!dp) {
    fprintf (stderr, "%s: could not initialize synthesis pass.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (argc == 2) {
    if (_set_engine (dp, argv[1])) {
      _set_synth_defaults (dp, "qdi");
      return LISP_RET_TRUE;
    }
    else {
      fprintf (stderr, "%s: unknown synthesis engine `%s'.\n", argv[0],
	       argv[1]);
      return LISP_RET_ERROR;
    }
  }
  else {
    if (!_set_engine (dp, "maelstrom")) {
      fprintf (stderr,
	       "%s: Could not find default maelstrom synthesis engine.\n",
	       argv[0]);
      return LISP_RET_ERROR;
    }
  }
  _set_synth_defaults (dp, "qdi");
  return LISP_RET_TRUE;
}

int process_synth_engine (int argc, char **argv)
{
  ActDynamicPass *dp;
  if (!std_argcheck (argc, argv, 3, "<name> <family>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  dp = getSynthPass ();
  if (!dp) {
    fprintf (stderr, "%s: could not initialize synthesis pass.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (_set_engine (dp, argv[1])) {
    if (_set_synth_defaults (dp, argv[2])) {
       return LISP_RET_TRUE;
    }
    fprintf (stderr, "%s: unknown circuit family `%s'.\n", argv[0],
	     argv[2]);
    return LISP_RET_ERROR;
  }
  else {
    fprintf (stderr, "%s: unknown synthesis engine `%s'.\n", argv[0],
	     argv[1]);
    return LISP_RET_ERROR;
  }
}

int process_synth_family (int argc, char **argv)
{
  ActDynamicPass *dp;
  if (!std_argcheck (argc, argv, 2, "<name>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  dp = getSynthPass ();
  if (!dp) {
    fprintf (stderr, "%s: could not initialize synthesis pass.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (!_set_synth_defaults (dp, argv[1])) {
    fprintf (stderr, "%s: invalid family. Options are qdi, bd, bd2, bdp, di, ditest.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  return LISP_RET_TRUE;
}

int process_synth_expropt (int argc, char **argv)
{
  ActDynamicPass *dp;
  if (!std_argcheck (argc, argv, 2, "<name>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  dp = getSynthPass ();
  if (!dp) {
    fprintf (stderr, "%s: could not initialize synthesis pass.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (ExternalExprOpt::engineExists (argv[1])) {
    dp->setParam ("externopt_toolname", (void *) Strdup (argv[1]));
    dp->setParam ("external_opt", 1);
  }
  else {
    fprintf (stderr, "%s: could not find logic optimization library for `%s'\n",
	     argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  return LISP_RET_TRUE;
}

int process_synth_exprfile (int argc, char **argv)
{
  ActDynamicPass *dp;
  if (!std_argcheck (argc, argv, 2, "<name>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  dp = getSynthPass ();
  if (!dp) {
    fprintf (stderr, "%s: could not initialize synthesis pass.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  dp->setParam ("expr", (void *) Strdup (argv[1]));
  return LISP_RET_TRUE;
}

int process_synth_outfile (int argc, char **argv)
{
  ActDynamicPass *dp;
  if (!std_argcheck (argc, argv, 2, "<name>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  dp = getSynthPass ();
  if (!dp) {
    fprintf (stderr, "%s: could not initialize synthesis pass.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  dp->setParam ("out", (void *) Strdup (argv[1]));
  return LISP_RET_TRUE;
}

int process_synth_run (int argc, char **argv)
{
  ActDynamicPass *dp;
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  dp = getSynthPass ();
  if (!dp) {
    fprintf (stderr, "%s: could not initialize synthesis pass.\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (F.act_toplevel == NULL) {
    fprintf (stderr, "%s: no top-level process specified\n", argv[0]);
    return LISP_RET_ERROR;
  }
  dp->run (F.act_toplevel);
  return LISP_RET_TRUE;
}
 
 
struct LispCliCommand synth_cmds[] = {
  { NULL, "ACT logic synthesis", NULL },
  { "init", "[engine] - initialize synthesis engine; default circuit family is qdi", process_synth_init },
  { "engine", "<name> <family> - set synthesis engine to <name>, circuit family to <family>", process_synth_engine },
  { "expropt", "<name> - set name of external combinational logic optimization engine",
    process_synth_expropt },
  { "exprfile", "<name> - set name of ACT file for synthesized expressions",
    process_synth_exprfile },
  { "outfile", "<name> - set name of ACT output file", process_synth_outfile },
  { "run", "- run sythesis pass", process_synth_run }

};

}

void synth_cmds_init (void)
{
  LispCliAddCommands ("synth", synth_cmds, sizeof (synth_cmds)/sizeof (synth_cmds[0]));
}
