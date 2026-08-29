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
#include <string.h>
#include "all_cmds.h"
#include "flow.h"

int output_window_width;

/*************************************************************************
 *
 *
 *  Utility functions
 *
 *
 *************************************************************************
 */
static const char *get_state_str (void)
{
  static char buf[1024];
  int sz = 1024;
  int pos = 0;
  int len;
  const char *s = NULL;
  switch (F.s) {
  case STATE_ANY:
    s = "[any]";
    break;
    
  case STATE_EMPTY:
    s = "[no design]";
    break;

  case STATE_DESIGN:
    s = "[unexpanded design]";
    break;

  case STATE_EXPANDED:
    s = "[expanded design]";
    break;

  case STATE_ERROR:
    s = "[error]";
    break;

  case STATE_DIRTY:
    s = "[dirty]";
    break;
    
  }
  snprintf (buf + pos, sz, "%s\n  ", s);
  len = strlen (buf + pos);
  pos += len;
  sz -= len;
  if (F.ckt_gen) {
    snprintf (buf + pos, sz, " ckt:yes");
  }
  else {
    snprintf (buf + pos, sz, " ckt:no");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;

  if (F.cell_map) {
    snprintf (buf + pos, sz, " cells:yes");
  }
  else {
    snprintf (buf + pos, sz, " cells:no");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;

  if (F.act_toplevel) {
    snprintf (buf + pos, sz, " top:set");
  }
  else {
    snprintf (buf + pos, sz, " top:unset");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;
  
  if (F.timer == 0) {
    snprintf (buf + pos, sz, " timer:none");
  }
  else if (F.timer == TIMER_INIT) {
    snprintf (buf + pos, sz, " timer:init");
  }
  else if (F.timer == TIMER_RUN) {
    snprintf (buf + pos, sz, " timer:run");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;

#ifdef FOUND_dali
  if (F.dali) {
    snprintf (buf + pos, sz, " placer:init");
  }
  else {
    snprintf (buf + pos, sz, " placer:none");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;
#endif
#ifdef FOUND_phydb
  if (F.phydb) {
    snprintf (buf + pos, sz, " phydb:init");
  } 
  else {
    snprintf (buf + pos, sz, " phydb:none");
  }
  len = strlen (buf + pos);
  pos += len;
  sz -= len;
#endif
     
  return buf;
}


/*--------------------------------------------------------------------------

  Check that # args match, and that we are in a valid flow state
  for the command.

--------------------------------------------------------------------------*/

int std_argcheck (int argc, char **argv, int argnum, const char *usage,
		  design_state required)
{
  if (argc != argnum) {
    fprintf (stderr, "Usage: %s %s\n", argv[0], usage);
    return 0;
  }
  if (required == STATE_ANY || F.s == required) {
    return 1;
  }

  warning ("%s: command failed; incorrect flow state.\n Current flow state: %s", argv[0], get_state_str ());
  fprintf (stderr, "\n");
  return 0;
}


/*--------------------------------------------------------------------------

  Open/close output file, interpreting "-" as stdout

--------------------------------------------------------------------------*/
FILE *std_open_output (const char *cmd, const char *s)
{
  FILE *fp;
  if (strcmp (s, "-") == 0) {
    fp = stdout;
  }
  else {
    fp = fopen (s, "w");
    if (!fp) {
      return NULL;
    }
  }
  return fp;
}

void std_close_output (FILE *fp)
{
  if (fp != stdout) {
    fclose (fp);
  }
}



void flow_init (void)
{
  F.s = STATE_EMPTY;
  F.cell_map = 0;
  F.ckt_gen = 0;
  F.timer = 0;
  F.sp = NULL;
  F.tp = NULL;

  F.act_design = new Act ();
  F.act_toplevel = NULL;

#ifdef FOUND_dali
  F.dali = NULL;
#endif

#ifdef FOUND_pwroute
  F.pwroute = NULL;
#endif

#ifdef FOUND_sproute
  F.sproute = NULL;
#endif

#ifdef FOUND_phydb
  F.phydb = NULL;
  F.phydb_cell = 0;
  F.phydb_lef = 0;
  F.phydb_def = 0;
#endif  
  
}

static void flow_reset_derived_state (void)
{
#ifdef FOUND_dali
  if (F.dali) {
    F.dali->Close();
    delete F.dali;
    F.dali = NULL;
  }
#endif

  timer_reset_for_reelaboration ();

#ifdef FOUND_pwroute
  if (F.pwroute) {
    delete F.pwroute;
    F.pwroute = NULL;
  }
#endif

#ifdef FOUND_sproute
  if (F.sproute) {
    delete F.sproute;
    F.sproute = NULL;
  }
#endif

#ifdef FOUND_phydb
  if (F.phydb) {
    delete F.phydb;
    F.phydb = NULL;
  }
  F.phydb_lef = 0;
  F.phydb_def = 0;
  F.phydb_cell = 0;
  F.phydb_cluster = 0;
#endif
}

void flow_reset_derived_state_direct (void)
{
  flow_reset_derived_state ();
}

/*
  The ACT half of a delay-site replacement: swap each instance's process,
  invalidate the cached nets, and refresh every pass. Expects the caller to have
  already torn down whatever derived state it intends to rebuild.
*/
static bool flow_reelaborate_delay_sites (
    const std::vector<flow_delay_site_replacement> &replacements)
{

  ActPass *layout_pass = F.act_design->pass_find ("stk2layout");
  if (layout_pass) {
    ActDynamicPass *layout = dynamic_cast<ActDynamicPass *> (layout_pass);
    if (!layout || layout->runcmd ("design_refresh") != 1) {
      fprintf (stderr, "flow_apply_delay_site_map: layout design_refresh failed\n");
      return false;
    }
  }

  for (size_t i = 0; i < replacements.size(); ++i) {
    Process *replacement = F.act_design->findProcess (
        replacements[i].expanded_process_name.c_str(), true);
    if (!replacement || !replacement->isExpanded()) {
      fprintf (stderr, "flow_apply_delay_site_map: process lookup failed for %s\n",
               replacements[i].expanded_process_name.c_str());
      return false;
    }
    std::string instance_name = replacements[i].instance_name;
    if (!F.act_toplevel->updateInst (instance_name.data(), replacement)) {
      fprintf (stderr, "flow_apply_delay_site_map: instance update failed for %s\n",
               instance_name.c_str());
      return false;
    }
    F.s = STATE_DIRTY;
  }

  ActPass *pass = F.act_design->pass_find ("booleanize");
  ActBooleanizePass *booleanize = dynamic_cast<ActBooleanizePass *> (pass);
  if (!booleanize) {
    fprintf (stderr, "flow_apply_delay_site_map: booleanize pass missing\n");
    return false;
  }
  booleanize->invalidateNets ();
  ActPass::refreshAll (F.act_design, F.act_toplevel);
  F.s = STATE_EXPANDED;
  return true;
}

bool flow_apply_delay_site_map (
    const std::vector<flow_delay_site_replacement> &replacements)
{
  if (!F.act_design || !F.act_toplevel || replacements.empty()) {
    fprintf (stderr, "flow_apply_delay_site_map: invalid design/top/replacements\n");
    return false;
  }
  flow_reset_derived_state ();
  return flow_reelaborate_delay_sites (replacements);
}

bool flow_apply_delay_site_map_keep_dali (
    const std::vector<flow_delay_site_replacement> &replacements)
{
  if (!F.act_design || !F.act_toplevel || replacements.empty()) {
    fprintf (stderr,
             "flow_apply_delay_site_map_keep_dali: invalid design/top/replacements\n");
    return false;
  }

  /*
    Everything flow_reset_derived_state drops, except Dali. Dali is holding the
    placement this checkpoint exists to preserve, and it is rebound to the new
    PhyDB by the caller once one exists.
  */
  timer_reset_for_reelaboration ();
#ifdef FOUND_pwroute
  if (F.pwroute) { delete F.pwroute; F.pwroute = NULL; }
#endif
#ifdef FOUND_sproute
  if (F.sproute) { delete F.sproute; F.sproute = NULL; }
#endif
#ifdef FOUND_phydb
  if (F.phydb) { delete F.phydb; F.phydb = NULL; }
  F.phydb_lef = 0;
  F.phydb_def = 0;
  F.phydb_cell = 0;
  F.phydb_cluster = 0;
#endif
  return flow_reelaborate_delay_sites (replacements);
}
