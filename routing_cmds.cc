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
#include <common/config.h>
#include <lispCli.h>
#include "all_cmds.h"
#include "ptr_manager.h"
#include "flow.h"
#include <act/tech.h>

#if defined(FOUND_pwroute) 

static int process_pwroute_init (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 2, "<verbose>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  config_set_default_int ("pwroute.center_vias", 0);
  int center_vias = config_get_int ("pwroute.center_vias");
  if (center_vias != 0 && center_vias != 1) {
    fprintf (stderr, "%s: pwroute.center_vias must be 0 or 1\n", argv[0]);
    return LISP_RET_ERROR;
  }

  //0=unlimited for max cuts
  config_set_default_int ("pwroute.m1_m2_min_cuts", 1);
  config_set_default_int ("pwroute.m1_m2_max_cuts", 0);
  config_set_default_int ("pwroute.m2_m3_min_cuts", 1);
  config_set_default_int ("pwroute.m2_m3_max_cuts", 0);
  int m1_m2_min = config_get_int ("pwroute.m1_m2_min_cuts");
  int m1_m2_max = config_get_int ("pwroute.m1_m2_max_cuts");
  int m2_m3_min = config_get_int ("pwroute.m2_m3_min_cuts");
  int m2_m3_max = config_get_int ("pwroute.m2_m3_max_cuts");

  if (m1_m2_min < 1 || m1_m2_max < 0 || (m1_m2_max != 0 && m1_m2_max < m1_m2_min)) {
    fprintf (stderr,
             "%s: invalid M1-M2 cut limits: min=%d max=%d; "
             "require min >= 1 and max == 0 or max >= min\n",
             argv[0], m1_m2_min, m1_m2_max);
    return LISP_RET_ERROR;
  }
  if (m2_m3_min < 1 || m2_m3_max < 0 || (m2_m3_max != 0 && m2_m3_max < m2_m3_min)) {
    fprintf (stderr,
             "%s: invalid M2-M3 cut limits: min=%d max=%d; "
             "require min >= 1 and max == 0 or max >= min\n",
             argv[0], m2_m3_min, m2_m3_max);
    return LISP_RET_ERROR;
  }


  F.pwroute = new pwroute::PWRoute(F.phydb, atoi(argv[1]));
  F.pwroute->SetCenterVias(center_vias != 0);
  F.pwroute->SetViaCutLimits(m1_m2_min, m1_m2_max, m2_m3_min, m2_m3_max);
  save_to_log (argc, argv, "i");

  return LISP_RET_TRUE;
}

static int process_pwroute_set_parameters (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 4, "<reinforcement_width, reinforcement_step, cluster_mesh_width>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.pwroute == NULL) {
    fprintf (stderr, "%s: pwroute needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.pwroute->SetMeshWidthStep(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_pwroute_set_reinforcement (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 2, "<bool>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.pwroute == NULL) {
    fprintf (stderr, "%s: pwroute needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.pwroute->SetReinforcement(atoi(argv[1]));
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_pwroute_run (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.pwroute == NULL) {
    fprintf (stderr, "%s: pwroute needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.pwroute->RunPWRoute();
  save_to_log (argc, argv, "f");

  return LISP_RET_TRUE;
}


static int process_pwroute_export_phydb (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.pwroute == NULL) {
    fprintf (stderr, "%s: pwroute needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.pwroute->ExportToPhyDB();
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_pwroute_close (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.pwroute != NULL) {
    delete F.pwroute;
    F.pwroute = NULL;
  }
  save_to_log (argc, argv, "");
  return LISP_RET_TRUE;
}

static struct LispCliCommand pwroute_cmds[] = {
  { NULL, "Power Routing for Gridded Cells", NULL },
  
  { "init", "<verbose> -initialize pwroute engine <verbose> ", process_pwroute_init },
  { "set_parameters", "<reinforcement_width, reinforcement_step, cluster_mesh_width> - run pw route with mesh configuration. Default is <8, 16, 2>", 
    process_pwroute_set_parameters},
  { "set_reinforcement", "<bool>, enable/disable reinforcement connection (default: 1) ", process_pwroute_set_reinforcement },
  { "run", "run pwroute", process_pwroute_run },
  { "export-phydb", "-export power and ground wires to phydb", process_pwroute_export_phydb },
  { "close", "-close pwroute", process_pwroute_close }
};

#endif

#if defined(FOUND_sproute) 


static int process_sproute_init (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 2, "<verbose>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  init_galois_shmemsys(0);
  F.sproute = new sproute::SPRoute(F.phydb, atoi(argv[1]));
  save_to_log (argc, argv, "i");

  return LISP_RET_TRUE;
}

static int process_sproute_set_num_threads (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 2, "<int>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.sproute == NULL) {
    fprintf (stderr, "%s: sproute needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.sproute->SetNumThreads(atoi(argv[1]));
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_sproute_set_algo (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 2, "Det/NonDet", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.sproute == NULL) {
    fprintf (stderr, "%s: sproute needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.sproute->SetAlgo(string(argv[1]));
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_sproute_set_max_iteration (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 2, "<int>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.sproute == NULL) {
    fprintf (stderr, "%s: sproute needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.sproute->SetMaxIteration(atoi(argv[1]));
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_sproute_run (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.sproute == NULL) {
    fprintf (stderr, "%s: sproute needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.sproute->Run();
  save_to_log (argc, argv, "f");

  return LISP_RET_TRUE;
}

static int process_sproute_close (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.sproute != NULL) {
    delete F.sproute;
    F.sproute = NULL;
  }
  save_to_log (argc, argv, "");
  return LISP_RET_TRUE;
}

static struct LispCliCommand sproute_cmds[] = {
  { NULL, "Global Routing", NULL },
  
  { "init", "initialize sproute engine", process_sproute_init },
  { "set-num-threads", "set num threads, default = 1", process_sproute_set_num_threads},
  { "set-algo", "Det/NonDet, default is NonDet", process_sproute_set_algo},
  { "set-max-iterations", "set max iterations of maze routing, default = 30", process_sproute_set_max_iteration},
  { "run", "run sproute", process_sproute_run },
  { "close", "close sproute", process_sproute_close }
};

#endif



void routing_cmds_init (void)
{
#if defined(FOUND_pwroute) 
  LispCliAddCommands ("pwroute", pwroute_cmds,
            sizeof (pwroute_cmds)/sizeof (pwroute_cmds[0]));
#endif

#if defined(FOUND_sproute) 
  LispCliAddCommands ("sproute", sproute_cmds,
            sizeof (sproute_cmds)/sizeof (sproute_cmds[0]));
#endif
}
