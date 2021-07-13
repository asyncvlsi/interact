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

  TaggedTG *tg;
  
} TS;

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
  TS.lib = NULL;
  TS.tg = NULL;
}

static void init (int mode = 0)
{
  static galois::SharedMemSys *g = NULL;

  if (mode == 0) {
    if (!g) {
      g = new galois::SharedMemSys;
      TS.anl = NULL;
      TS.engine = NULL;
    }
  }
  else {
    clear_timer ();
    if (g) {
      delete g;
      g = NULL;
    }
  }
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
  
  using CellLib = galois::eda::liberty::CellLib;
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
    ap = new ActPin (vdn_i, vdn_i, pinname);
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

      ap = new ActPin (drv_pin->getNet(), gate_out->getInst(), pinname);

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
	ap = new ActPin (drv_pin->getNet(), gate_out->getInst(), pinname);

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

  engine->checkAndPadParasitics ();

  if (gr_create_error) {
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

timing_info::timing_info ()
{
  pin = NULL;
  MALLOC (arr, double, TS.M);
  MALLOC (req, double, TS.M);
  for (int i=0; i < TS.M; i++) {
    arr[i] = 0;
    req[i] = 0;
  }
  dir = -1;
}

timing_info::~timing_info ()
{
  FREE (arr);
  FREE (req);
}

void timing_info::populate (ActPin *p,
			    galois::eda::utility::TransitionMode mode)
{
  pin = p;

  auto maxmode = galois::eda::utility::AnalysisMode::ANALYSIS_MAX;
  
  for (int i=0; i < TS.M; i++) {
    arr[i] = TS.engine->getPinArrv (p, mode, TS.lib, maxmode, i);
    req[i] = TS.engine->getPinPerfReq (p, mode, TS.lib, maxmode, i);
  }

  if (mode == galois::eda::utility::TransitionMode::TRANS_RISE) {
    dir = 1;
  }
  else {
    dir = 0;
  }
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


/*
 *  Vertex for driver (which is the net)
 *  Returns a list of (arrival time, required time)
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

  ti = new timing_info ();
  ti->populate (p, TransMode::TRANS_FALL);
  list_append (l, ti);

  ti = new timing_info ();
  ti->populate (p, TransMode::TRANS_RISE);
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

    ti = new timing_info ();
    ti->populate (p, TransMode::TRANS_FALL);
    list_append (l, ti);

    ti = new timing_info ();
    ti->populate (p, TransMode::TRANS_RISE);
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

    ti = new timing_info ();
    ti->populate (p, TransMode::TRANS_FALL);
    list_append (l, ti);

    ti = new timing_info ();
    ti->populate (p, TransMode::TRANS_RISE);
    list_append (l, ti);
  }
  

  return l;
}

/*
 *  Vertex for driver (which is the net)
 *  Returns a list of (arrival time, required time)
 */
list_t *timer_query_driver (int vid)
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

  ti = new timing_info ();
  ti->populate (p, TransMode::TRANS_FALL);
  list_append (l, ti);

  ti = new timing_info ();
  ti->populate (p, TransMode::TRANS_RISE);
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
    }
    else {
      from_adj = 0;
    }
    
    to_id = c->to_tick ? to_id+1 : to_id;
    if (to_id == TS.M) {
      to_adj = TS.M*TS.p;
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

    if (root_tm > (from_tm+1e-5) || root_tm > (to_tm+1e-5)) {
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

#endif
