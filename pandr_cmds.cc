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

  phydb::Macro *m = F.phydb->GetMacroPtr (std::string (buf));

  if (!m) {
    if (p->isBlackBox() || p->isLowLevelBlackBox() || !p->getUnexpanded()->isDefined()) {
      warning ("Did not find macro for black-box: `%s'", buf);
    }
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

  LispSetReturnListEnd ();

  save_to_log (argc, argv, "s");

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

static int process_phydb_read_techconfig (int argc, char **argv)
{
  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  // input file will be checked by the following function
  bool ret = F.phydb->ReadTechConfigFile (argc, argv);
  if (!ret) {
    fprintf (stderr, "%s: failed to read technology configuration file!\n", argv[0]);
    return LISP_RET_ERROR;
  }

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

static int process_phydb_write_cluster (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }
  
  F.phydb->WriteCluster (argv[1]);
  save_to_log (argc, argv, "s");

  return LISP_RET_TRUE;
}

static int process_phydb_write_aux_rect (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 2, "<file>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  std::string name;

  name = argv[1];
  name = name + "_ppnp.rect";

  F.phydb->SavePpNpToRectFile (name);

  name = argv[1];
  name = name + "_wells.rect";

  F.phydb->SaveWellToRectFile (name);

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

  /* -- mangle string: LEF/DEF has mangled strings -- */
  char *buf;
  if (strlen (argv[1]) == 0) {
    fprintf (stderr, "%s: empty cell type?\n", argv[0]);
    return LISP_RET_ERROR;
  }
  MALLOC (buf, char, strlen(argv[1])*2 + 1);
  F.act_design->mangle_string (argv[1], buf, strlen (argv[1])*2+1);
  std::string macnm(buf);
  FREE (buf);

  phydb::Macro *cell = F.phydb->GetMacroPtr (macnm);
  
  if (!cell) {
    fprintf (stderr, "%s: cell type `%s' not found.\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  int llx = atoi (argv[2]);
  int lly = atoi (argv[3]);
  phydb::CompOrient orient;
  if (strcmp (argv[4], "N") == 0) {
    orient = phydb::CompOrient::N;
  }
  else if (strcmp (argv[4], "FS") == 0) {
    orient = phydb::CompOrient::FS;
  }
  else {
    fprintf (stderr, "%s: unknown orientation `%s'\n", argv[0], argv[4]);
  }

  std::string tmp;
  do {
    tmp = "__xn" + std::to_string (ncomp++);
  } while (F.phydb->IsComponentExisting (tmp));

  F.phydb->AddComponent (tmp, cell, phydb::PlaceStatus::FIXED, llx, lly, orient);

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

  char *buf;
  if (strlen (argv[1]) == 0) {
    fprintf (stderr, "%s: empty instance name?\n", argv[0]);
    return LISP_RET_ERROR;
  }
  MALLOC (buf, char, strlen(argv[1])*2 + 1);
  F.act_design->mangle_string (argv[1], buf, strlen (argv[1])*2+1);

  std::string tmpnm (buf);
  
  FREE (buf);
  
  phydb::Component *comp = F.phydb->GetComponentPtr (tmpnm);
  if (!comp) {
    fprintf (stderr, "%s: instance `%s' not found.\n", argv[0], argv[1]);
    return LISP_RET_ERROR;
  }
  int llx = atoi (argv[2]);
  int lly = atoi (argv[3]);
  phydb::CompOrient orient;
  if (strcmp (argv[4], "N") == 0) {
    orient = phydb::CompOrient::N;
  }
  else if (strcmp (argv[4], "FS") == 0) {
    orient = phydb::CompOrient::FS;
  }
  else {
    fprintf (stderr, "%s: unknown orientation `%s'\n", argv[0], argv[4]);
  }

  comp->SetPlacementStatus (phydb::PlaceStatus::FIXED);
  comp->SetLocation (llx, lly);
  comp->SetOrientation (orient);

  save_to_log (argc, argv, "siis");

  return LISP_RET_TRUE;
}

static int process_phydb_add_io (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 5, "<iopin_name> <net_name> <direction> <use>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  std::string iopin_name(argv[1]);
  std::string net_name(argv[2]);
  std::string direction_str(argv[3]);
  std::string use_str(argv[4]);
  phydb::SignalDirection direction = phydb::StrToSignalDirection(direction_str);
  phydb::SignalUse use = phydb::StrToSignalUse(use_str);
  int net_id = F.phydb->GetNetId(net_name);

  phydb::IOPin *io_pin = F.phydb->AddIoPin(iopin_name, direction, use);
  io_pin->SetNetId(net_id);

  save_to_log (argc, argv, "siis");

  return LISP_RET_TRUE;
}

static int process_phydb_place_io (int argc, char **argv)
{
  if (!std_argcheck (argc, argv, 11, "<iopin_name> <metal_name> <lx> <ly> <ux> <uy> <placement_status> <x> <y> <orientation>", STATE_EXPANDED)) {
    return LISP_RET_ERROR;
  }

  if (F.phydb == NULL) {
    fprintf (stderr, "%s: phydb needs to be initialized!\n", argv[0]);
    return LISP_RET_ERROR;
  }

  std::string iopin_name(argv[1]);
  phydb::IOPin *io_pin = F.phydb->GetIoPinPtr(iopin_name);
  try {
    std::string metal_name(argv[2]);
    int lx = atoi (argv[3]);
    int ly = atoi (argv[4]);
    int ux = atoi (argv[5]);
    int uy = atoi (argv[6]);
    io_pin->SetShape(metal_name, lx, ly, ux, uy);
  } catch (...) {
    fprintf (stderr, "%s %s %s %s: cannot convert string to int!\n", argv[3], argv[4], argv[5], argv[6]);
    return LISP_RET_ERROR;
  }
  try {
    std::string place_status_str(argv[7]);
    int x = atoi (argv[8]);
    int y = atoi (argv[9]);
    std::string orient_str(argv[10]);
    phydb::PlaceStatus place_status = phydb::StrToPlaceStatus(place_status_str);
    phydb::CompOrient orient = phydb::StrToCompOrient(orient_str);
    io_pin->SetPlacement(place_status, x, y, orient);
  } catch (...) {
    fprintf (stderr, "%s %s: cannot convert string to int!\n", argv[8], argv[9]);
    return LISP_RET_ERROR;
  }

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
  { "read-techconfig", "<file> - read technology configuration file", 
    process_phydb_read_techconfig },
  { "read-cluster", "<file> - read Cluster file and populate database", 
    process_phydb_read_cluster },

  { "place-cell", "<cell-type> <llx> <lly> <N|FS> - add a new cell to a fixed location",
    process_phydb_place_cell },

  { "place-inst", "<inst> <llx> <lly> <N|FS> - place an instance at a fixed location",
    process_phydb_place_inst },

  { "add-io", "<iopin_name> <net_name> <direction> <use> - add a new I/O pin",
    process_phydb_add_io},

  { "place-io", "<iopin_name> <metal_name> <lx> <ly> <ux> <uy> <placement_status> <x> <y> <orientation> - place an I/O pin at a fixed location",
    process_phydb_place_io},
  
  { "write-def", "<file> - write DEF from database",
    process_phydb_write_def},
  { "write-guide", "<file> - write GUIDE from database",
    process_phydb_write_guide},
  { "write-cluster", "<file> - write CLUSTER from database",
    process_phydb_write_cluster},
  { "write-aux-rect", "<file> - write PP/NP cover and well rect files",
    process_phydb_write_aux_rect },

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

