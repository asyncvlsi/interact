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

#ifdef FOUND_galois_eda

#include "actpin.h"
#include "cyclone/AsyncTimingEngine.h"

TaggedTG *timer_get_tagged_tg (void);

using TransMode = galois::eda::utility::TransitionMode;

cyclone::TimingPath timer_get_crit (void);
void timer_display_path (pp_t *pp, cyclone::TimingPath path, int show_delays);

class timing_info {
 public:
  /* array of arrival times and required times */
  timing_info ();
  timing_info (ActPin *p, galois::eda::utility::TransitionMode m);
  timing_info (ActPin *p, int dir);
  ~timing_info();

  
  double *arr;
  double *req;
  double slew;
  ActPin *pin;
  int dir;

  void Print (FILE *fp);

private:
  void _populate (ActPin *p, galois::eda::utility::TransitionMode m);
  void _init ();
};

struct cyclone_constraint {
  int tg_id; // timing graph constraint #
  unsigned int root_dir:1, from_dir:1, to_dir:1; // 0 = -, 1 = +
  unsigned int witness_ready:2; // 0 = not registered, 1 = registered,
				// 2 = computed

  int fast_path_id;
  int slow_path_id;
  int topK;
  
  cyclone::TimingCheck *tc;
};

void *read_lib_file (const char *file);
const char *timing_graph_init (Act *a, Process *p, int *lib_id, int nlibs);
const char *timer_run (void);
void timer_get_period (double *p, int *M);
const char *timer_create_graph (Act *a, Process *p);
const char *timer_get_time_string (void);
double timer_get_time_units (void);
int timer_get_num_cyclone_constraints (void);
void timer_incremental_update (void);

list_t *timer_query (int vid);

/* returns nothing, or two timing_info * objects: fall followed by rise */
list_t *timer_query_driver (int vid);

#define timer_query_extract_fall(x) ((timing_info *)list_value(list_first(x)))
#define timer_query_extract_rise(x) ((timing_info *)list_value(list_next(list_first(x))))

void timer_query_free (list_t *l);
cyclone_constraint *timer_get_cyclone_constraint (int id);
timing_info *timer_query_transition (int vid, int dir);
ActPin *tgraph_vertex_to_pin (int vid);
ActPin *timer_get_dst_pin (AGedge *e);

#ifdef FOUND_phydb
#include <phydb/phydb.h>

void timer_convert_path (cyclone::TimingPath &path,
			 std::vector<phydb::ActEdge> &actp);

void timer_get_fast_end_paths (int constraint,
			       std::vector<phydb::ActEdge> &actp);

void timer_get_slow_end_paths (int constraint,
			       std::vector<phydb::ActEdge> &actp);

void timer_get_perf_paths (int constraint, 
			   std::vector<phydb::ActEdge> &actp);

void timer_link_engine (phydb::PhyDB *);

#endif

void timer_compute_witnesses (void);
void timer_add_check (int constraint);
void timer_set_topK (int k);
void timer_set_topK_id (int id, int k);


#endif

void init_galois_shmemsys(int mode = 0);

#endif /* __GALOIS_CMDS_H__ */
