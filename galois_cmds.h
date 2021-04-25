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
#ifndef __GALOIS_CMDS_H__
#define __GALOIS_CMDS_H__

#include <act/act.h>
#include "config_pkg.h"

void *read_lib_file (const char *file);
const char *timing_graph_init (Act *a, Process *p, int *lib_id, int nlibs);
const char *timer_run (void);
void timer_get_period (double *p, int *M);
const char *timer_create_graph (Act *a, Process *p);

#ifdef FOUND_galois_eda

#include "actpin.h"
#include "galois/eda/asyncsta/AsyncTimingEngine.h"

class timing_info {
 public:
  /* array of arrival times and required times */
  timing_info ();
  ~timing_info();

  void populate (ActPin *p, galois::eda::utility::TransitionMode m);
  
  double *arr;
  double *req;
  ActPin *pin;
  int dir;
};

list_t *timer_query (int vid);

/* returns nothing, or two timing_info * objects: fall followed by rise */
list_t *timer_query_driver (int vid);

#define timer_query_extract_fall(x) ((timing_info *)list_value(list_first(x)))
#define timer_query_extract_rise(x) ((timing_info *)list_value(list_next(list_first(x))))

void timer_query_free (list_t *l);

#endif

#endif /* __GALOIS_CMDS_H__ */
