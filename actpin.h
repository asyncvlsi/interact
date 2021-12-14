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
#ifndef __ACTPIN_H__
#define __ACTPIN_H__

#include "config_pkg.h"

#include <act/act.h>
#include <act/passes.h>

#ifdef FOUND_galois_eda

#include <act/timing/tgraph.h>
#include "galois/eda/utility/ExtNetlistAdaptor.h"

class ActNetlistAdaptor : public galois::eda::utility::ExtNetlistAdaptor {
private:
  Act *_a;
  Process *_top;
  TaggedTG *_tg;
  ActDynamicPass *_tp;
  ActStatePass *_sp;
  ActBooleanizePass *_bp;
  struct pHashtable *_map;

  int _idToTimingVertex (ActId *) const;
  
public:
  ActNetlistAdaptor (Act *a, Process *p);

  void setEdgeMap (struct pHashtable *map) { _map = map; }
  
  void *getPinFromFullName (const std::string& name,
			    const char divider = '/',
			    const char busDelimL = '[',
			    const char busDelimR = ']',
			    const char delimiter = ':') const;
  void *getInstFromFullName (const std::string& name,
			     const char divider = '/',
			     const char busDelimL = '[',
			     const char busDelimR = ']') const;
  void *getNetFromFullName (const std::string& name,
			     const char divider = '/',
			     const char busDelimL = '[',
			     const char busDelimR = ']') const;

  std::string getFullName4Pin (void *const pin) const;
  std::string getFullName4Inst (void *const inst) const;
  std::string getFullName4Net (void *const net) const;

  void *getInst4Pin (void *const pin) const;
  std::string getInstTypeName4Inst(void* const inst) const;
  std::string getPinNameInInst4Pin(void* const pin) const;

  void *getNet4Pin (void *const pin) const;

  bool isPin1LessThanPin2 (void * const p1, void * const p2) const;
  bool isNet1LessThanNet2 (void * const p1, void * const p2) const;
  bool isInst1LessThanInst2 (void * const p1, void * const p2) const;
  bool isSamePin (void * const p1, void * const p2) const;
  bool isSameNet (void * const p1, void * const p2) const;
  bool isSameInst (void * const p1, void * const p2) const;
};


/*
  Pins can be inputs or outputs of gates
  
  Vertices in the tagged timing graph correspond to production rules (x+ or x-)

  An input pin and output pin of a gate has two timingvertexinfo * associated with it
  (for rise and fall)
*/

class ActPin {
public:
  ActPin (AGvertex *net, // this is the timing vertex for the driver
	  AGvertex *pinvtx,  // this is the timing vertex for the pin
	  act_connection *pinname) { // the pin name for "ac"

    _driver_vtx = net;
    _pin_vtx = pinvtx;
    _pin = pinname;
  }

  ActPin (AGvertex *net, unsigned int prim_inp) {
    _driver_vtx = net;
    _pin_vtx = net;
    _pin = (act_connection *) (((unsigned long)prim_inp << 1) | 1);
  }

  void Print (FILE *fp);

  AGvertex *getNetVertex() { return _driver_vtx; }
  AGvertex *getInstVertex() { return _pin_vtx; }
  

  TimingVertexInfo *getNet() {
    return (TimingVertexInfo *) (_driver_vtx->getInfo());
  }
  TimingVertexInfo *getInst() {
    return (TimingVertexInfo *) (_pin_vtx->getInfo());
  }
  act_connection *getPin() { return _pin; }

  int isExternalInput() { return (((unsigned long)_pin) & 1); }
  int externalId() { return (((unsigned long)_pin) >> 1); };

  act_boolean_netlist_t *cellBNL(ActBooleanizePass *p) {
    return p->getBNL (getInst()->getCell());
  }

  /* checks if two pins are connected, i.e. they belong to the same net */
  int operator==(ActPin &a) {
    return (a._driver_vtx == _driver_vtx);
  }

  void sPrintPin (char *buf, int sz);
  void sPrintCellType (char *buf, int sz);
  void sPrintFullName (char *buf, int sz);
  
private:
  AGvertex *_driver_vtx;
  //TimingVertexInfo *_driver_vtx; // net name

  /* pin */
  //TimingVertexInfo *_pin_vtx;
  AGvertex *_pin_vtx;
  act_connection *_pin;
};

#endif

#endif /* __ACTPIN_H__ */
