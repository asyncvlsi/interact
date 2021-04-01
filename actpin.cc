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

#ifdef FOUND_Galois

/*-- for .lib lookup --*/
std::string ActPinTranslator::getPinName (void * const p) const
{
  ActPin *x = (ActPin *)p;
  char buf[1024], buf2[1024];

  x->sPrintPin (buf, 1024);
  ActNamespace::Global()->Act()->msnprintf (buf2, 1024, "%s", buf);
  return std::string (buf2);
}

/*-- for .lib lookup --*/
std::string ActPinTranslator::getCellInstType (void * const p) const
{
  ActPin *x = (ActPin *)p;
  char buf[1024];

  x->sPrintCellType (buf, 1024);
  
  return std::string (buf);
}

/*-- for printing --*/
std::string ActPinTranslator::getFullName (void * const p) const
{
  ActPin *x = (ActPin *)p;
  char buf[10240];

  x->sPrintFullName (buf, 10240);

  return std::string (buf);
}

/* -- fixme -- */
bool ActPinTranslator::isP1LessThanP2 (void * const p1, void * const p2) const
{
  return ((unsigned long)p1) < ((unsigned long)p2);
}

/* -- fixme -- */
bool ActPinTranslator::isSamePin (void * const p1, void * const p2) const
{
  return ((unsigned long)p1) == ((unsigned long)p2);
}

bool ActPinTranslator::isInSameNet (void * const p1, void * const p2) const
{
  ActPin *x1 = (ActPin *)p1;
  ActPin *x2 = (ActPin *)p2;

  return (*x1) == (*x2);
}

bool ActPinTranslator::isInSameInst (void * const p1, void * const p2) const
{
  ActPin *x1 = (ActPin *)p1;
  ActPin *x2 = (ActPin *)p2;

  return (*(x1->getCell()) == *(x2->getCell()));
}

#endif


/* -- printing functions -- */

void ActPin::Print (FILE *fp)
{
  ActId *x;
  char buf[1024];

  net->Print (fp);
  sPrintFullName (buf, 1024);
  fprintf (fp, "(%s)", buf);
}

void ActPin::sPrintPin (char *buf, int sz)
{
  ActId *tmp;
  tmp = pin->toid();
  tmp->sPrint (buf, sz);
  delete tmp;
}

void ActPin::sPrintCellType (char *buf, int sz)
{
  ActNamespace::Global()->Act()->msnprintfproc (buf, sz, ac->getCellProc());
}

void ActPin::sPrintFullName (char *buf, int sz)
{
  snprintf (buf, 10240, "%s:", ac->getInstName());
  sPrintPin (buf + strlen (buf), 10240 - strlen (buf));
}
