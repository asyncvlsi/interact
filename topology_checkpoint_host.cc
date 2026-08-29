/*************************************************************************
 * The ACT-authoritative topology host for a Dali placement checkpoint.
 *
 * Dali owns global placement: when to stop, whether a checkpoint is worth
 * taking, and what happens to the placement afterwards. What it cannot own is
 * the netlist, which belongs to ACT. This is the seam.
 *
 * At a checkpoint Dali supplies a batch of already-decided delay-site pair
 * counts. The host translates those values into compatible ACT process names,
 * re-elaborates once, regenerates the layout, rebuilds PhyDB and the timer, and
 * reports what changed as plain names and connectivity. It never chooses a
 * site or count and never touches Dali's circuit; Dali validates and applies
 * the returned delta, so a failed host cannot half-mutate the placement.
 *************************************************************************/
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <act/passes.h>
#include <act/tech.h>

#include "all_cmds.h"
#include "flow.h"
#include "timing_driven_helpers.h"

#ifdef FOUND_dali
#ifdef FOUND_phydb

#include <dali/dali.h>

namespace {

using interact::timing_driven::FormatProcessName;
using interact::timing_driven::SplitEndpoint;

/**
 * The netlist as plain names: what a component is, and what a net joins.
 *
 * Taken from PhyDB rather than from ACT structures because PhyDB is built from
 * the DEF that ACT just generated, so it carries exactly the names ACT chose
 * while being far easier to walk. Diffing two of these is what produces the
 * delta.
 */
struct Inventory {
  /** component name -> macro name */
  std::map<std::string, std::string> components;
  /** net name -> ordered "component:pin" endpoints */
  std::map<std::string, std::vector<std::string>> nets;
};

Inventory CaptureInventory (phydb::PhyDB *phydb)
{
  Inventory inventory;
  auto &design = *(phydb->GetDesignPtr ());
  for (auto &component : design.GetComponentsRef ()) {
    const std::string macro =
        component.GetMacro () ? component.GetMacro ()->GetName () : "";
    inventory.components[component.GetName ()] = macro;
  }
  for (auto &net : design.GetNetsRef ()) {
    std::vector<std::string> pins;
    for (auto &phydb_pin : net.GetPinsRef ()) {
      phydb::Component &component =
          design.GetComponentsRef ()[phydb_pin.InstanceId ()];
      const std::string pin_name =
          component.GetMacro ()->GetPinsRef ()[phydb_pin.PinId ()].GetName ();
      pins.push_back (component.GetName () + ":" + pin_name);
    }
    inventory.nets[net.GetName ()] = pins;
  }
  return inventory;
}

/**
 * The difference between two inventories, as a delta Dali can validate.
 *
 * Reports additions and retirements only. A net whose *membership* changed
 * under a preserved name would be neither, and is therefore reported as an
 * error rather than silently dropped: Dali would apply a delta that did not
 * describe the netlist it was given.
 */
bool BuildDelta (const Inventory &before, const Inventory &after,
                 dali::TopologyDelta *delta, std::string *error)
{
  for (const auto &entry : after.components) {
    if (before.components.count (entry.first) == 0) {
      dali::TopologyDeltaComponent component;
      component.name = entry.first;
      component.macro_name = entry.second;
      // Dali replaces this with a position near the line the cell belongs to.
      component.seed_x = 0.0;
      component.seed_y = 0.0;
      delta->added_components.push_back (component);
    } else if (before.components.at (entry.first) != entry.second) {
      *error = "component '" + entry.first + "' changed master from '" +
               before.components.at (entry.first) + "' to '" + entry.second +
               "'";
      return false;
    }
  }
  for (const auto &entry : before.components) {
    if (after.components.count (entry.first) == 0) {
      *error = "component '" + entry.first +
               "' disappeared; a delay-site parameter increase must be purely "
               "additive";
      return false;
    }
  }

  for (const auto &entry : after.nets) {
    auto previous = before.nets.find (entry.first);
    if (previous == before.nets.end ()) {
      dali::TopologyDeltaNet net;
      net.name = entry.first;
      for (const std::string &endpoint : entry.second) {
        std::string component_name;
        std::string pin_name;
        if (!SplitEndpoint (endpoint, &component_name, &pin_name)) {
          *error = "cannot parse endpoint '" + endpoint + "'";
          return false;
        }
        net.pins.push_back ({component_name, pin_name});
      }
      delta->added_nets.push_back (net);
    } else if (previous->second != entry.second) {
      // A net of the enclosing design that the grown site now drives from a
      // different cell. Its name belongs to the design and does not change.
      dali::TopologyDeltaNet net;
      net.name = entry.first;
      for (const std::string &endpoint : entry.second) {
        std::string component_name;
        std::string pin_name;
        if (!SplitEndpoint (endpoint, &component_name, &pin_name)) {
          *error = "cannot parse endpoint '" + endpoint + "'";
          return false;
        }
        net.pins.push_back ({component_name, pin_name});
      }
      delta->rewired_nets.push_back (net);
    }
  }
  for (const auto &entry : before.nets) {
    if (after.nets.count (entry.first) == 0) {
      delta->retired_nets.push_back (entry.first);
    }
  }
  return true;
}

/**
 * Substitutes a decided pair count into a configured process template.
 *
 * The template must contain exactly one literal `{pairs}` and nothing else that
 * looks like a placeholder. Deliberately not printf: a format string is a small
 * language, and a host that could evaluate one could turn a count into
 * something other than that count. This can only put the integer where the
 * template says the integer goes.
 */
/**
 * Applies Dali-decided delay-site increases through ACT re-elaboration.
 *
 * Every call remains atomic and value-only. Repeated calls are permitted
 * because Dali may measure the legal result of one batch before deciding the
 * next; the host never chooses whether another call occurs.
 */
class ActTopologyChangeHost : public dali::TopologyCheckpointHost {
 public:
  void Configure (const std::string &process_template,
                  const std::string &tech_config, const std::string &liberty)
  {
    liberty_ = liberty;
    process_template_ = process_template;
    tech_config_ = tech_config;
    configured_ = true;
    applications_ = 0;
  }

  bool IsConfigured () const { return configured_; }

  /*
    Apply the whole batch through one ACT re-elaboration.

    Every request was decided together, from one measurement of one placement.
    Applying them one at a time would mean one re-elaboration each, and every
    request after the first would be landing on a netlist a previous one had
    already changed -- which is not what Dali measured. So the replacements are
    accumulated and handed to ACT once.

    Nothing here decides anything. Each decided pair count is formatted through
    the configured process template and passed on; no request is added, dropped,
    reordered, resized or substituted.
  */
  dali::TopologyMutationResult ApplyTopologyChange (
      const dali::TopologyCheckpointContext &context,
      const dali::TopologyChangeBatch &batch) override
  {
    if (!configured_) {
      return dali::TopologyMutationResult::Failed (
          "no process template is configured, so nothing can be applied");
    }
    if (!F.phydb) {
      return dali::TopologyMutationResult::Failed ("no PhyDB to change");
    }
    if (batch.requests.empty ()) {
      return dali::TopologyMutationResult::Failed (
          "an empty batch reached the host");
    }

    const Inventory before = CaptureInventory (F.phydb);
    std::vector<flow_delay_site_replacement> replacements;
    replacements.reserve (batch.requests.size ());

    for (const dali::TopologyChangeRequest &request : batch.requests) {
      if (request.site.empty () ||
          request.requested_pairs <= request.current_pairs) {
        return dali::TopologyMutationResult::Failed (
            "a request does not increase the pair count of a named site");
      }
      std::string process;
      std::string template_error;
      if (!FormatProcessName (process_template_, request.requested_pairs,
                              &process, &template_error)) {
        return dali::TopologyMutationResult::Failed (template_error);
      }
      // ACT must already be at the size Dali measured. If it is not, the
      // request was computed against a different netlist than the one about to
      // change.
      const std::string prefix = request.site + "_";
      size_t site_components = 0;
      for (const auto &entry : before.components) {
        if (entry.first.compare (0, prefix.size (), prefix) == 0) {
          ++site_components;
        }
      }
      if (site_components != static_cast<size_t> (2 * request.current_pairs)) {
        return dali::TopologyMutationResult::Failed (
            "site '" + request.site + "' has " +
            std::to_string (site_components) + " components, not the " +
            std::to_string (2 * request.current_pairs) +
            " implied by the requested current pair count");
      }
      printf ("CHECKPOINT_HOST applying %s -> %s (pairs %d -> %d)\n",
              request.site.c_str (), process.c_str (), request.current_pairs,
              request.requested_pairs);
      replacements.push_back ({request.site, process});
    }
    printf ("CHECKPOINT_HOST batch of %zu site(s) at iteration %d\n",
            replacements.size (), context.checkpoint_iteration);
    fflush (stdout);
    // Paths PhyDB was built from, so the regenerated files land where the flow
    // expects them and nothing has to be told twice.
    const std::string lef = F.phydb->tech ().GetLefName ();
    const std::string def = F.phydb->design ().GetDefName ();
    const std::string cell = CellFileFor (lef);
    const std::string identity_base = IdentityBaseFor (def);
    const std::string identity_suffix =
        applications_ == 0 ? std::string ()
                           : "_epoch_" + std::to_string (applications_);
    /*
      The die is carried across unchanged, in database units, which is what the
      layout pass emits directly as DIEAREA. Regenerating it from cell area
      instead -- the area_mult/aspect_ratio path the original flow used -- would
      grow the die by the cells just added, and Dali is mid-placement against
      the region it already has.
    */
    const phydb::Rect2D<int> die = F.phydb->GetDieArea ();

    if (!F.dali->WriteCurrentTimingConstraintIdentities (
            identity_base + identity_suffix +
            "_constraints_pre_checkpoint.json")) {
      return dali::TopologyMutationResult::Failed (
          "pre-checkpoint constraint identity capture failed");
    }

    if (!flow_apply_delay_site_map_keep_dali (replacements)) {
      return dali::TopologyMutationResult::Failed (
          "ACT re-elaboration of the batch failed");
    }
    if (!flow_rebuild_cell_and_netlist (cell)) {
      return dali::TopologyMutationResult::Failed ("cell/netlist rebuild failed");
    }
    if (!flow_generate_layout_files (lef, cell, def, die.ll.x, die.ll.y,
                                     die.ur.x, die.ur.y)) {
      return dali::TopologyMutationResult::Failed ("layout generation failed");
    }
    if (!flow_build_phydb (lef, cell, def, tech_config_)) {
      return dali::TopologyMutationResult::Failed ("PhyDB rebuild failed");
    }
    /*
      The timer was torn down with PhyDB, so it is stood back up in the same
      order the flow builds it the first time: the tagged timing-graph pass, the
      graph itself, then the cell library, which is what creates the timer
      object the PhyDB link attaches to.
    */
    if (!F.act_design->pass_find ("taggedTG")) {
      new ActDynamicPass (F.act_design, "taggedTG", "libacttpass.so", "tgraph");
    }
    if (!timer_build_graph_direct ()) {
      return dali::TopologyMutationResult::Failed ("timer graph rebuild failed");
    }
    if (!timer_initialize_liberty_path (liberty_)) {
      return dali::TopologyMutationResult::Failed (
          "timer initialization from " + liberty_ + " failed");
    }
    if (!timer_link_phydb_direct (F.phydb)) {
      return dali::TopologyMutationResult::Failed ("timer/PhyDB relink failed");
    }

    const Inventory after = CaptureInventory (F.phydb);
    {
      /*
        Counted per site as well as in total, because the DEF's COMPONENTS and
        NETS sections both carry names under the site prefix -- plain_delay's
        internal t[] array is nets, not cells -- and grepping a DEF conflates
        them.
      */
      for (const dali::TopologyChangeRequest &request : batch.requests) {
        const std::string prefix = request.site + "_";
        size_t before_site = 0, after_site = 0, before_site_nets = 0,
               after_site_nets = 0;
        for (const auto &e : before.components)
          if (e.first.compare (0, prefix.size (), prefix) == 0) ++before_site;
        for (const auto &e : after.components)
          if (e.first.compare (0, prefix.size (), prefix) == 0) ++after_site;
        for (const auto &e : before.nets)
          if (e.first.compare (0, prefix.size (), prefix) == 0)
            ++before_site_nets;
        for (const auto &e : after.nets)
          if (e.first.compare (0, prefix.size (), prefix) == 0)
            ++after_site_nets;
        printf ("CHECKPOINT_HOST inventory: components %zu -> %zu "
                "(%s %zu -> %zu), nets %zu -> %zu (%s %zu -> %zu)\n",
                before.components.size (), after.components.size (),
                prefix.c_str (), before_site, after_site,
                before.nets.size (), after.nets.size (),
                prefix.c_str (), before_site_nets, after_site_nets);
      }
      fflush (stdout);
    }
    dali::TopologyDelta delta;
    std::string error;
    if (!BuildDelta (before, after, &delta, &error)) {
      return dali::TopologyMutationResult::Failed ("topology delta: " + error);
    }

    // Rebind before returning: Dali is about to validate the delta against its
    // own circuit and then keep running against this database.
    F.dali->RebindPhyDB (F.phydb);
    if (!F.dali->WriteCurrentTimingConstraintEndpointIdentities (
            identity_base + identity_suffix +
            "_constraints_post_refresh.json")) {
      return dali::TopologyMutationResult::Failed (
          "post-refresh constraint identity capture failed");
    }

    printf ("CHECKPOINT_HOST delta: +%zu components, +%zu nets, %zu retired, "
            "%zu rewired\n", delta.added_components.size (),
            delta.added_nets.size (), delta.retired_nets.size (),
            delta.rewired_nets.size ());
    fflush (stdout);

    ++applications_;
    return dali::TopologyMutationResult::Applied (std::move (delta));
  }

 private:
  /** The cell file sits beside the LEF, sharing its base name. */
  static std::string CellFileFor (const std::string &lef_file)
  {
    const size_t dot = lef_file.rfind (".lef");
    if (dot == std::string::npos) return lef_file + ".cell";
    return lef_file.substr (0, dot) + ".cell";
  }

  static std::string IdentityBaseFor (const std::string &def_file)
  {
    const size_t dot = def_file.rfind (".def");
    if (dot == std::string::npos) return def_file;
    return def_file.substr (0, dot);
  }

  std::string process_template_;
  std::string tech_config_;
  std::string liberty_;
  bool configured_ = false;
  int applications_ = 0;
};

ActTopologyChangeHost act_topology_change_host;

}  // namespace

dali::TopologyCheckpointHost *interact_fixed_topology_host (void)
{
  return &act_topology_change_host;
}

bool interact_configure_fixed_topology_host (const std::string &process_template,
                                             const std::string &tech_config,
                                             const std::string &liberty)
{
  // Validated here as well as at use, so a malformed template is rejected when
  // the recipe is read rather than in the middle of a placement.
  std::string probe;
  std::string error;
  if (!FormatProcessName (process_template, 1, &probe, &error)) {
    fprintf (stderr, "%s\n", error.c_str ());
    return false;
  }
  act_topology_change_host.Configure (process_template, tech_config, liberty);
  return true;
}

#endif /* FOUND_phydb */
#endif /* FOUND_dali */
