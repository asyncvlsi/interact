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
#include <act/passes.h>
#include <common/list.h>
#include <common/pp.h>
#include <lispCli.h>
#include <string.h>
#include "all_cmds.h"
#include "ptr_manager.h"
#include "flow.h"
#include "actpin.h"
#include <act/tech.h>


#if defined(FOUND_phydb)

static int process_phydb_init (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.act_toplevel == NULL) {
    fprintf (stderr, "%s: no top-level process specified\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.cell_map != 1) {
    fprintf (stderr, "%s: phydb requires the design to be mapped to cells.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  ActNetlistPass *np = getNetlistPass();
  if (np->completed()) {
    F.ckt_gen = 1;
  }

  if (F.ckt_gen != 1) {
    fprintf (stderr, "%s: phydb requires the transistor netlist to be created.\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.phydb != NULL) {
    fprintf (stderr, "%s: phydb already initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  F.phydb = new phydb::PhyDB();
  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_phydb_close (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: no database!\n", argv[0]);
    return LISP_RET_ERROR;
  }
 
  delete F.phydb;
  F.phydb = NULL;

  save_to_log (argc, argv, "");

  return LISP_RET_TRUE;
}

static int process_phydb_read_lef (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);

  F.phydb->ReadLef (argv[1]);
  F.phydb_lef = 1;

  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static void _find_macro (void *cookie, Process *p)
{
  char buf[10240];

  if (!p) {
    return;
  }

  if (!p->isCell()) {
    return;
  }

  F.act_design->msnprintfproc (buf, 10240, p);

  Macro *m = F.phydb->GetMacroPtr (std::string (buf));
  if (!m) {
    return;
  }

  double w, h;
  w = m->GetWidth();
  h = m->GetHeight();

  w = w*1000.0/Technology::T->scale;
  h = h*1000.0/Technology::T->scale;

  LispAppendListStart ();
  LispAppendReturnString (buf);
  LispAppendReturnInt ((long)w);
  LispAppendReturnInt ((long)h);
  LispAppendListEnd ();
}

static int process_phydb_get_used_lef (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 1, "", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  if (F.phydb_lef == 0) {
    fprintf (stderr, "%s: no lef file in phydb!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  ActPass *ap = F.act_design->pass_find ("apply");
  ActApplyPass *app;
  if (!ap) {
    app = new ActApplyPass (F.act_design);
  }
  else {
    app = dynamic_cast<ActApplyPass *> (ap);
  }
  Assert (app, "Hmm");

  LispSetReturnListStart ();

  app->setCookie (NULL);
  app->setProcFn (_find_macro);
  app->setChannelFn (NULL);
  app->setDataFn (NULL);
  app->run_per_type (F.act_toplevel);
  app->setProcFn (NULL);

  save_to_log (argc, argv, "s");

  LispSetReturnListEnd ();

  return LISP_RET_LIST;
}


static int process_phydb_read_def (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);
  
  if (F.phydb_def) {
    fprintf (stderr, "%s: already read in DEF file! Command ignored.\n",
        argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.phydb->ReadDef (argv[1]);
  F.phydb_def = 1;
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_read_cell (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open file `%s' for reading\n", argv[0],
        argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);

  if (F.phydb_cell) {
    fprintf (stderr, "%s: reading additional .cell file; continuing anyway\n",
        argv[0]);
  }
  
  F.phydb->ReadCell (argv[1]);
  F.phydb_cell = 1;
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_read_cluster (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  FILE *fp = fopen (argv[1], "r");
  if (!fp) {
    fprintf (stderr, "%s: could not open cluster file `%s' for reading\n", argv[0],
        argv[1]);
    return LISP_RET_ERROR;
  }
  fclose (fp);

  
  F.phydb->ReadCluster (argv[1]);
  F.phydb_cluster = 1;
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_write_def (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.phydb->WriteDef (argv[1]);
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_write_guide (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.phydb->WriteGuide (argv[1]);
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_place_cell (int argc, char **argv)
{
  static int ncomp = 0;
  if (!std_argcheck (argc, argv, 5, "<cell-type> <llx> <lly> <orient>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  phydb::Macro *cell = F.phydb->GetMacroPtr (argv[1]);
  if (!cell) {
    fprintf (stderr, "%s: cell type `%s' not found.\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  int llx = atoi (argv[2]);
  int lly = atoi (argv[3]);
  phydb::CompOrient orient;
  if (strcmp (argv[4], "N") == 0) {
    orient = phydb::N;
  }
  else if (strcmp (argv[4], "FS") == 0) {
    orient = phydb::FS;
  }
  else {
    fprintf (stderr, "%s: unknown orientation `%s'\n", argv[0], argv[4]);
  }

  std::string tmp;
  do {
    tmp = "_xn" + std::to_string (ncomp++);
  } while (F.phydb->IsComponentExisting (tmp));

  std::string compname = cell->GetName();
   
  F.phydb->AddComponent (tmp, compname, phydb::FIXED, llx, lly, orient);

  save_to_log (argc, argv, "siis");

  return LISP_RET_TRUE;
}

static int process_phydb_place_inst (int argc, char **argv)
{
  static int ncomp = 0;
  if (!std_argcheck (argc, argv, 5, "<inst> <llx> <lly> <orient>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  std::string tmpnm (argv[1]);
  phydb::Component *comp = F.phydb->GetComponentPtr (tmpnm);
  if (!comp) {
    fprintf (stderr, "%s: instance `%s' not found.\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  int llx = atoi (argv[2]);
  int lly = atoi (argv[3]);
  phydb::CompOrient orient;
  if (strcmp (argv[4], "N") == 0) {
    orient = phydb::N;
  }
  else if (strcmp (argv[4], "FS") == 0) {
    orient = phydb::FS;
  }
  else {
    fprintf (stderr, "%s: unknown orientation `%s'\n", argv[0], argv[4]);
  }

  comp->SetPlacementStatus (phydb::FIXED);
  comp->SetLocation (llx, lly);
  comp->SetOrientation (orient);

  save_to_log (argc, argv, "siis");

  return LISP_RET_TRUE;
}

static struct LispCliCommand phydb_cmds[] = {
  { NULL, "Physical database access", NULL },
  
  { "init", "- initialize physical database", process_phydb_init },
  { "read-lef", "<file> - read LEF and populate database",
    process_phydb_read_lef },
  { "get-used-lef", "- return list of macros used by the design",
    process_phydb_get_used_lef },
  { "read-def", "<file> - read DEF and populate database",
    process_phydb_read_def },
  { "read-cell", "<file> - read CELL file and populate database", 
    process_phydb_read_cell },
  { "read-cluster", "<file> - read Cluster file and populate database", 
    process_phydb_read_cluster },

  { "place-cell", "<cell-type> <llx> <lly> <N|FS> - add a new cell to a fixed location", process_phydb_place_cell },

  { "place-inst", "<inst> <llx> <lly> <N|FS> - place an instance at a fixed location", process_phydb_place_inst },
  
  { "write-def", "<file> - write DEF from database",
    process_phydb_write_def},
  { "write-guide", "<file> - write GUIDE from database",
    process_phydb_write_guide},
  { "close", "- tear down physical database", process_phydb_close }

};

#endif //end FOUND_phydb


void pandr_cmds_init (void)
{
  timer_cmds_init ();
#if defined(FOUND_phydb)
  LispCliAddCommands ("phydb", phydb_cmds,
            sizeof (phydb_cmds)/sizeof (phydb_cmds[0]));
#endif

  placement_cmds_init ();
  routing_cmds_init ();
}

