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
#include "config_pkg.h"
#include "flow.h"

#ifdef FOUND_galois_eda

#include "galois/Galois.h"
#include "galois/eda/liberty/CellLib.h"
#include "galois/eda/liberty/Cell.h"
#include "galois/eda/liberty/CellPin.h"
#include "galois/eda/delaycalc/NldmDelayCalculator.h"
#include "galois/eda/parasitics/Manager.h"
#include "galois/eda/parasitics/Net.h"
#include "galois/eda/parasitics/Node.h"
#include "galois/eda/parasitics/Edge.h"

#include "cyclone/AsyncTimingEngine.h"

#include "ptr_manager.h"
#include "galois_cmds.h"
#include "actpin.h"

static struct timing_state {
  ActNetlistAdaptor *anl;	/* netlist adaptor between ACT and
				   timer */

  cyclone::AsyncTimingEngine *engine; /* sta engine */

  galois::eda::liberty::CellLib *lib; /* used for cycle ratio */

  struct pHashtable *edgeMap;	/* maps TimingEdgeInfo to input pins
				   */
  int M;
  double p;

  const char *time_units;
  double time_mult;
  
  const char *lib_units;

  TaggedTG *tg;

  A_DECL (cyclone_constraint, constraints);
  
} TS;

ActPin *tgraph_vertex_to_pin (int vid);


static void clear_timer()
{
  if (F.tp) {
    delete F.tp;
    F.tp = NULL;
  }
  if (TS.anl) {
    delete TS.anl;
    TS.anl = NULL;
  }
  if (TS.engine) {
    delete TS.engine;
    TS.engine = NULL;
  }
  if (TS.edgeMap) {
    phash_free (TS.edgeMap);
    TS.edgeMap = NULL;
  }
  if (TS.tg) {
    A_FREE (TS.constraints);
  }
  TS.lib = NULL;
  TS.time_units = NULL;
  TS.lib_units = NULL;
  TS.time_mult = 1.0;
  TS.tg = NULL;
  A_INIT (TS.constraints);
}

void init_galois_shmemsys(int mode) {
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

static void init (int mode = 0)
{
  static int first = 1;

  if (first) {
      TS.anl = NULL;
      TS.engine = NULL;
  }
  first = 0;
  
  if (mode == 1) {
    clear_timer ();
    first = 1;
  }
  init_galois_shmemsys (mode);
}

/*------------------------------------------------------------------------
 *
 *  Read liberty file, return handle
 *
 *------------------------------------------------------------------------
 */
void *read_lib_file (const char *file)
{
  init();
  
  FILE *fp = fopen (file, "r");
  if (!fp) {
    return NULL;
  }
  fclose (fp);

  galois::eda::liberty::CellLib *lib = new galois::eda::liberty::CellLib;
  lib->parse(file);

  return (void *)lib;
}

static
galois::eda::utility::Float
getPortCap(void* const,                          // external pin pointer
           galois::eda::utility::TransitionMode, // TRANS_FALL or TRANS_RISE
           galois::eda::liberty::CellLib* const, // cell library
           galois::eda::utility::AnalysisMode    // ANALYSIS_MIN or ANALYSIS_MAX
          )
{
  return 0.0;
}


static
AGedge *find_edge (AGvertex *from, AGvertex *to)
{
  AGvertexFwdIter fw(TS.tg, from->vid);
  AGvertexBwdIter bw(TS.tg, to->vid);

  fw = fw.begin();
  bw = bw.begin();

  while ((fw != fw.end()) && (bw != bw.end())) {
    AGedge *e1 = (*fw);
    AGedge *e2 = (*bw);
    if (e1->dst == to->vid) {
      return e1;
    }
    if (e2->src == from->vid) {
      return e2;
    }
    fw++;
    bw++;
  }
  return NULL;
}

void timer_display_path (pp_t *pp, cyclone::TimingPath path, int show_delays)
{
  char buf[1024];

  AGvertex *start_inst;
  AGvertex *cur_inst;
  AGvertex *tmp;
  
  start_inst = NULL; 
  TransMode start_t;
  
  cur_inst = NULL;
  TransMode cur_t;
  
  tmp = NULL;

  double path_time;
  int path_ticks;

  path_time = 0;
  path_ticks = 0;

  int sz = (int)path.size();
  int sz10;

  sz10 = 1;
  while (sz > 10) {
    sz10++;
    sz = sz/10;
  }

  if (show_delays) {
    pp_printf (pp, "%*s %10s  %10s  %10s  %5s %s",
	       sz10+1, " ", 
	       "Path delay", "Incr delay",
	       "Slew time", "Tick?", "Transition");
    pp_forced (pp, 0);
    pp_printf (pp, "%*s %10s  %10s  %10s  %5s %10s",
	       sz10+1, " ",
	       "----------", "----------",
	       "---------", "-----", "----------");
    pp_forced (pp, 0);
  }
  else {
    pp_printf (pp, "%*s %5s %s",
	       sz10+1, " ", "Tick?", "Transition");
    pp_forced (pp, 0);
    pp_printf (pp, "%*s %5s %10s",
	       sz10+1, " ", "-----", "----------");
    pp_forced (pp, 0);
  }

  double adjust = 0;
  int first = 1;

  int count = 0;

  for (auto x : path) {
    ActPin *p = (ActPin *) x.first;
    TransMode t = x.second;
    p->sPrintFullName (buf, 1024);
    double tm;
    timing_info *ti;

    if (show_delays) {
      ti = new timing_info (p, t);
    }
    else {
      ti = NULL;
    }

    if (ti) {
      if (first) {
	adjust = ti->arr[0];
      }
    }

    Assert (path_ticks < TS.M, "What?");

    if (ti) {
      tm = ti->arr[path_ticks] - adjust - path_time;

      if (tm < 0) {
	tm = tm + TS.M*TS.p;
      }
    
      path_time = ti->arr[path_ticks] - adjust;

      if (fabs (path_time) < 1.0e-6 && !first) {
	path_time = TS.p*TS.M;
	first = 2;
      }
    }

    tmp = p->getInstVertex();

    int tflag = 0;
    if (!start_inst) {
      start_inst = tmp;
      cur_inst = tmp;
    }

    if (tmp == start_inst) {
      start_t = t;
    }
    else {
      if (cur_inst == start_inst) {
	cur_inst = tmp;
	cur_t = t;
      }
      else if (tmp == cur_inst) {
	cur_t = t;
      }
      else {
	AGvertex *old;
	/* here's the edge! */
	if (start_t == TransMode::TRANS_RISE) {
	  Assert ((start_inst->vid & 1) == 0, "Hmm");
	  start_inst = TS.tg->getVertex (start_inst->vid | 1);
	}
	old = cur_inst;
	if (cur_t == TransMode::TRANS_RISE) {
	  Assert ((cur_inst->vid & 1) == 0, "Hmm");
	  cur_inst = TS.tg->getVertex (cur_inst->vid | 1);
	}
	AGedge *e = find_edge (start_inst, cur_inst);
#if 0	
	pp_printf (pp, "[e%s]", e ? "*" : "");
	printf ("edge from: ");
#endif	
	TimingVertexInfo *si, *di;
	TimingEdgeInfo *ei;

	ei = (TimingEdgeInfo *)e->getInfo();

	if (ei->isTicked()) {
	  path_ticks++;
	  tflag = 1;
	}
	si = (TimingVertexInfo *)start_inst->getInfo();
	di = (TimingVertexInfo *)cur_inst->getInfo();
#if 0	
	printf ("%s ->[%d] %s\n", si->info(),
		ei->isTicked() ? 1 : 0,
		di->info());
#endif
	start_inst = old;
	start_t = cur_t;

	cur_inst = tmp;
	cur_t = t;
      }
    }

    if (show_delays) {
      pp_printf (pp, "#%*d %10.6g  %10.6g  %10.6g    %c   ",
		 sz10, ++count, path_time, tm, ti->slew, tflag ? 'Y' : '_');
      pp_printf (pp, "%s%c", buf, (t == TransMode::TRANS_FALL ? '-' : '+'));
      pp_forced (pp, 0);
    }
    else {
      pp_printf (pp, "#%*d   %c   ",
		 sz10, ++count, tflag ? 'Y' : '_');
      pp_printf (pp, "%s%c", buf, (t == TransMode::TRANS_FALL ? '-' : '+'));
      pp_forced (pp, 0);
    }

    if (ti) {
      delete ti;
    }

    if (first == 2) {
      path_time = 0;
      first = 0;
    }

    if (first) {
      first = 0;
    }
  }
  pp_forced (pp, 0);
}

/*------------------------------------------------------------------------
 *
 * Push timing graph to Galois timer (return NULL on success, error
 * message otherwise)
 *
 *------------------------------------------------------------------------
 */
static cyclone::AsyncTimingEngine *
timer_engine_init (ActPass *tg, Process *p, int nlibs,
		   galois::eda::liberty::CellLib **libs,
		   ActNetlistAdaptor **ret_anl)
							     
{
  cyclone::AsyncTimingEngine *engine = NULL;

  /* -- make sure we've created the timing graph in the ACT library -- */
  if (!tg->completed()) {
    tg->run (p);
  }

  /* -- create the pin translator -- */
  *ret_anl = new ActNetlistAdaptor (F.act_design, p);

  /* -- create timer -- */
  engine = new cyclone::AsyncTimingEngine(*ret_anl);

  auto maxmode = galois::eda::utility::AnalysisMode::ANALYSIS_MAX;

  /* -- add cell library -- */
  for (int i=0; i < nlibs; i++) {
    if (i == nlibs-1) {
      engine->addCellLib (libs[i], maxmode, true);
    }
    else {
      engine->addCellLib (libs[i], maxmode);
    }
  }
  engine->setEpsilon (1e-6); // slew convergence tolerance

  auto nldm = new galois::eda::delaycalc::NldmDelayCalculator (engine);
  nldm->setFuncPtr4GetPortCap (getPortCap);
  engine->setDelayCalculator (nldm);
  
  TaggedTG *gr = (TaggedTG *) tg->getMap (p);
  TS.tg = gr;
  
  ActBooleanizePass *bp =
    dynamic_cast<ActBooleanizePass *> (tg->getPass ("booleanize"));

  int gr_create_error = 0;

  /* Step 1: create all the output pins */
  for (int i=0; i < gr->numVertices(); i += 2) {
    AGvertex *vdn = gr->getVertex (i);
    AGvertex *vup = gr->getVertex (i+1);

    TimingVertexInfo *vup_i, *vdn_i;

    vup_i = (TimingVertexInfo *) vup->getInfo();
    vdn_i = (TimingVertexInfo *) vdn->getInfo();
    
    if (!vup_i || !vdn_i) continue;
    if (!vup_i->base()->getExpr() || !vdn_i->base()->getExpr()) continue;

    Assert (vup_i->getFirstConn() == vdn_i->getFirstConn(), "What?!");

    act_connection *netinfo =  vup_i->getFirstConn();
    Process *cellname = vup_i->getCell();
    Assert (cellname, "What?");

    act_boolean_netlist_t *bnl = bp->getBNL (cellname);
    Assert (bnl, "What?");

    /* -- create timing vertex for cell output -- */
    ActPin *ap;
    act_connection *pinname;

    pinname = NULL;
    for (int i=0; i < A_LEN (bnl->ports); i++) {
      if (bnl->ports[i].omit) continue;
      if (bnl->ports[i].input) continue;
      pinname = bnl->ports[i].c;
      break;
    }
    if (!pinname) {
      fatal_error ("Gate %s has no output pin?", cellname->getName());
    }
    ap = new ActPin (vdn, vdn, pinname);
    /* driver is the same as the cell with the pin */
    
    vup_i->setSpace (ap);
    vdn_i->setSpace (ap);

    engine->addDriverPin (ap);
#if 0
    printf ("add driver pin: ");
    actpin_print (ap);
    printf ("\n");
#endif    
  }

  A_DECL (ActPin *, cur_gate_pins);
  A_INIT (cur_gate_pins);

  TS.edgeMap = phash_new (32);
    
  for (int i=0; i < gr->numVertices(); i += 2) {
    AGvertex *vdn = gr->getVertex (i);
    AGvertex *vup = gr->getVertex (i+1);

    TimingVertexInfo *vup_i, *vdn_i;
    vup_i = (TimingVertexInfo *) vup->getInfo();
    vdn_i = (TimingVertexInfo *) vdn->getInfo();

    if (!vup_i || !vdn_i) continue;
    if (!vup_i->base()->getExpr() || !vdn_i->base()->getExpr()) continue;

    ActPin *gate_out = (ActPin *)vup_i->getSpace();
    Assert (gate_out, "What?");

    /* -- gate_out is the output pin for the timing vertex -- */

    AGvertexBwdIter bw(gr, i);
    for (bw = bw.begin(); bw != bw.end(); bw++) {
      AGedge *be = (*bw);
      AGvertex *src = gr->getVertex (be->src);
      ActPin *drv_pin;
      TimingVertexInfo *src_v = (TimingVertexInfo *) src->getInfo();
      if (!src_v->getSpace()) continue;

      drv_pin = (ActPin *) src_v->getSpace();

      /* drv_pin is the driving pin that drives one of the gate inputs */

      TimingEdgeInfo *ei = (TimingEdgeInfo *)be->getInfo();
      if (ei->getIPin() < 0) continue;

      ActPin *ap;

      /* find the pin name */
      act_connection *pinname;
      int pinid = ei->getIPin();
      act_boolean_netlist_t *bnl = gate_out->cellBNL(bp);
      Assert (bnl, "What?");

      pinname = NULL;
      for (int i=0; i < A_LEN (bnl->ports); i++) {
	if (bnl->ports[i].omit) continue;
	if (pinid == 0) {
	  pinname = bnl->ports[i].c;
	  break;
	}
	pinid--;
      }
      Assert (pinname, "Can't find pin?");

      /* create a new pin for this net, hooked up to pin "pinname" of
	 the "gate_out" vertex */

      ap = new ActPin (drv_pin->getNetVertex(),
		       gate_out->getInstVertex(), pinname);

      phash_bucket_t *act_pin = phash_add (TS.edgeMap, ei);
      act_pin->v = ap;
      //printf ("add pin to %p (src=%d, dst=%d)\n", ei, be->src, be->dst);
		       
      engine->addLoadPin (ap);
      //printf ("add load pin: "); actpin_print (ap);
      //printf ("\n");

      A_NEW (cur_gate_pins, ActPin *);
      A_NEXT (cur_gate_pins) = ap;
      A_INC (cur_gate_pins);
      
      /* -- interconnect arcs -- */
      engine->addNetLeg (drv_pin, ap, TransMode::TRANS_RISE);
      engine->addNetLeg (drv_pin, ap, TransMode::TRANS_FALL);

#if 0      
      printf ("add net leg: "); actpin_print (drv_pin);
      printf (" -> "); actpin_print (ap);
      printf ("\n");
#endif      
	
      /* -- internal arc from this signal to gate output for v_dn -- */
      if (!engine->addDelayArc
	  (ap, TransMode::TRANS_RISE, gate_out, TransMode::TRANS_FALL)) {
	if (!ei->isGuess()) {
	  char buf[100];
	  fprintf (stderr, "FAILED to add delay arc\n\t");
	  ap->Print (stderr);
	  fprintf (stderr, " (r) -> ");
	  gate_out->Print (stderr);
	  gate_out->sPrintCellType (buf, 100);
	  fprintf (stderr, " (f) [cell: %s]\n", buf);
	  gr_create_error++;
	}
      }
#if 0
      printf ("add delay arc (%d -> %d): ", be->src, be->dst); ap->Print (stdout);
      printf (" (r) -> "); gate_out->Print (stdout);
      printf (" (f)\n");
#endif      
      if (ei->isTicked()) {
#if 0
	printf (" ** tick (%d -> %d)\n", be->src, be->dst);
#endif	
	engine->setDelayEdgeTick
	  (ap, TransMode::TRANS_RISE, gate_out, TransMode::TRANS_FALL, true);
      }
    }

    /* upgoing transition */
    AGvertexBwdIter bw2(gr, i+1);
    for (bw2 = bw2.begin(); bw2 != bw2.end(); bw2++) {
      AGedge *be = (*bw2);
      AGvertex *src = gr->getVertex (be->src);
      ActPin *drv_pin;
      TimingVertexInfo *src_v = (TimingVertexInfo *) src->getInfo();
      ActPin *ap;
      
      if (!src_v->getSpace()) continue;

      drv_pin = (ActPin *) src_v->getSpace();

      TimingEdgeInfo *ei = (TimingEdgeInfo *)be->getInfo();
      if (ei->getIPin() < 0) continue;

      /* -- see if we have this pin already -- */
      ap = NULL;
      for (int pc=0; pc < A_LEN (cur_gate_pins); pc++) {
	if (*(cur_gate_pins[pc]) == (*drv_pin)) {
	  ap = cur_gate_pins[pc];
	  break;
	}
      }

      if (!ap) {
	/* new pin */
	int pinid = ei->getIPin();
	act_boolean_netlist_t *bnl = ap->cellBNL (bp);
	act_connection *pinname;
	Assert (bnl, "What?");

	pinname = NULL;
	for (int i=0; i < A_LEN (bnl->ports); i++) {
	  if (bnl->ports[i].omit) continue;
	  if (pinid == 0) {
	    pinname = bnl->ports[i].c;
	    break;
	  }
	  pinid--;
	}
	Assert (pinname, "Can't find pin?");
	ap = new ActPin (drv_pin->getNetVertex(),
			 gate_out->getInstVertex(),
			 pinname);

	phash_bucket_t *act_pin = phash_add (TS.edgeMap, ei);
	act_pin->v = ap;
	//printf ("add pin to %p (src=%d, dst=%d)\n", ei, be->src, be->dst);

	engine->addLoadPin (ap);
#if 0	
	printf ("add load pin: "); actpin_print (ap);
	printf ("\n");
#endif	

	A_NEW (cur_gate_pins, ActPin *);
	A_NEXT (cur_gate_pins) = ap;
	A_INC (cur_gate_pins);
      
	/* -- interconnect arcs -- */
	engine->addNetLeg (drv_pin, ap, TransMode::TRANS_RISE);
	engine->addNetLeg (drv_pin, ap, TransMode::TRANS_FALL);
#if 0
	printf ("add net leg: "); actpin_print (drv_pin);
	printf (" -> "); actpin_print (ap);
	printf ("\n");
#endif	
      }

      /* -- internal arc from this signal to gate output for v_up -- */
      if (!engine->addDelayArc
	  (ap, TransMode::TRANS_FALL, gate_out, TransMode::TRANS_RISE)) {
	if (!ei->isGuess()) {
	  char buf[100];
	  fprintf (stderr, "FAILED to add delay arc\n\t");
	  ap->Print (stderr);
	  fprintf (stderr, " (f) -> ");
	  gate_out->Print (stderr);
	  gate_out->sPrintCellType (buf, 100);
	  fprintf (stderr, " (r) [cell: %s]\n", buf);
	  gr_create_error++;
	}
      }
#if 0
      printf ("add delay arc (%d -> %d): ", be->src, be->dst); ap->Print (stdout);
      printf (" (f) -> "); gate_out->Print (stdout);
      printf (" (r)\n");
#endif      
      if (ei->isTicked()) {
#if 0
	printf (" ** tick (%d -> %d)\n", be->src, be->dst);
#endif	
	engine->setDelayEdgeTick (ap, TransMode::TRANS_FALL, gate_out, TransMode::TRANS_RISE, true);
      }
    }
    A_LEN (cur_gate_pins) = 0;
  }
  A_FREE (cur_gate_pins);

  /* -- check for cycle of unticked edges -- */

  auto unticked = engine->findUntickedDelayEdgeCycles ();
  if (!unticked.empty()) {
    pp_t *pp = pp_init (stdout, output_window_width);
    fprintf (stderr, "ERROR: timing graph construction error; the following cycle has zero ticks!\n");
    fprintf (stderr, "Total unticked cycles: %lu\n", (unsigned long) unticked.size());
    for (size_t i = 0; i < unticked.size() && i < 10; i++) {
      pp_printf (pp, "Unticked cycle #%d:", i);
      pp_forced (pp, 0);
      timer_display_path (pp, unticked[i], 0);
    }
    if (engine) {
      delete engine;
    }
    return NULL;
  }

  /* -- add all timing forks -- */

  double delay_units;
  double timer_units;

  A_INIT (TS.constraints);

  if (config_exists ("net.delay")) {
    delay_units = config_get_real ("net.delay");
  }
  else {
    /* rule of thumb: FO4 is roughly F/2 ps where F is in nm units */
    delay_units = config_get_real ("net.lambda")*1e-3;
  }

  timer_units = TS.lib[0].timeUnit;
  delay_units = delay_units/timer_units;

  /* + = 1, - = 2 */
  using TransMode = galois::eda::utility::TransitionMode;
  TransMode tmap[3];
  tmap[0] = TransMode::TRANS_RISE;
  tmap[1] = TransMode::TRANS_RISE;
  tmap[2] = TransMode::TRANS_FALL;

  for (int i=0; i < TS.tg->numConstraints(); i++) {
    /* add this fork! */
    TransMode from_dirs[2], to_dirs[2], root_dir[2];
    int nroot, nfrom, nto;
    TaggedTG::constraint *c = TS.tg->getConstraint (i);

    if (c->iso) {
      /* -- isochronic forks checked differently -- */
      continue;
    }

    if (c->root_dir == 0) {
      nroot = 2;
      root_dir[0] = tmap[1];
      root_dir[1] = tmap[2];
    }
    else {
      nroot = 1;
      root_dir[0] = tmap[c->root_dir];
    }
    if (c->from_dir == 0) {
      nfrom = 2;
      from_dirs[0] = tmap[1];
      from_dirs[1] = tmap[2];
    }
    else {
      nfrom = 1;
      from_dirs[0] = tmap[c->from_dir];
    }
    if (c->to_dir == 0) {
      nto = 2;
      to_dirs[0] = tmap[1];
      to_dirs[1] = tmap[2];
    }
    else {
      nto = 1;
      to_dirs[0] = tmap[c->to_dir];
    }
    ActPin *rpin, *apin, *bpin;
    rpin = tgraph_vertex_to_pin (c->root);
    apin = tgraph_vertex_to_pin (c->from);
    bpin = tgraph_vertex_to_pin (c->to);

    if (!rpin || !apin || !bpin) {
      warning ("Unexpected error in constraint #%d; skipped.\n", i);
      continue;
    }
    
    for (int n=0; n < nroot; n++) {
      for (int l=0; l < nfrom; l++) {
	for (int m=0; m < nto; m++) {
	  /* a unique fork */

	  A_NEW (TS.constraints, cyclone_constraint);
	  A_NEXT (TS.constraints).tg_id = i;
	  A_NEXT (TS.constraints).root_dir =
	    (root_dir[n] == TransMode::TRANS_FALL ? 0 : 1);
	  A_NEXT (TS.constraints).from_dir =
	    (from_dirs[l] == TransMode::TRANS_FALL ? 0 : 1);
	  A_NEXT (TS.constraints).to_dir =
	    (to_dirs[m] == TransMode::TRANS_FALL ? 0 : 1);
	  A_NEXT (TS.constraints).witness_ready = 0;
	  A_INC (TS.constraints);

	  engine->addTimingFork (rpin, root_dir[n],
				 apin, from_dirs[l],
				 c->from_tick ? true : false,
				 bpin, to_dirs[m],
				 c->to_tick ? true : false);
	  if (c->margin != 0) {
	    engine->setTimingForkMargin (rpin, root_dir[n],
					 apin, from_dirs[l],
					 c->from_tick ? true : false,
					 bpin, to_dirs[m],
					 c->to_tick ? true : false,
					 c->margin*delay_units);
	  }
	}
      }
    }
  }

  engine->checkAndPadParasitics ();

  if (gr_create_error) {
    if (engine) {
      delete engine;
    }
    return NULL;
  }
  return engine;
}

const char *timer_create_graph (Act *a, Process *p)
{
  ActPass *ap = a->pass_find ("taggedTG");

  if (!ap) {
    return "no timing graph found";
  }

  if (!F.tp) {
    F.tp = dynamic_cast<ActDynamicPass *> (ap);
  }
  Assert (F.tp, "Hmm");

  /* -- create timing graph -- */
  if (!F.tp->completed()) {
    F.tp->run (p);
  }

  return NULL;
}

static int is_roughly_equal (double a, double b)
{
  if (fabs ((a-b)/b) < 1e-6) {
    return 1;
  }
  else {
    return 0;
  }
}

/*------------------------------------------------------------------------
 *
 * Push timing graph to Galois timer (return NULL on success, error
 * message otherwise)
 *
 *------------------------------------------------------------------------
 */
const char *timing_graph_init (Act *a, Process *p, int *libids, int nlibs)
{
  init ();
  
  ActPass *ap = a->pass_find ("taggedTG");
  if (!ap) {
    return "no timing graph found";
  }

  if (!F.tp) {
    F.tp = dynamic_cast<ActDynamicPass *> (ap);
  }
  Assert (F.tp, "What?");

  /* -- add cell library -- */
  galois::eda::liberty::CellLib **libs;

  if (nlibs == 0) {
    return "no libraries?";
  }
  
  MALLOC (libs, galois::eda::liberty::CellLib *, nlibs);
  
  for (int i=0; i < nlibs; i++) { 
    libs[i] = (galois::eda::liberty::CellLib *) ptr_get ("liberty", libids[i]);
    if (!libs[i]) {
      return "timing lib file found";
    }
    if (i == 0) {
      TS.lib = libs[i];
    }
  }

  if (is_roughly_equal (libs[0]->timeUnit,1e-12)) {
    TS.lib_units = "1ps";
    TS.time_units = "ps";
    TS.time_mult = 1.0;
  }
  else if (is_roughly_equal (libs[0]->timeUnit, 1e-11)) {
    TS.lib_units = "10ps";
    TS.time_units = "ps";
    TS.time_mult = 10.0;
  }
  else if (is_roughly_equal (libs[0]->timeUnit, 1e-10)) {
    TS.lib_units = "100ps";
    TS.time_mult = 0.10;
  }
  else if (is_roughly_equal (libs[0]->timeUnit, 1e-9)) {
    TS.lib_units = "1ns";
    TS.time_units = "ns";
    TS.time_mult = 1.0;
  }
  else {
    TS.lib_units = "???";
    TS.time_units = "ns";
    TS.time_mult = libs[0]->timeUnit/1e-9;
  }

  /* -- initialize engine -- */
  TS.engine = timer_engine_init (ap, p, nlibs, libs, &TS.anl);
  FREE (libs);
  if (!TS.engine) {
    clear_timer ();
    return "timing graph construction failed; arcs missing from .lib";
  }

  return NULL;
}

#include <lispCli.h>

static void timer_validate_constraints (void);

const char *timer_run (void)
{
  init ();

  if (!TS.engine) {
    return "timer not initialized";
  }
  
  TS.engine->computeCriticalCycle(TS.lib);
  auto stats = TS.engine->getCriticalCycleRatioAndTicks();

  // set core metrics
  TS.p = stats.first;
  TS.M = stats.second;

  // timing propagation
  TS.engine->computeTiming4Pins();

  // check root from/to ordering in constraints
  timer_validate_constraints ();

  LispSetReturnListStart ();

  LispAppendReturnFloat (TS.p);
  LispAppendReturnInt (TS.M);
  
  LispSetReturnListEnd ();

  return NULL;
}

void timing_info::_init ()
{
  pin = NULL;
  MALLOC (arr, double, TS.M);
  MALLOC (req, double, TS.M);
  for (int i=0; i < TS.M; i++) {
    arr[i] = 0;
    req[i] = 0;
  }
  slew = 0.0;
  dir = -1;
}

timing_info::timing_info ()
{
  _init();
}

timing_info::~timing_info ()
{
  FREE (arr);
  FREE (req);
}

void timing_info::_populate (ActPin *p,
			     galois::eda::utility::TransitionMode mode)
{
  pin = p;

  auto maxmode = galois::eda::utility::AnalysisMode::ANALYSIS_MAX;
  
  for (int i=0; i < TS.M; i++) {
    arr[i] = TS.engine->getPinArrv (p, mode, TS.lib, maxmode, i);
    req[i] = TS.engine->getPinPerfReq (p, mode, TS.lib, maxmode, i);
  }
  slew = TS.engine->getPinSlew (p, mode, TS.lib, maxmode);

  if (mode == galois::eda::utility::TransitionMode::TRANS_RISE) {
    dir = 1;
  }
  else {
    dir = 0;
  }
}

timing_info::timing_info (ActPin *p,
			  galois::eda::utility::TransitionMode mode)
{
  _init ();
  _populate (p, mode);
}

timing_info::timing_info (ActPin *p, int dir)
{
  _init ();
  _populate (p, dir ? galois::eda::utility::TransitionMode::TRANS_RISE :
	     galois::eda::utility::TransitionMode::TRANS_FALL);
}


void timer_get_period (double *p, int *M)
{
  if (!TS.engine) {
    return;
  }
  if (p) {
    *p = TS.p;
  }
  if (M) {
    *M = TS.M;
  }
}

timing_info *timer_query_transition (int vid, int dir)
{
  AGvertex *v;
  ActPin *p;
  TimingVertexInfo *vi;

  v = TS.tg->getVertex (vid & ~1);
  if (!v) {
    return NULL;
  }

  vi = (TimingVertexInfo *) v->getInfo();
  if (!vi) {
    return NULL;
  }

  p = (ActPin *) vi->getSpace();
  if (!p) {
    return NULL;
  }

  return new timing_info (p, dir ?
			  TransMode::TRANS_RISE :
			  TransMode::TRANS_FALL);
}

/*
 *  Vertex for driver (which is the net)
 *  Returns a list of (arrival time, required time)
 *
 *  This is a net query, so it returns information for the driving pin 
 *  as well as driven pins.
 *
 */
list_t *timer_query (int vid)
{
  list_t *l;
  AGvertex *v = TS.tg->getVertex (vid);
  TimingVertexInfo *di = (TimingVertexInfo *) v->getInfo();
  timing_info *ti;
  if (!di) {
    /* no driver, external signal, no info */
    return NULL;
  }

  ActPin *p = (ActPin *) di->getSpace ();
  if (!p) {
    /* ok this is weird */
    return NULL;
  }

  l = list_new ();

  using TransMode = galois::eda::utility::TransitionMode;

  ti = new timing_info (p, TransMode::TRANS_FALL);
  list_append (l, ti);

  ti = new timing_info (p, TransMode::TRANS_RISE);
  list_append (l, ti);

  int vid2;
  if (vid & 1) {
    vid2 = vid-1;
  }
  else {
    vid2 = vid+1;
  }

  AGvertexFwdIter fw(TS.tg, vid);
  for (fw = fw.begin(); fw != fw.end(); fw++) {
    AGedge *e = (*fw);
    phash_bucket_t *b;
    TimingEdgeInfo *ei = (TimingEdgeInfo *)e->getInfo();

    b = phash_lookup (TS.edgeMap, ei);
    
    if (!b) {
      continue;
    }

    p = (ActPin *)b->v;

    ti = new timing_info (p, TransMode::TRANS_FALL);
    list_append (l, ti);

    ti = new timing_info (p, TransMode::TRANS_RISE);
    list_append (l, ti);
  }

  AGvertexFwdIter fw2(TS.tg, vid2);
  for (fw2 = fw2.begin(); fw2 != fw2.end(); fw2++) {
    AGedge *e = (*fw2);
    phash_bucket_t *b;
    TimingEdgeInfo *ei = (TimingEdgeInfo *)e->getInfo();

    b = phash_lookup (TS.edgeMap, ei);
    
    if (!b) {
      continue;
    }
      
    p = (ActPin *)b->v;

    ti = new timing_info (p, TransMode::TRANS_FALL);
    list_append (l, ti);

    ti = new timing_info (p, TransMode::TRANS_RISE);
    list_append (l, ti);
  }

  return l;
}

ActPin *tgraph_vertex_to_pin (int vid)
{
  AGvertex *v = TS.tg->getVertex (vid);
  if (!v) {
    return NULL;
  }
  TimingVertexInfo *di = (TimingVertexInfo *) v->getInfo();
  if (!di) {
    /* no driver, external signal, no info */
    return NULL;
  }

  ActPin *p = (ActPin *) di->getSpace ();
  if (!p) {
    /* ok this is weird */
    return NULL;
  }
  return p;
}

/*
 *  Vertex for driver (which is the net)
 *  Returns a list of (arrival time, required time)
 */
list_t *timer_query_driver (int vid)
{
  list_t *l;
  timing_info *ti;
  ActPin *p = tgraph_vertex_to_pin (vid);
  if (!p) {
    return NULL;
  }

  l = list_new ();

  using TransMode = galois::eda::utility::TransitionMode;

  ti = new timing_info (p, TransMode::TRANS_FALL);
  list_append (l, ti);

  ti = new timing_info (p, TransMode::TRANS_RISE);
  list_append (l, ti);

  return l;
}

void timer_query_free (list_t *l)
{
  if (!l) {
    return;
  }
  listitem_t *li;
  for (li = list_first (l); li; li = list_next (li)) {
    timing_info *ti = (timing_info *) list_value (li);
    delete ti;
  }
  list_free (l);
}

static void check_one_constraint (TaggedTG::constraint *c)
{
  if (!c) return;
  if (!TS.engine) {
    return;
  }

  list_t *l[3];
  l[0] = timer_query_driver (c->root);
  l[1] = timer_query_driver (c->from);
  l[2] = timer_query_driver (c->to);
  if (!l[0] || !l[1] || !l[2]) {
    timer_query_free (l[0]);
    timer_query_free (l[1]);
    timer_query_free (l[2]);
    return;
  }

  timing_info *ti[3][2];
  for (int i=0; i < 3; i++) {
    ti[i][0] = timer_query_extract_fall (l[i]);
    ti[i][1] = timer_query_extract_rise (l[i]);
  }

  for (int i=0; i < TS.M; i++) {
    int from_id, to_id;
    double from_adj, to_adj;
    double root_tm, from_tm, to_tm;

    from_id = i;
    to_id = i;

    from_id = c->from_tick ? from_id+1 : from_id;
    if (from_id == TS.M) {
      from_adj = TS.M*TS.p;
      from_id = 0;
    }
    else {
      from_adj = 0;
    }
    
    to_id = c->to_tick ? to_id+1 : to_id;
    if (to_id == TS.M) {
      to_adj = TS.M*TS.p;
      to_id = 0;
    }
    else {
      to_adj = 0;
    }
#ifndef MAX
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#endif

#define EXTRACT_TIME(id,time,which,idx)					\
    do {								\
      if (c->id == 0) {							\
	/* both */							\
	time = MAX(ti[which][0]->arr[idx], ti[which][1]->arr[idx]);	\
      }									\
      else if (c->id == 1) {						\
	/* rise */							\
	time = ti[which][1]->arr[idx];					\
      }									\
      else {								\
	/* fall */							\
	time = ti[which][0]->arr[idx];					\
      }									\
    } while (0)

    EXTRACT_TIME(root_dir, root_tm, 0, i);
    EXTRACT_TIME(from_dir, from_tm, 1, from_id);
    EXTRACT_TIME(to_dir, to_tm, 2, to_id);
    from_tm += from_adj;
    to_tm += to_adj;

    if (root_tm > (from_tm*(1+1e-5)) || root_tm > (to_tm*(1+1e-5))) {
      c->error = 1;
      break;
    }
  }
  timer_query_free (l[0]);
  timer_query_free (l[1]);
  timer_query_free (l[2]);
}

static void timer_validate_constraints (void)
{
  /* -- check that the root occurs before src -> dst! -- */
  TS.tg->chkSortConstraints ();
  
  for (int i=0; i < TS.tg->numConstraints(); i++) {
    check_one_constraint (TS.tg->getConstraint (i));
  }
}


cyclone::TimingPath timer_get_crit (void)
{
  return TS.engine->getCriticalCycle ();
}


const char *timer_get_time_string (void)
{
  return TS.lib_units;
}

double timer_get_time_units (void)
{
  return TS.lib[0].timeUnit;
}

TaggedTG *timer_get_tagged_tg (void)
{
  return TS.tg;
}

int timer_get_num_cyclone_constraints (void)
{
  return A_LEN (TS.constraints);
}

cyclone_constraint *timer_get_cyclone_constraint (int id)
{
  if (id < 0 || id >= A_LEN (TS.constraints)) {
    return NULL;
  }
  return &TS.constraints[id];
}

#if 0
cyclone::TimingPath timer_get_fastpaths (int constraint)
{
  auto fastPaths =
    TS.engine->getCrctCriticalPaths(
		  p3A, TransMode::TRANS_FALL,
  		  p2ZN, TransMode::TRANS_RISE, false,
		  p2ZN, TransMode::TRANS_FALL, false,
		  libs[0], maxMode, 0, true);
  std::cout << "Critical paths on the fast end:\n";
  for (size_t i = 0; i < fastPaths.size(); i++) {
    std::cout << "Fast path #" << i << "\n";
    printTimingPathWithSlack(vAdaptor, fastPaths[i], "  ");
  }
  std::cout << "\n";
}

cyclone::TimingPath timer_get_slowpaths (int constraint)
{
  auto slowPaths =
      engine->getCrctCriticalPaths(
		  p3A, TransMode::TRANS_FALL,
  		  p2ZN, TransMode::TRANS_RISE, false,
		  p2ZN, TransMode::TRANS_FALL, false,
		  libs[0], maxMode, 0, false);
  std::cout << "Critical paths on the slow end:\n";
  for (size_t i = 0; i < slowPaths.size(); i++) {
    std::cout << "Slow path #" << i << "\n";
    printTimingPathWithSlack(vAdaptor, slowPaths[i], "  ");
  }
  std::cout << "\n";
}
#endif


ActPin *timer_get_dst_pin (AGedge *e)
{
  phash_bucket_t *b;
  b = phash_lookup (TS.edgeMap, e->getInfo());
  if (!b) {
    return NULL;
  }
  else {
    return (ActPin *)b->v;
  }
}


#endif
