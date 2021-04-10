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
#include "galois/eda/utility/ExtPinTranslator.h"

class ActPinTranslator : public galois::eda::utility::ExtPinTranslator {
 public:
  std::string getPinName (void * const p) const;
  std::string getCellInstType (void * const p) const;
  std::string getFullName (void * const p) const;

  bool isP1LessThanP2 (void * const p1, void * const p2) const;
  bool isSamePin (void * const p1, void * const p2) const;
  bool isInSameNet (void * const p1, void * const p2) const;
  bool isInSameInst (void * const p1, void * const p2) const;
};

/*
  Pins can be inputs or outputs of gates
  
  Vertices in the tagged timing graph correspond to production rules (x+ or x-)

  An input pin and output pin of a gate has two timingvertexinfo * associated with it
  (for rise and fall)
*/

class ActPin {
public:
  ActPin (TimingVertexInfo *net, // this is the timing vertex for the driver
	  TimingVertexInfo *pinvtx,  // this is the timing vertex for the pin
	  act_connection *pinname) { // the pin name for "ac"

    _driver_vtx = net;
    _pin_vtx = pinvtx;
    _pin = pinname;
  }
	  
  void Print (FILE *fp);

  TimingVertexInfo *getNet() { return _driver_vtx; }
  TimingVertexInfo *getInst() { return _pin_vtx; }
  act_connection *getPin() { return _pin; }

  act_boolean_netlist_t *cellBNL(ActBooleanizePass *p) {
    return p->getBNL (_pin_vtx->getCell());
  }

  /* checks if two pins are connected, i.e. they belong to the same net */
  int operator==(ActPin &a) {
    return (a._driver_vtx == _driver_vtx);
  }

  void sPrintPin (char *buf, int sz);
  void sPrintCellType (char *buf, int sz);
  void sPrintFullName (char *buf, int sz);
  
private:
  TimingVertexInfo *_driver_vtx; // net name

  /* pin */
  TimingVertexInfo *_pin_vtx;
  act_connection *_pin;
};

#endif

#endif /* __ACTPIN_H__ */
