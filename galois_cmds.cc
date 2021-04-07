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

#ifdef FOUND_galois_eda

#include "galois/Galois.h"
#include "galois/eda/liberty/CellLib.h"
#include "galois/eda/liberty/Cell.h"
#include "galois/eda/liberty/CellPin.h"
#include "galois/eda/asyncsta/AsyncTimingEngine.h"

#include "ptr_manager.h"
#include "galois_cmds.h"
#include "actpin.h"

static struct timing_state {
  ActDynamicPass *dp;		/* timing graph pass */
  
  ActPinTranslator *apt;	/* pin translator between ACT and
				   timer */

  galois::eda::asyncsta::AsyncTimingEngine *engine; /* sta engine */

  galois::eda::liberty::CellLib *lib; /* used for cycle ratio */
  
} TS;

static void clear_timer()
{
  if (TS.dp) {
    delete TS.dp;
    TS.dp = NULL;
  }
  if (TS.apt) {
    delete TS.apt;
    TS.apt = NULL;
  }
  if (TS.engine) {
    delete TS.engine;
    TS.engine = NULL;
  }
  TS.lib = NULL;
}

static void init (int mode = 0)
{
  static galois::SharedMemSys *g = NULL;

  if (mode == 0) {
    if (!g) {
      g = new galois::SharedMemSys;
      TS.dp = NULL;
      TS.apt = NULL;
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

  if (TS.dp) {
    return "timing graph already initialized";
  }

  TS.dp = dynamic_cast<ActDynamicPass *> (ap);
  Assert (TS.dp, "What?");

  /* -- make sure we've created the timing graph in the ACT library -- */
  TS.dp->run (p);
  
  TS.apt = new ActPinTranslator ();
  TS.engine = new galois::eda::asyncsta::AsyncTimingEngine(TS.apt);

  auto maxmode = galois::eda::utility::AnalysisMode::ANALYSIS_MAX;

  /* -- add cell library -- */
  for (int i=0; i < nlibs; i++) { 
    galois::eda::liberty::CellLib *lib;
    lib = (galois::eda::liberty::CellLib *) ptr_get ("liberty", libids[i]);
    if (!lib) {
      return "timing lib file found";
    }
    TS.engine->addCellLib (lib, maxmode, true);
    if (i == 0) {
      TS.lib = lib;
    }
  }
  
  TaggedTG *gr = (TaggedTG *) TS.dp->getMap (p);
  
  ActBooleanizePass *bp =
    dynamic_cast<ActBooleanizePass *> (a->pass_find ("booleanize"));

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

    TS.engine->addDriverPin (ap);
#if 0
    printf ("add driver pin: ");
    actpin_print (ap);
    printf ("\n");
#endif    
  }

  using TransMode = galois::eda::utility::TransitionMode;

  A_DECL (ActPin *, cur_gate_pins);
  A_INIT (cur_gate_pins);
    
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

      ap = new ActPin (drv_pin->getNet(), gate_out->getCell(), pinname);
		       
      TS.engine->addLoadPin (ap);
      //printf ("add load pin: "); actpin_print (ap);
      //printf ("\n");

      A_NEW (cur_gate_pins, ActPin *);
      A_NEXT (cur_gate_pins) = ap;
      A_INC (cur_gate_pins);
      
      /* -- interconnect arcs -- */
      TS.engine->addNetLeg (drv_pin, ap, TransMode::TRANS_RISE);
      TS.engine->addNetLeg (drv_pin, ap, TransMode::TRANS_FALL);

#if 0      
      printf ("add net leg: "); actpin_print (drv_pin);
      printf (" -> "); actpin_print (ap);
      printf ("\n");
#endif      
	
      /* -- internal arc from this signal to gate output for v_dn -- */
      if (!TS.engine->addDelayArc
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
      printf ("add delay arc: "); ap->Print (stdout);
      printf (" (r) -> "); gate_out->Print (stdout);
      printf (" (f)\n");
#endif      
      if (ei->isTicked()) {
#if 0	
	printf (" ** tick\n");
#endif	
	TS.engine->setEdgeTick
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
	ap = new ActPin (drv_pin->getNet(), gate_out->getCell(), pinname);

	TS.engine->addLoadPin (ap);
#if 0	
	printf ("add load pin: "); actpin_print (ap);
	printf ("\n");
#endif	

	A_NEW (cur_gate_pins, ActPin *);
	A_NEXT (cur_gate_pins) = ap;
	A_INC (cur_gate_pins);
      
	/* -- interconnect arcs -- */
	TS.engine->addNetLeg (drv_pin, ap, TransMode::TRANS_RISE);
	TS.engine->addNetLeg (drv_pin, ap, TransMode::TRANS_FALL);
#if 0
	printf ("add net leg: "); actpin_print (drv_pin);
	printf (" -> "); actpin_print (ap);
	printf ("\n");
#endif	
      }

      /* -- internal arc from this signal to gate output for v_up -- */
      if (!TS.engine->addDelayArc
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
      printf ("add delay arc: "); ap->Print (stdout);
      printf (" (f) -> "); gate_out->Print (stdout);
      printf (" (r)\n");
#endif      
      if (ei->isTicked()) {
#if 0	
	printf (" ** tick\n");
#endif	
	TS.engine->setEdgeTick (ap, TransMode::TRANS_FALL, gate_out, TransMode::TRANS_RISE, true);
      }
    }
    A_LEN (cur_gate_pins) = 0;
  }
  A_FREE (cur_gate_pins);

  if (gr_create_error) {
    clear_timer ();
    return "timing graph construction failed; arcs missing from .lib";
  }

  return NULL;
}

#include <lispCli.h>

const char *timer_run (void)
{
  init ();

  if (!TS.engine) {
    return "timer not initialized";
  }
  
  TS.engine->computeCriticalCycle(TS.lib);
  auto stats = TS.engine->getCriticalCycleRatioAndTicks();

  // timing propagation
  TS.engine->computeTiming4Pins();

  LispSetReturnListStart ();
  LispAppendReturnFloat (stats.first);
  LispAppendReturnInt (stats.second);
  LispSetReturnListEnd ();

  return NULL;
}

#endif
