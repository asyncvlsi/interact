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
#ifndef SPROUTE_CMDS_H
#define SPROUTE_CMDS_H

static void init_galois_shmemsys(int mode = 0) {
  static galois::SharedMemSys *g = NULL;

  if (mode == 0) {
    if (!g) {
      g = new galois::SharedMemSys;
    }
  }
  else {
    if (g) {
      delete g;
      g = NULL;
    }
  }
}

static int process_sproute_init (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  init_galois_shmemsys(0);
  F.sproute = new sproute::SPRoute(F.phydb);
  save_to_log (argc, argv, "i");

  return LISP_RET_TRUE;
}

static int process_sproute_set_num_threads (int argc, char **argv) {
  if (!std_argcheck (argc, argv, 1, "<int>", STATE_EXPANDED)) {
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
  if (!std_argcheck (argc, argv, 1, "Det/NonDet", STATE_EXPANDED)) {
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
  if (!std_argcheck (argc, argv, 1, "<int>", STATE_EXPANDED)) {
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
    //init_galois_shmemsys(1);
    delete F.sproute;
    F.sproute = NULL;
  }
  save_to_log (argc, argv, "");
  return LISP_RET_TRUE;
}

static struct LispCliCommand sproute_cmds[] = {
  { NULL, "sproute", NULL },
  
  { "init", "initialize sproute engine", process_sproute_init },
  { "set-num-threads", "set num threads, default = 1", process_sproute_set_num_threads},
  { "set-algo", "Det/NonDet, default is NonDet", process_sproute_set_algo},
  { "set-max-iterations", "set max iterations of maze routing, default = 30", process_sproute_set_max_iteration},
  { "run", "run sproute", process_sproute_run },
  { "close", "close sproute", process_sproute_close }
};

#endif
