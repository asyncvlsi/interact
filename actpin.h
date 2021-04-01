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

#ifdef FOUND_Galois

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

#endif

/*
  Pins can be inputs or outputs of gates
  
  Vertices in the tagged timing graph correspond to production rules (x+ or x-)

  An input pin and output pin of a gate has two timingvertexinfo * associated with it
  (for rise and fall)
*/


/*------------------------------------------------------------------------
 *
 *  ActNet pointer: a combination of a path to an instance, followed
 *  by the local canonical net.
 * 
 *------------------------------------------------------------------------
 */
class ActNet {
public:
  ActNet (const char *_iname, act_connection *_net) {
    inst = _iname;
    net = _net;
  }

  int operator==(ActNet& n) {
    return (n.net == net && n.inst == inst);
  }

  void Print (FILE *fp) {
    ActId *tmp;
    if (inst) {
      fprintf (fp, "%s/", inst);
    }
    tmp = net->toid();
    tmp->Print (fp);
    delete tmp;
  }

private:
  const char *inst;		// root of where the net exists
  act_connection *net;		// the net within that root instance
};

class ActCell {
public:
  ActCell (const char *_inst, Process *_p) {
    inst = _inst;
    p = _p;
  }

  Process *getCellProc () { return p; }
  const char *getInstName() { return inst; }

  int operator==(ActCell& ac) {
    return inst == ac.inst && p == ac.p;
  }

private:
  const char *inst;
  Process *p;
};
  


class ActPin {
public:
  ActPin (ActNet *_n, ActCell *_ac, act_connection *_pin) {
    net = _n;
    ac = _ac;
    pin = _pin;
  }
	  
  void Print (FILE *fp);

  ActNet *getNet() { return net; }
  ActCell *getCell() { return ac; }

  act_boolean_netlist_t *cellBNL(ActBooleanizePass *p) {
    return p->getBNL (ac->getCellProc());
  }

  /* checks if two pins are connected, i.e. they belong to the same net */
  int operator==(ActPin &a) {
    return (*(a.net) == *net);
  }

  void sPrintPin (char *buf, int sz);
  void sPrintCellType (char *buf, int sz);
  void sPrintFullName (char *buf, int sz);
  
private:
  ActNet *net;			// net name

  /* pin */
  ActCell *ac;
  act_connection *pin;
};


#endif /* __ACTPIN_H__ */
