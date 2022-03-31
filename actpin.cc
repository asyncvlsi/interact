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

  if (isExternalInput()) {
    snprintf (buf, sz, "pi$%d", externalId());
  }
  else {
    tmp = _pin->toid();
    tmp->sPrint (buf, sz);
    delete tmp;
  }
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
  _map = NULL;
  
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
  pass = a->pass_find ("booleanize");
  if (pass) {
    _bp = dynamic_cast<ActBooleanizePass *> (pass);
  }
  if (!pass || !_bp) {
    fatal_error ("No Booleanize pass!");
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
  char *buf;
  int sz;

  sz = 1 + strlen (name.c_str());
  MALLOC (buf, char, sz);
  if (_a->unmangle_string (name.c_str(), buf, sz) == -1) {
     printf ("WARNING: unmangle of `%s' failed!\n", name.c_str());
     FREE (buf);
     return NULL; 
  }
  int pos = strlen (buf)-1;
  while (pos > 0 && buf[pos] != delimiter) {
     pos--;
  }
  if (pos == 0) {
     printf ("WARNING: `%s' couldn't find the pin separator `%c'\n", buf, delimiter);
     FREE (buf);
     return NULL;
  }
  buf[pos] = '.';

  ActId *x = ActId::parseId (buf, divider, busDelimL,
			     busDelimR, '.');

  if (x) {
    vid = _idToTimingVertex (x);
    delete x;
    x = NULL;
  }
  else {
    vid = -1;
  }
  if (vid == -1) {
    FREE (buf);
    return NULL;
  }
  AGvertex *vtx = _tg->getVertex (vid);
  TimingVertexInfo *tv = (TimingVertexInfo *) vtx->getInfo();
  ActPin *me;

  /* driver */
  me = (ActPin *) tv->getSpace();
  act_boolean_netlist_t *bnl = me->cellBNL (_bp);
  
  /* pin must be here */
  ActId *pin_act_id = ActId::parseId (buf+pos+1);
  if (!pin_act_id) {
    printf ("WARNING: `%s' is an invalid pin name\n", buf+pos+1);
    FREE (buf);
    return NULL;
  }

#if 0
  printf (">>> %s\n", buf);
  printf ("   >>> pin id: ");
  pin_act_id->Print (stdout);
  printf ("\n");
#endif

  if (!me->isExternalInput()) {
    /* not an external pin, check if it is a driver */

    /*-- check if this is the driver pin --*/
    if (bnl->cur->localLookup (pin_act_id, NULL)) {
      if (pin_act_id->validateDeref (bnl->cur)) {
	act_connection *pin_req = pin_act_id->Canonical (bnl->cur);
	Assert (pin_req, "What?");
	delete pin_act_id;

	if (pin_req == me->getPin()) {
#if 0
	  printf (">> Returning driver: "); me->Print (stdout);
	  printf ("\n");
#endif
	  return me;
	}
      }
    }
  }
  pin_act_id = NULL;

  /*-- must be a pin associated with the fanout --*/

  char *buf2;
  AGvertex *getInstVtx = NULL;
  MALLOC (buf2, char, sz);
  buf[pos] = ':';

  AGvertexFwdIter fw(_tg, vid);
  for (fw = fw.begin(); fw != fw.end(); fw++) {
    AGedge *be = (*fw);
    TimingEdgeInfo *ei = (TimingEdgeInfo *)be->getInfo();
    if (!ei) continue;

    phash_bucket_t *b = phash_lookup (_map, ei);
    ActPin *tp;
    if (b) {
      tp = (ActPin *)b->v;
    }
    else {
      tp = NULL;
    }

    if (tp) {
      tp->sPrintFullName (buf2, sz);
      if (strcmp (buf,buf2) == 0) {
#if 0
	printf (">> Returning: %s\n", buf2);
#endif
	FREE (buf);
	FREE (buf2);
	return tp;
      }
    }
  }

  AGvertexFwdIter fw2(_tg, vid+1);
  for (fw2 = fw2.begin(); fw2 != fw2.end(); fw2++) {
    AGedge *be = (*fw2);
    TimingEdgeInfo *ei = (TimingEdgeInfo *)be->getInfo();
    if (!ei) continue;

    phash_bucket_t *b = phash_lookup (_map, ei);
    ActPin *tp;
    if (b) {
      tp = (ActPin *)b->v;
    }
    else {
      tp = NULL;
    }

    if (tp) {
      tp->sPrintFullName (buf2, sz);
      if (strcmp (buf,buf2) == 0) {
#if 0
	printf (">> Returning: %s\n", buf2);
#endif
	FREE (buf);
	FREE (buf2);
	return tp;
      }
    }
  }
  FREE (buf);
  FREE (buf2);

  printf ("WARNING: pin for `%s' not found?\n", name.c_str());
  
  return NULL;
}

void *ActNetlistAdaptor::getInstFromFullName (const std::string& name,
					      const char divider,
					      const char busDelimL,
					      const char busDelimR) const
{
  char *buf;
  int sz;

  sz = 1 + strlen (name.c_str());
  MALLOC (buf, char, sz);
  if (_a->unmangle_string (name.c_str(), buf, sz) == -1) {
     printf ("WARNING: unmangle of `%s' failed!\n", name.c_str());
     FREE (buf);
     return NULL; 
  }
  std::string tmp = buf;
  FREE (buf);
  return getNetFromFullName (tmp, divider, busDelimL, busDelimR);
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

  if (p->isExternalInput()) {
    return NULL;
  }

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

