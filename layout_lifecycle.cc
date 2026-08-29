/*************************************************************************
 * Typed layout and PhyDB lifecycle operations for the timing-driven host.
 *************************************************************************/
#include <cstdio>

#include <act/passes.h>
#include <act/tech.h>
#include "all_cmds.h"
#include "flow.h"

#ifdef FOUND_phydb
bool flow_build_phydb (const std::string &lef_file,
                       const std::string &cell_file,
                       const std::string &def_file,
                       const std::string &tech_config_file)
{
  if (!F.act_toplevel || F.phydb) return false;
  F.phydb = new phydb::PhyDB ();
  F.phydb->ReadLef (lef_file);
  F.phydb->ReadCell (cell_file);
  F.phydb->ReadDef (def_file);
  if (!F.phydb->ReadTechConfigFile (tech_config_file)) {
    delete F.phydb;
    F.phydb = NULL;
    return false;
  }
  F.phydb_lef = 1;
  F.phydb_cell = 1;
  F.phydb_def = 1;
  return true;
}
#endif

bool flow_generate_layout_files (const std::string &lef_file,
                                 const std::string &cell_file,
                                 const std::string &def_file,
                                 double die_llx,
                                 double die_lly,
                                 double die_urx,
                                 double die_ury)
{
  if (!F.act_design || !F.act_toplevel) return false;
  ActPass *booleanize_pass = F.act_design->pass_find ("booleanize");
  ActBooleanizePass *booleanize =
      dynamic_cast<ActBooleanizePass *> (booleanize_pass);
  if (!booleanize) return false;
  booleanize->createNets (F.act_toplevel);
  ActPass *pass = F.act_design->pass_find ("stk2layout");
  ActDynamicPass *layout = dynamic_cast<ActDynamicPass *> (pass);
  if (!layout) {
    layout = new ActDynamicPass (F.act_design, "stk2layout", "pass_layout.so",
                                 "layout");
  }
  if (!layout->loaded()) return false;

  layout->run (F.act_toplevel);
  FILE *lef = fopen (lef_file.c_str (), "w");
  FILE *cell = fopen (cell_file.c_str (), "w");
  if (!lef || !cell) {
    if (lef) fclose (lef);
    if (cell) fclose (cell);
    return false;
  }
  layout->setParam ("lef_file", static_cast<void *> (lef));
  layout->setParam ("cell_file", static_cast<void *> (cell));
  layout->run_recursive (F.act_toplevel, 1);
  fclose (lef);
  fclose (cell);
  layout->run_recursive (F.act_toplevel, 4);

  FILE *def = fopen (def_file.c_str (), "w");
  if (!def) return false;
  layout->setParam ("def_file", static_cast<void *> (def));
  layout->setParam ("do_pins", 1);
  layout->setParam ("is_bb", 1);
  const double unit_conv = Technology::T->scale *
                           config_get_int ("lefdef.micron_conversion") / 1000.0;
  layout->setParam ("bb_x", (die_urx - die_llx) / unit_conv);
  layout->setParam ("bb_y", (die_ury - die_lly) / unit_conv);
  layout->setParam ("bb_llx", die_llx);
  layout->setParam ("bb_lly", die_lly);
  layout->run_recursive (F.act_toplevel, 5);
  fclose (def);
  return true;
}
