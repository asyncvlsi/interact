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
#include <string>
#include <vector>
#include <act/passes.h>
#include <common/list.h>
#include <common/pp.h>
#include <lispCli.h>
#include "all_cmds.h"
#include "ptr_manager.h"
#include "flow.h"
#include "dali_qt_gui_bridge.h"
#include <act/tech.h>

#if defined(FOUND_dali) 
/*
  The thinnest host that is still a real one: it answers every checkpoint with
  "nothing changed".

  It exists to prove the boundary works end to end -- Dali stopping, closing its
  topology-sized engines, calling out of the library into interact, and resuming
  -- before anything is allowed to change a netlist through it. A host that
  returned a delta would prove the same plumbing and also change the placement,
  which would make the two effects impossible to tell apart.
*/
namespace {

class NoChangeCheckpointHost : public dali::TopologyCheckpointHost {
 public:
  dali::TopologyMutationResult ApplyTopologyChange (
      const dali::TopologyCheckpointContext &context,
      const dali::TopologyChangeBatch &batch) override
  {
    printf ("CHECKPOINT_HOST no-change at iteration %d, resuming at %d "
            "(components %zu, nets %zu, batch of %zu site(s))\n",
            context.checkpoint_iteration, context.resume_iteration,
            context.component_count, context.net_count,
            batch.requests.size ());
    fflush (stdout);
    return dali::TopologyMutationResult::NoChange ();
  }
};

NoChangeCheckpointHost interact_no_change_checkpoint_host;

}  // namespace

static int process_dali_init (int argc, char **argv)
{
  if (argc < 2) {
    fprintf (stderr, "Usage: init <verbosity level> [log_file_name]\n");
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

  if (argc == 2) {
    F.dali = new dali::Dali(F.phydb, argv[1], "");
  } else {
    F.dali = new dali::Dali(F.phydb, argv[1], argv[2]);
  }
  /*
    Register the topology-checkpoint host.

    This is the seam where an ACT-authoritative netlist change will eventually
    be produced. It reports no change, so registering it cannot alter a
    placement; what it does establish is that Dali reaches a real host across
    the static link, at a checkpoint Dali chose, with its placement engines
    already closed.

    Dali keeps the loop. It decides whether a checkpoint happens at all, from a
    schedule set in the recipe, and it takes none unless that schedule asks for
    one. The host is only ever asked a question.
  */
  /*
    The no-change host is the default: it proves the boundary works without
    changing anything, and it is what every ordinary run gets. A recipe that
    wants a real topology change arms the ACT-authoritative host instead, with
    dali:topology-site.
  */
  F.dali->SetTopologyCheckpointHost (&interact_no_change_checkpoint_host);
  save_to_log (argc, argv, "i");

  return LISP_RET_TRUE;
}

/*
  dali:topology-site <instance> <expanded-process> <old-pairs> <new-pairs> <techconf>

  Arms the ACT-authoritative host for exactly one delay-site parameter
  increase, and installs it in place of the no-change host. Both pair counts
  are stated rather than derived: this is an integration proof, and deciding how
  far to raise a site from measured slack is a policy question that belongs to a
  later milestone.
*/
static int process_dali_topology_site (int argc, char **argv)
{
  if (argc != 4) {
    fprintf (stderr, "Usage: %s <process-template> <techconf> <liberty>\n"
             "  the template must contain exactly one literal {pairs}, e.g. "
             "plain_delay<{pairs}>\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (!interact_configure_fixed_topology_host (argv[1], argv[2], argv[3])) {
    fprintf (stderr, "%s: the process template must contain exactly one "
             "literal {pairs}\n", argv[0]);
    return LISP_RET_ERROR;
  }
  F.dali->SetTopologyCheckpointHost (interact_fixed_topology_host ());
  save_to_log (argc, argv, "sss");
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
  if (argc < 2) {
    fprintf (stderr, "Usage: place-design <target_density> [number_of_threads]\n");
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

  int number_of_threads = 1;
  if (argc >= 3) {
    try {
      number_of_threads = std::stoi(argv[2]);
    } catch (...) {
      fprintf (stderr, "%s: invalid number of threads!\n", argv[2]);
      return LISP_RET_ERROR;
    }
  }

  bool is_success = F.dali->StartPlacement(density, number_of_threads);
  save_to_log (argc, argv, "f");

  if (!is_success) {
    return LISP_RET_ERROR;
  }
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

  F.dali->IoPinPlacement(argc, argv);
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_dali_global_place (int argc, char **argv)
{
  if (argc < 2) {
    fprintf (stderr, "Usage: global-place <target_density> [number_of_threads]\n");
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

  int number_of_threads = 1;
  if (argc >= 3) {
    try {
      number_of_threads = std::stoi(argv[2]);
    } catch (...) {
      fprintf (stderr, "%s: invalid number of threads!\n", argv[2]);
      return LISP_RET_ERROR;
    }
  }

  F.dali->GlobalPlace(density, number_of_threads);
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

/*
 * Forward a .dali recipe to Dali's own command processor.
 *
 * Placement configuration and execution live in the recipe language rather than
 * in per-option interact commands, so a recipe is portable across the
 * standalone binary, the interactive prompt, and this host.
 */
static int process_dali_source (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file.dali>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  bool res = F.dali->RunCommandFile (argv[1]);
  save_to_log (argc, argv, "s");

  if (!res) {
    return LISP_RET_ERROR;
  }

  return LISP_RET_TRUE;
}

static int process_dali_enable_gui (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<every_snapshot|off>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (!InstallDaliQtGui (F.dali, argv[1])) {
    fprintf (stderr, "%s: Qt GUI support is not available in this interact build\n",
             argv[0]);
    return LISP_RET_ERROR;
  }
  save_to_log (argc, argv, "i");
  return LISP_RET_TRUE;
}

/*
 * Forward one already-tokenized command to Dali's command processor.
 *
 * Dali::ExecuteCommand accepts both bare and `dali:`-namespaced command names,
 * so any command the recipe language supports is reachable without adding a
 * matching interact command for each one.
 */
static int process_dali_cmd (int argc, char **argv)
{
  if (argc < 2) {
    fprintf (stderr, "Usage: %s <command> [args...]\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  std::vector<std::string> arguments;
  for (int i = 1; i < argc; i++) {
    arguments.push_back (std::string (argv[i]));
  }

  bool res = F.dali->ExecuteCommand (arguments);
  save_to_log (argc, argv, "s*");

  if (!res) {
    return LISP_RET_ERROR;
  }

  return LISP_RET_TRUE;
}

/* Return source-level delay repair actions from one synchronized Dali report. */
static int process_dali_timing_repair_plan (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  if (!F.dali->ReportTiming()) {
    return LISP_RET_ERROR;
  }

  const std::vector<dali::TimingRepairSitePlanItem> plan =
      F.dali->LastTimingRepairPlan();
  LispSetReturnListStart ();
  for (const dali::TimingRepairSitePlanItem &item : plan) {
    LispAppendListStart ();
    LispAppendReturnString (item.id.c_str());
    LispAppendReturnString (item.process_name.c_str());
    LispAppendReturnString (item.instance_name.c_str());
    LispAppendReturnString (item.parameter_name.c_str());
    LispAppendReturnInt (item.initial_parameter_value);
    LispAppendReturnFloat (item.worst_slack);
    LispAppendListEnd ();
  }
  LispSetReturnListEnd ();
  save_to_log (argc, argv, "");
  return LISP_RET_LIST;
}

static int process_dali_export_phydb (int argc, char **argv)
{
  if (F.s != STATE_EXPANDED ||
      (argc != 1 && (argc != 2 ||
                     std::string (argv[1]) != "-placement-anchor"))) {
    fprintf (stderr, "Usage: %s [-placement-anchor]\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.dali == NULL) {
    fprintf (stderr, "%s: dali needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.dali->ExportToPhyDB (
      argc == 2 ? dali::PhyDBExportMode::kPlacementAnchor
                : dali::PhyDBExportMode::kFull);
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

static int process_dali_apply_delay_site_map (int argc, char **argv)
{
  if (argc < 3 || ((argc - 1) % 2) != 0) {
    fprintf (stderr, "Usage: %s <instance> <expanded-process> ...\n", argv[0]);
    return LISP_RET_ERROR;
  }

  std::vector<flow_delay_site_replacement> replacements;
  for (int i = 1; i < argc; i += 2) {
    replacements.push_back ({argv[i], argv[i + 1]});
  }

  bool applied = flow_apply_delay_site_map (replacements);
  if (!applied) {
    fprintf (stderr, "apply-delay-site-map: failed\n");
  }
  LispSetReturnInt (applied ? 1 : 0);
  return LISP_RET_INT;
}

#if defined(FOUND_phydb) && defined(FOUND_timing_actpin)
static int process_dali_run_timing_driven_placement_p2b (int argc,
                                                          char **argv)
{
  if (!std_argcheck (argc, argv, 3, "<config> <output-dir>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  const int result = run_timing_driven_placement_p2b (argv[1], argv[2]);
  LispSetReturnInt (result);
  return LISP_RET_INT;
}

#ifdef DALI_P2B_TEST_HARNESS
static int process_dali_run_timing_driven_placement_p2b_rollback_failure_test(
    int argc, char **argv) {
  if (!std_argcheck(argc, argv, 3, "<config> <output-dir>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  const int result = run_timing_driven_placement_p2b_rollback_failure_test(
      argv[1], argv[2]);
  LispSetReturnInt(result);
  return LISP_RET_INT;
}

static int process_dali_run_timing_driven_placement_p2b_begin_failure_test(
    int argc, char **argv) {
  if (!std_argcheck(argc, argv, 3, "<config> <output-dir>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  const int result = run_timing_driven_placement_p2b_begin_failure_test(
      argv[1], argv[2]);
  LispSetReturnInt(result);
  return LISP_RET_INT;
}

static int process_dali_run_timing_driven_placement_p2b_commit_failure_test(
    int argc, char **argv) {
  if (!std_argcheck(argc, argv, 3, "<config> <output-dir>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }
  const int result = run_timing_driven_placement_p2b_commit_failure_test(
      argv[1], argv[2]);
  LispSetReturnInt(result);
  return LISP_RET_INT;
}
#endif
#endif

static struct LispCliCommand dali_cmds[] = {
  { NULL, "Placement", NULL },
  
  { "init", "<verbosity_level(0-5)> - initialize Dali placement engine", process_dali_init },
  { "source", "<file.dali> - run a Dali command recipe", process_dali_source },
  { "enable-gui", "<every_snapshot|off> - enable Dali's Qt placement viewer",
    process_dali_enable_gui },
  { "cmd", "<command> [args...] - run one Dali command", process_dali_cmd },
  { "timing-repair-plan", "- return declared delay repair actions from current timing",
    process_dali_timing_repair_plan },
  { "add-welltap", "<-cell cell_name -interval max_microns> [-checker_board] - add well-tap cell", process_dali_add_welltap},
  { "place-design", "<target_density> [number_of_threads] - legacy; prefer 'source'/'cmd'", process_dali_place_design },
  { "place-io", "<metal_name> - place I/O pins", process_dali_place_io },
  { "global-place", "<target_density> [number_of_threads] - global placement", process_dali_global_place},
  { "refine-place", "<engine> - refine placement using an external placer", process_dali_external_refine},
  { "export-phydb", "[-placement-anchor] - export placement to phydb",
    process_dali_export_phydb },
  { "apply-delay-site-map",
    "<instance> <expanded-process> ... - apply a complete delay-site map",
    process_dali_apply_delay_site_map },
#if defined(FOUND_phydb) && defined(FOUND_timing_actpin)
  { "run-timing-driven-placement-p2b", "<config> <output-dir> - run the Dali-owned P2B transaction controller",
    process_dali_run_timing_driven_placement_p2b },
#ifdef DALI_P2B_TEST_HARNESS
  { "run-timing-driven-placement-p2b-rollback-failure-test",
    "<config> <output-dir> - test-only real-host rollback failure",
    process_dali_run_timing_driven_placement_p2b_rollback_failure_test },
  { "run-timing-driven-placement-p2b-begin-failure-test",
    "<config> <output-dir> - test-only begin failure after a prior commit",
    process_dali_run_timing_driven_placement_p2b_begin_failure_test },
  { "run-timing-driven-placement-p2b-commit-failure-test",
    "<config> <output-dir> - test-only partial artifact promotion failure",
    process_dali_run_timing_driven_placement_p2b_commit_failure_test },
#endif
#endif
  { "topology-site",
    "<process-template> <techconf> <liberty> - install the "
    "ACT-authoritative topology transport",
    process_dali_topology_site },
  { "close", "- close Dali", process_dali_close }

};

#endif


#if defined(FOUND_bipart)


static int process_bipart_partition (int argc, char **argv)
{
  if (!std_argcheck ((argc >= 2 && argc <= 4 )? 4 : argc, argv, 4, "<file> [k] [depth]", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  int K = 2;
  int Cdepth = 20;
  FILE *fp;


  if (argc >= 3) {
    K = atoi (argv[2]);
  }
  if (argc >= 4) {
    Cdepth = atoi(argv[3]);
  }

  /*-- make sure Galois runtime is initialized --*/
  init_galois_shmemsys ();

  bipart::MetisGraph *mG = bipart::biparting (*F.phydb, Cdepth, K);
  
  fp = fopen (argv[1], "w");
  if (!fp) {
    fprintf (stderr, "Could not open file `%s' to write partition info\n",
	     argv[1]);
    delete mG;
    return LISP_RET_ERROR;
  }

  /* write partition */
  bipart::GGraph &gg = *(mG->getGraph ());
  char *outbuf;
  int bufsz = 1024;
  MALLOC (outbuf, char, bufsz);
  for (auto &node : gg) {
    std::string str = gg.getData (node).name;
    int idx = gg.getData (node).getPart ();
    if (strlen (str.data()) > 0) {
      while (strlen (str.data()) >= bufsz) {
         bufsz *= 2;
         REALLOC (outbuf, char, bufsz);
      }
      F.act_design->unmangle_string (str.data(), outbuf, bufsz);
      fprintf (fp, "%s %d\n", outbuf, idx);
    }
  }
  FREE (outbuf);

  fclose (fp);

  delete mG;

  return LISP_RET_TRUE;
}

static struct LispCliCommand bipart_cmds[] = {
  { NULL, "Partitioning", NULL },
  
  { "partition", "<file> [k] [depth] - compute a k-way partition (default 2)",
    process_bipart_partition }
};

#endif


void placement_cmds_init (void)
{

#if defined(FOUND_bipart) 
  LispCliAddCommands ("bipart", bipart_cmds,
		      sizeof (bipart_cmds)/sizeof (bipart_cmds[0]));
#endif

#if defined(FOUND_dali) 
  LispCliAddCommands ("dali", dali_cmds,
            sizeof (dali_cmds)/sizeof (dali_cmds[0]));
#endif

}
