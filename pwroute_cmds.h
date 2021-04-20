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
#ifndef PWROUTE_CMDS_H
#define PWROUTE_CMDS_H


static int process_pwroute_init (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return 0;
  }

  F.pwroute = new pwroute::PWRoute(F.phydb);
  save_to_log (argc, argv, "i");

  return 1;
}

static int process_pwroute_set_parameters (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 4, "<reinforcement_width, reinforcement_step, cluster_mesh_width>", STATE_EXPANDED)) {
    return 0;
  }

  if (F.pwroute == NULL) {
    fprintf (stderr, "%s: pwroute needs to be initialized!\n", argv[0]);
    return 0;
  }
  
  F.pwroute->SetMeshWidthStep(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
  save_to_log (argc, argv, "");

  return 1;
}

static int process_pwroute_run (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }

  if (F.pwroute == NULL) {
    fprintf (stderr, "%s: pwroute needs to be initialized!\n", argv[0]);
    return 0;
  }

  F.pwroute->RunPWRoute();
  save_to_log (argc, argv, "f");

  return 1;
}


static int process_pwroute_export_phydb (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }

  if (F.pwroute == NULL) {
    fprintf (stderr, "%s: pwroute needs to be initialized!\n", argv[0]);
    return 0;
  }

  F.pwroute->ExportToPhyDB();
  save_to_log (argc, argv, "");

  return 1;
}

static int process_pwroute_close (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return 0;
  }

  if (F.pwroute != NULL) {
    delete F.pwroute;
    F.pwroute = NULL;
  }
  save_to_log (argc, argv, "");
  return 1;
}

static struct LispCliCommand pwroute_cmds[] = {
  { NULL, "pwroute", NULL },
  
  { "init", "-initialize pwroute engine", process_pwroute_init },
  { "set_parameters", "<reinforcement_width, reinforcement_step, cluster_mesh_width> - run pw route with mesh configuration. Default is <8, 16, 2>", 
    process_pwroute_set_parameters},
  { "run", "run pwroute", process_pwroute_run },
  { "export-phydb", "-export power and ground wires to phydb", process_pwroute_export_phydb },
  { "close", "-close pwroute", process_pwroute_close }
};

#endif
