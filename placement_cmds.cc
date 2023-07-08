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

#if defined(FOUND_dali) 
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
  { "place-design", "<target_density> [number_of_threads] - place design", process_dali_place_design },
  { "place-io", "<metal_name> - place I/O pins", process_dali_place_io },
  { "global-place", "<target_density> [number_of_threads] - global placement", process_dali_global_place},
  { "refine-place", "<engine> - refine placement using an external placer", process_dali_external_refine},
  { "export-phydb", "- export placement to phydb", process_dali_export_phydb },
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
  for (auto &node : gg) {
    std::string str = gg.getData (node).name;
    int idx = gg.getData (node).getPart ();
    if (strlen (str.data()) > 0) {
      fprintf (fp, "%s %d\n", str.data(), idx);
    }
  }

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
