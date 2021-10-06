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
#include <act/act.h>
#include <string.h>
#include "actpin.h"

#ifdef FOUND_galois_eda

/* -- printing functions -- */
void ActPin::Print (FILE *fp)
{
  ActId *x;
  char buf[1024];

  if (_driver_vtx) {
    char *tmp = getNet()->getFullInstPath();
    fprintf (fp, "%s", tmp);
    FREE (tmp);
  }
  sPrintFullName (buf, 1024);
  fprintf (fp, "(%s)", buf);
}

void ActPin::sPrintPin (char *buf, int sz)
{
  ActId *tmp;
  tmp = _pin->toid();
  tmp->sPrint (buf, sz);
  delete tmp;
}

void ActPin::sPrintCellType (char *buf, int sz)
{
  ActNamespace::Global()->Act()->msnprintfproc (buf, sz, getInst()->getCell());
}

void ActPin::sPrintFullName (char *buf, int sz)
{
  char *tmp = getInst()->getFullInstPath ();
  snprintf (buf, 10240, "%s:", tmp);
  FREE (tmp);
  sPrintPin (buf + strlen (buf), 10240 - strlen (buf));
}



ActNetlistAdaptor::ActNetlistAdaptor (Act *a, Process *p)
{
  ActPass *pass;
  _a = a;
  _top = p;

  
  pass = a->pass_find ("taggedTG");
  if (pass) {
    _tp = dynamic_cast<ActDynamicPass *>(pass);
  }
  if (!pass  || !_tp) {
    fatal_error ("Need to have timing analysis!");
  }
  _tg = (TaggedTG *) _tp->getMap (_top);
  if (!_tg) {
    fatal_error ("No timing graph!");
  }
  pass = a->pass_find ("collect_state");
  if (pass) {
    _sp = dynamic_cast<ActStatePass *> (pass);
  }
  if (!_sp || !pass) {
    fatal_error ("No state pass!");
  }
}

int ActNetlistAdaptor::_idToTimingVertex (ActId *id) const
{
  int goff;
  Array *x;
  InstType *itx;

  /* -- validate the type of this identifier -- */

  itx = _top->CurScope()->FullLookup (id, &x);
  if (itx == NULL) {
    id->Print (stderr);
    fprintf (stderr, ": ID not found\n");
    return -1;
  }
  if (!TypeFactory::isBoolType (itx)) {
    id->Print (stderr);
    fprintf (stderr, ": ID is not a signal\n");
    return -1;
  }
  if (itx->arrayInfo() && (!x || !x->isDeref())) {
    id->Print (stderr);
    fprintf (stderr, ": ID is not an array\n");
    return -1;
  }

  /* -- check all the de-references are valid -- */
  if (!id->validateDeref (_top->CurScope())) {
    id->Print (stderr);
    fprintf (stderr, ": invalid array reference\n");
    return -1;
  }

  if (!_sp->checkIdExists (id)) {
    id->Print (stderr);
    fprintf (stderr, ": identifier not found in the timing graph!\n");
    return -1;
  }

  goff = _sp->globalBoolOffset (id);
  
  return 2*(_tg->globOffset() + goff);
}


void *ActNetlistAdaptor::getPinFromFullName (const std::string& name,
					     const char divider,
					     const char busDelimL,
					     const char busDelimR,
					     const char delimiter) const
{
  int vid;
  ActId *x = ActId::parseId (name.c_str(), divider, busDelimL,
			     busDelimR, divider);
  
  return NULL;
}

void *ActNetlistAdaptor::getInstFromFullName (const std::string& name,
					      const char divider,
					      const char busDelimL,
					      const char busDelimR) const
{
  return getNetFromFullName (name, divider, busDelimL, busDelimR);
}

void *ActNetlistAdaptor::getNetFromFullName (const std::string& name,
					     const char divider,
					     const char busDelimL,
					     const char busDelimR) const
{
  int vid;
  ActId *x = ActId::parseId (name.c_str(), divider, busDelimL,
			     busDelimR, divider);
  if (x) {
    vid = _idToTimingVertex (x);
  }
  else {
    vid = -1;
  }
  if (vid == -1) {
    return NULL;
  }
  else {
    return _tg->getVertex (vid)->getInfo();
  }
}

/*-- file I/O and debugging --*/
std::string ActNetlistAdaptor::getFullName4Pin (void *const pin) const
{
  ActPin *x = (ActPin *)pin;
  char buf[1024];

  x->sPrintFullName (buf, 1024);
  return std::string (buf);
}


std::string ActNetlistAdaptor::getFullName4Inst (void *const inst) const
{
  TimingVertexInfo *_inst = (TimingVertexInfo *)inst;
  char *tmp = _inst->getFullInstPath();
  std::string ret = std::string (tmp);
  FREE (tmp);
  return ret;
}

std::string ActNetlistAdaptor::getFullName4Net (void *const net) const
{
  TimingVertexInfo *_net = (TimingVertexInfo *)net;
  char *tmp = _net->getFullInstPath();
  std::string ret = std::string (tmp);
  FREE (tmp);
  return ret;
}

void *ActNetlistAdaptor::getInst4Pin (void *const pin) const
{
  ActPin *p = (ActPin *)pin;
  return p->getInst();
}

std::string ActNetlistAdaptor::getInstTypeName4Inst (void *const inst) const
{
  TimingVertexInfo *_inst = (TimingVertexInfo *)inst;
  char buf[1024];

  _a->msnprintfproc (buf, 1024, _inst->getCell());

  return std::string (buf);
}

std::string ActNetlistAdaptor::getPinNameInInst4Pin (void *const pin) const
{
  ActPin *x = (ActPin *)pin;
  char buf[1024], buf2[1024];

  x->sPrintPin (buf, 1024);
  ActNamespace::Global()->Act()->msnprintf (buf2, 1024, "%s", buf);
  return std::string (buf2);
}

void *ActNetlistAdaptor::getNet4Pin (void *const pin) const
{
  ActPin *p = (ActPin *)pin;
  return p->getNet();
}

bool ActNetlistAdaptor::isPin1LessThanPin2 (void * const p1, void * const p2) const
{
  ActPin *x1 = (ActPin *)p1;
  ActPin *x2 = (ActPin *)p2;
  if (((unsigned long)x1->getInst()) < ((unsigned long)x2->getInst())) {
    return true;
  }
  else {
    if (((unsigned long)x1->getPin()) < ((unsigned long)x2->getPin())) {
      return true;
    }
    else {
      return false;
    }
  }
}

bool ActNetlistAdaptor::isNet1LessThanNet2 (void * const p1, void * const p2) const
{
  ActPin *x1 = (ActPin *)p1;
  ActPin *x2 = (ActPin *)p2;

  return ((unsigned long)x1->getNet()) < ((unsigned long)x2->getNet()) ?
    true : false;
}


bool ActNetlistAdaptor::isInst1LessThanInst2 (void * const p1, void * const p2) const
{
  ActPin *x1 = (ActPin *)p1;
  ActPin *x2 = (ActPin *)p2;

  return ((unsigned long)x1->getInst()) < ((unsigned long)x2->getInst()) ?
    true : false;
}

bool ActNetlistAdaptor::isSamePin (void * const p1, void * const p2) const
{
  ActPin *x1 = (ActPin *)p1;
  ActPin *x2 = (ActPin *)p2;

  return (x1->getInst() == x2->getInst()) && (x1->getPin() == x2->getPin());
}

bool ActNetlistAdaptor::isSameNet (void * const p1, void * const p2) const
{
  ActPin *x1 = (ActPin *)p1;
  ActPin *x2 = (ActPin *)p2;

  return (x1->getNet() == x2->getNet());
}


bool ActNetlistAdaptor::isSameInst (void * const p1, void * const p2) const
{
  ActPin *x1 = (ActPin *)p1;
  ActPin *x2 = (ActPin *)p2;

  return (x1->getInst() == x2->getInst());
}

#endif

