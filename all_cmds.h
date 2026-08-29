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
#include <act/act.h>
#include <act/passes.h>
#include "config_pkg.h"

#ifdef FOUND_dali
#include <dali/timing/timing_driven_placement_controller.h>
#endif

#ifdef FOUND_phydb
namespace phydb { class PhyDB; }
#endif

void act_cmds_init (void);
void synth_cmds_init (void);
void ckt_cmds_init (void);
void timer_cmds_init (void);
void timer_reset_for_reelaboration (void);
#ifdef FOUND_timing_actpin
bool timer_build_graph_direct (void);
bool timer_initialize_liberty_path (const std::string &path);
#if defined(DALI_P2B_TEST_HARNESS) && defined(FOUND_dali)
void timer_test_clobber_library_stack (void);
#endif
#ifdef FOUND_dali
bool timer_run_and_capture (
    dali::TimingDrivenPlacementMeasurement *measurement);
#endif
#ifdef FOUND_phydb
bool timer_link_phydb_direct (phydb::PhyDB *phydb);
#endif
#endif
void pandr_cmds_init (void);
void placement_cmds_init (void);
void routing_cmds_init (void);
void conf_cmds_init (void);
void misc_cmds_init (void);


/* -- functions exported -- */
FILE *sys_get_fileptr (int v);
void act_flatten_prs (Act *a, FILE *fp, Process *p, int mode);
void act_flatten_sim (Act *a, FILE *fps, FILE *fpa, Process *p);
void act_emit_verilog (Act *a, FILE *fp, Process *p);

/* fmt has i for integer, s for string, f for float, * means repeat
   prev to the of arg list */
void save_to_log (int argc, char **argv, const char *fmt);

ActNetlistPass *getNetlistPass (void);

bool flow_rebuild_cell_and_netlist (const std::string &cell_file);

#ifdef FOUND_dali
#ifdef FOUND_phydb
namespace dali { class TopologyCheckpointHost; }
/* The ACT-authoritative topology host consulted at a Dali checkpoint. */
dali::TopologyCheckpointHost *interact_fixed_topology_host (void);
/* Arms it for exactly one delay-site parameter increase. */
bool interact_configure_fixed_topology_host (const std::string &process_template,
                                             const std::string &tech_config,
                                             const std::string &liberty);
#endif
#endif
bool flow_generate_layout_files (const std::string &lef_file,
                                 const std::string &cell_file,
                                 const std::string &def_file,
                                 double die_llx,
                                 double die_lly,
                                 double die_urx,
                                 double die_ury);
#if defined(FOUND_dali) && defined(FOUND_phydb) && defined(FOUND_timing_actpin)
int run_timing_driven_placement_p2b (const char *config_path,
                                     const char *output_path);
#ifdef DALI_P2B_TEST_HARNESS
int run_timing_driven_placement_p2b_rollback_failure_test(
    const char *config_path, const char *output_path);
int run_timing_driven_placement_p2b_begin_failure_test(
    const char *config_path, const char *output_path);
int run_timing_driven_placement_p2b_commit_failure_test(
    const char *config_path, const char *output_path);
#endif
#endif
#ifdef FOUND_phydb
bool flow_build_phydb (const std::string &lef_file,
                       const std::string &cell_file,
                       const std::string &def_file,
                       const std::string &tech_config_file);
#endif

#ifdef FOUND_galois

void init_galois_shmemsys(int mode = 0);
void galois_set_threads (int nthreads);

#endif
