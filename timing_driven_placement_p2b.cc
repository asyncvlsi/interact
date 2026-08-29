/*************************************************************************
 * Dali-owned timing-driven placement host adapter.
 *
 * This adapter stores only paths and value snapshots. All ACT, timer, PhyDB,
 * and Dali objects are borrowed from F during one host call and are destroyed
 * by the lifecycle boundary before any ACT replacement is applied.
 *************************************************************************/
#include "config_pkg.h"
#if defined(FOUND_dali) && defined(FOUND_phydb) && defined(FOUND_timing_actpin)
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <act/passes.h>
#include <phydb/phydb.h>

#include "all_cmds.h"
#include "flow.h"
#include "timing_driven_helpers.h"
#include <dali/timing/timing_driven_candidate_policy.h>
#include <dali/timing/timing_driven_placement_config.h>

namespace {

using Candidate = dali::TimingDrivenPlacementCandidate;
using Measurement = dali::TimingDrivenPlacementMeasurement;
using interact::timing_driven::FnvDigest;
using interact::timing_driven::JsonEscape;
using interact::timing_driven::SiteMember;

struct AnchorEntry {
  std::string status;
  std::string orient;
  int x = 0;
  int y = 0;
};

struct IoAnchorEntry {
  AnchorEntry placement;
  std::string layer;
  int llx = 0;
  int lly = 0;
  int urx = 0;
  int ury = 0;
};

using IoAnchor = std::map<std::string, IoAnchorEntry>;

bool GeneratedWellTap(const std::string &name) {
  return name.compare(0, 12, "__well_tap__") == 0;
}

bool ReadAnchor(const std::string &path, std::map<std::string, AnchorEntry> *out) {
  std::ifstream input(path);
  if (!input || !out) return false;
  std::string line;
  std::string current;
  std::map<std::string, AnchorEntry> parsed;
  bool in_components = false;
  const std::regex start(R"(^\s*-\s+(\S+)\s+(\S+).*)");
  const std::regex placement(
      R"(^\s*\+\s+(FIXED|PLACED|COVER|UNPLACED)\s+\(\s*(-?[0-9]+)\s+(-?[0-9]+)\s*\)\s+(N|S|E|W|FN|FS|FE|FW).*)");
  while (std::getline(input, line)) {
    if (line.find("COMPONENTS ") == 0) {
      in_components = true;
      continue;
    }
    if (in_components && line.find("END COMPONENTS") == 0) break;
    if (!in_components) continue;
    std::smatch match;
    if (std::regex_match(line, match, start)) {
      current = match[1].str();
      continue;
    }
    if (!current.empty() && std::regex_match(line, match, placement)) {
      parsed[current] = {match[1].str(), match[4].str(),
                         std::stoi(match[2].str()), std::stoi(match[3].str())};
      current.clear();
    }
  }
  if (parsed.empty()) return false;
  *out = std::move(parsed);
  return true;
}

bool ReadIoAnchor(const std::string &path, IoAnchor *out) {
  std::ifstream input(path);
  if (!input || !out) return false;
  std::string line;
  std::string current;
  IoAnchor parsed;
  bool in_pins = false;
  const std::regex start(R"(^\s*-\s+(\S+).*)");
  const std::regex placement(
      R"(^\s*\+\s+(FIXED|PLACED|COVER|UNPLACED)\s+\(\s*(-?[0-9]+)\s+(-?[0-9]+)\s*\)\s+(N|S|E|W|FN|FS|FE|FW).*)");
  const std::regex layer(
      R"(^\s*\+\s+LAYER\s+(\S+)\s+\(\s*(-?[0-9]+)\s+(-?[0-9]+)\s*\)\s+\(\s*(-?[0-9]+)\s+(-?[0-9]+)\s*\).*)");
  while (std::getline(input, line)) {
    if (line.find("PINS ") == 0) {
      in_pins = true;
      continue;
    }
    if (in_pins && line.find("END PINS") == 0) break;
    if (!in_pins) continue;
    std::smatch match;
    if (std::regex_match(line, match, start)) {
      current = match[1].str();
      continue;
    }
    if (!current.empty() && std::regex_match(line, match, placement)) {
      parsed[current].placement = {
          match[1].str(), match[4].str(), std::stoi(match[2].str()),
          std::stoi(match[3].str())};
      continue;
    }
    if (!current.empty() && std::regex_match(line, match, layer)) {
      parsed[current].layer = match[1].str();
      parsed[current].llx = std::stoi(match[2].str());
      parsed[current].lly = std::stoi(match[3].str());
      parsed[current].urx = std::stoi(match[4].str());
      parsed[current].ury = std::stoi(match[5].str());
    }
    if (!current.empty() && line.find(';') != std::string::npos) {
      current.clear();
    }
  }
  *out = std::move(parsed);
  return true;
}

std::vector<std::string> ComponentInventory(phydb::Design &design,
                                            const std::vector<std::string> &sites,
                                            bool static_only) {
  std::vector<std::string> result;
  for (phydb::Component &component : design.GetComponentsRef()) {
    if (static_only && GeneratedWellTap(component.GetName())) continue;
    bool delay = false;
    for (const std::string &site : sites) {
      if (SiteMember(component.GetName(), site)) {
        delay = true;
        break;
      }
    }
    if (static_only && delay) continue;
    result.push_back(component.GetName() + "|" +
                     (component.GetMacro() ? component.GetMacro()->GetName() : "") +
                     "|" + component.GetSourceStr());
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::string IoDigest(phydb::Design &design) {
  std::vector<std::string> values;
  for (phydb::IOPin &pin : design.GetIoPinsRef()) {
    const phydb::Point2D<int> location = pin.GetLocation();
    values.push_back(pin.GetName() + "|" +
                     phydb::PlaceStatusStr(pin.GetPlacementStatus()) +
                     "|" + std::to_string(location.x) + ":" + std::to_string(location.y));
  }
  std::sort(values.begin(), values.end());
  return FnvDigest(values);
}

std::string PlacementStatusDigest(phydb::Design &design) {
  std::vector<std::string> values;
  for (phydb::Component &component : design.GetComponentsRef()) {
    values.push_back(component.GetName() + "|" +
                     phydb::PlaceStatusStr(component.GetPlacementStatus()));
  }
  std::sort(values.begin(), values.end());
  return FnvDigest(values);
}

std::string ConstraintDigest(const Measurement &measurement) {
  std::vector<int> ids;
  ids.reserve(measurement.constraints.size());
  for (const auto &constraint : measurement.constraints)
    ids.push_back(constraint.constraint_id);
  std::sort(ids.begin(), ids.end());
  std::ostringstream digest;
  digest << "ids:";
  for (int id : ids) digest << id << ",";
  return digest.str();
}

bool NonEmptyFile(const std::filesystem::path &path) {
  std::error_code error;
  const bool exists = std::filesystem::exists(path, error);
  if (error || !exists) return false;
  const auto size = std::filesystem::file_size(path, error);
  return !error && size != 0;
}

bool WriteManifest(const std::filesystem::path &path,
                   const Measurement &measurement,
                   const std::vector<std::string> &components,
                   const std::vector<std::string> &static_components,
                   const std::vector<std::string> &io_names,
                   const std::string &placement_status_digest) {
  std::ofstream output(path);
  if (!output) return false;
  output << "artifact_id " << measurement.artifact_id << "\n"
         << "generation_id " << measurement.generation_id << "\n"
         << "artifact_parent_id " << measurement.artifact_parent_id << "\n"
         << "generation_parent_id " << measurement.generation_parent_id << "\n"
         << "topology_identity " << measurement.topology_identity << "\n"
         << "static_inventory_digest " << measurement.static_inventory_digest << "\n"
         << "io_inventory_digest " << measurement.io_inventory_digest << "\n"
         << "component_inventory_digest " << FnvDigest(components) << "\n"
         << "placement_status_digest " << placement_status_digest << "\n"
         << "constraint_id_digest " << measurement.constraint_id_digest << "\n"
         << "constraint_count " << measurement.constraint_count << "\n"
         << "timing_use_rc " << measurement.timing_use_rc << "\n"
         << "rc_min_routing_layer " << measurement.rc_min_routing_layer << "\n"
         << "die_llx " << measurement.die_grid.die_llx << "\n"
         << "die_lly " << measurement.die_grid.die_lly << "\n"
         << "die_urx " << measurement.die_grid.die_urx << "\n"
         << "die_ury " << measurement.die_grid.die_ury << "\n"
         << "grid_x " << measurement.die_grid.grid_x << "\n"
         << "grid_y " << measurement.die_grid.grid_y << "\n"
         << "period_ps " << std::setprecision(17) << measurement.period_ps << "\n"
         << "wns_ps " << measurement.wns_ps << "\n"
         << "tns_ps " << measurement.tns_ps << "\n"
         << "hpwl_um " << measurement.placement_hpwl_um << "\n"
         << "placement_legal " << measurement.placement_legal << "\n"
         << "overlap_count " << measurement.overlap_count << "\n"
         << "legality_total_violation_count "
         << measurement.legality.TotalViolationCount() << "\n"
         << "legality_is_legal " << measurement.legality.IsLegal() << "\n"
         << "delay_parameters " << measurement.delay_parameters.size() << "\n";
  for (const auto &parameter : measurement.delay_parameters) {
    output << "delay_parameter " << parameter.first << " "
           << parameter.second << "\n";
  }
  output << "replacement_processes "
         << measurement.replacement_processes.size() << "\n";
  for (const auto &replacement : measurement.replacement_processes) {
    output << "replacement_process " << replacement.first << " "
           << replacement.second << "\n";
  }
  output << "constraints " << measurement.constraints.size() << "\n";
  for (const auto &constraint : measurement.constraints) {
    output << "constraint " << constraint.constraint_id << " "
           << constraint.site_id << " " << constraint.slack_ps << "\n";
  }
  output << "components " << components.size() << "\n";
  for (const std::string &item : components) output << "component " << item << "\n";
  output << "static_components " << static_components.size() << "\n";
  for (const std::string &item : static_components) output << "static " << item << "\n";
  output << "io_pins " << io_names.size() << "\n";
  for (const std::string &item : io_names) output << "io " << item << "\n";
  return output.good();
}

class JsonLinesObserver final : public dali::TimingDrivenPlacementEventObserver {
 public:
  explicit JsonLinesObserver(const std::filesystem::path &path) : output_(path) {}
  void OnTimingDrivenPlacementEvent(
      const dali::TimingDrivenPlacementEvent &event) override {
    if (!output_) return;
    output_ << std::setprecision(17)
            << "{\"type\":" << static_cast<int>(event.type)
            << ",\"trial_id\":" << event.trial_id
            << ",\"operation_succeeded\":"
            << (event.operation_succeeded ? "true" : "false")
            << ",\"decision\":" << static_cast<int>(event.decision)
            << ",\"reason\":\"" << JsonEscape(event.reason) << "\"";
    if (event.measurement) {
      const Measurement &measurement = *event.measurement;
      output_ << ",\"artifact_id\":\"" << JsonEscape(measurement.artifact_id)
              << "\",\"generation_id\":\""
              << JsonEscape(measurement.generation_id)
              << "\",\"artifact_parent_id\":\""
              << JsonEscape(measurement.artifact_parent_id)
              << "\",\"generation_parent_id\":\""
              << JsonEscape(measurement.generation_parent_id)
              << "\",\"period_ps\":" << measurement.period_ps
              << ",\"wns_ps\":" << measurement.wns_ps
              << ",\"tns_ps\":" << measurement.tns_ps
              << ",\"constraint_count\":" << measurement.constraint_count
              << ",\"constraint_id_digest\":\""
              << JsonEscape(measurement.constraint_id_digest)
              << "\",\"hpwl_um\":" << measurement.placement_hpwl_um
              << ",\"placement_legal\":"
              << (measurement.placement_legal ? "true" : "false")
              << ",\"overlap_count\":" << measurement.overlap_count
              << ",\"static_inventory_digest\":\""
              << JsonEscape(measurement.static_inventory_digest)
              << "\",\"io_inventory_digest\":\""
              << JsonEscape(measurement.io_inventory_digest) << "\""
              << ",\"constraints\":[";
      for (std::size_t index = 0; index < measurement.constraints.size();
           ++index) {
        if (index != 0) output_ << ",";
        const auto &constraint = measurement.constraints[index];
        output_ << "{\"id\":" << constraint.constraint_id
                << ",\"site\":\"" << JsonEscape(constraint.site_id)
                << "\",\"slack_ps\":" << constraint.slack_ps << "}";
      }
      output_ << "]";
    }
    output_ << "}\n";
    output_.flush();
  }

 private:
  std::ofstream output_;
};

class P2BHost final : public dali::TimingDrivenFlowHost {
 public:
#ifdef DALI_P2B_TEST_HARNESS
  struct TestHooks {
    std::function<bool(const Candidate &, const std::string &)> begin;
    std::function<void(const Measurement &)> before_commit;
    std::function<void(const Measurement &)> after_commit;
  };
#endif

  P2BHost(const dali::TimingDrivenPlacementConfig &config,
          std::filesystem::path output_dir)
      : config_(config),
        output_dir_(std::move(output_dir)) {
    committed_candidate_ = config_.policy.controller.initial_candidate;
    committed_generation_ = config_.policy.controller.initial_generation_id;
    std::filesystem::create_directories(output_dir_ / "artifacts");
  }

#ifdef DALI_P2B_TEST_HARNESS
  P2BHost(const dali::TimingDrivenPlacementConfig &config,
          std::filesystem::path output_dir, TestHooks hooks)
      : P2BHost(config, std::move(output_dir)) {
    test_hooks_ = std::move(hooks);
  }
#endif

  bool BeginTrial(const Candidate &candidate,
                  const std::string &committed_anchor) override {
    host_calls_.push_back("BeginTrial parent_artifact=" +
                          (committed_artifact_.empty() ? committed_anchor
                                                       : committed_artifact_));
    current_measurement_.reset();
    current_anchor_tmp_.clear();
    current_manifest_tmp_.clear();
    current_act_tmp_.clear();
#ifdef DALI_P2B_TEST_HARNESS
    if (test_hooks_.begin &&
        !test_hooks_.begin(candidate, committed_artifact_)) {
      return false;
    }
#endif
    trial_active_ = true;
    current_candidate_ = candidate;
    current_parent_artifact_ = committed_artifact_.empty()
                                   ? committed_anchor
                                   : committed_artifact_;
    current_parent_generation_ = committed_artifact_.empty()
                                     ? config_.policy.controller.initial_generation_id
                                     : committed_generation_;
    if (committed_artifact_.empty()) {
      const std::filesystem::path seed_cell =
          output_dir_ / "seed-cell.act";
      if (!flow_rebuild_cell_and_netlist(seed_cell.string())) {
        fprintf(stderr, "p2b: initial ACT netlist build failed\n");
        return false;
      }
    }
    if (!ApplyCandidate(candidate)) {
      fprintf(stderr, "p2b: ACT map application failed\n");
      return false;
    }
    if (!RebuildDerivedState(committed_anchor, false)) {
      fprintf(stderr, "p2b: derived-state rebuild failed\n");
      return false;
    }
    return true;
  }

  std::optional<Measurement> RunPlacementAndTiming() override {
    host_calls_.push_back("RunPlacementAndTiming");
    if (!trial_active_ || !F.dali) return std::nullopt;
    if (!F.dali->RunCommandFile(config_.lifecycle.placement_recipe_path)) {
      fprintf(stderr, "p2b: Dali placement recipe failed\n");
      return std::nullopt;
    }
    if (!committed_artifact_.empty() &&
        !F.dali->ExecuteCommandLine("set disable_io_place true")) {
      fprintf(stderr, "p2b: failed to preserve committed I/O placement\n");
      return std::nullopt;
    }
    if (!F.dali->StartPlacement(config_.lifecycle.target_density,
                                config_.lifecycle.num_threads)) {
      fprintf(stderr, "p2b: typed Dali placement failed\n");
      return std::nullopt;
    }
    const auto options = F.dali->GetRuntimeOptions();
    if (options.timing_use_rc != config_.lifecycle.timing_use_rc ||
        options.rc_min_routing_layer != config_.lifecycle.rc_min_routing_layer ||
        options.is_standard_cell != config_.lifecycle.is_standard_cell ||
        options.well_emit_mode != config_.lifecycle.well_emit_mode ||
        options.disable_welltap != !config_.lifecycle.enable_well_taps) {
      fprintf(stderr, "p2b: Dali recipe/runtime metadata mismatch rc=%d/%d layer=%d/%d standard=%d/%d well=%d/%d\n",
              options.timing_use_rc, config_.lifecycle.timing_use_rc,
              options.rc_min_routing_layer, config_.lifecycle.rc_min_routing_layer,
              options.is_standard_cell, config_.lifecycle.is_standard_cell,
              options.well_emit_mode, config_.lifecycle.well_emit_mode);
      fprintf(stderr, "p2b: well-tap runtime mismatch disable=%d expected=%d\n",
              options.disable_welltap,
              !config_.lifecycle.enable_well_taps);
      return std::nullopt;
    }
    F.dali->ExportToPhyDB(dali::PhyDBExportMode::kPlacementAnchor);
    const dali::GriddedPlacementLegalityReport report =
        F.dali->ValidatePlacementLegality();
    Measurement measurement;
    measurement.delay_parameters = current_candidate_.delay_parameters;
    measurement.replacement_processes = current_candidate_.replacement_processes;
    const int measurement_id = ++measurement_number_;
    measurement.artifact_id = "p2b-trial-" + std::to_string(measurement_id);
    measurement.generation_id = "p2b-generation-" +
                                 std::to_string(measurement_id - 1);
    measurement.artifact_parent_id = current_parent_artifact_;
    measurement.generation_parent_id = current_parent_generation_;
    measurement.placement_legal = report.IsLegal();
    measurement.legality = dali::ToTimingDrivenPlacementLegalitySummary(report);
    measurement.overlap_count = static_cast<int>(report.component_overlap_count);
    measurement.placement_hpwl_um = F.dali->GetCircuit().UnweightedHPWL();
    measurement.die_grid = config_.lifecycle.fixed_die_grid;
    measurement.timing_use_rc = config_.lifecycle.timing_use_rc;
    measurement.rc_min_routing_layer = config_.lifecycle.rc_min_routing_layer;
    if (!F.dali->ReportTiming()) {
      fprintf(stderr, "p2b: Dali RC timing refresh failed\n");
      return std::nullopt;
    }
    if (!timer_run_and_capture(&measurement)) {
      fprintf(stderr, "p2b: RC timing bridge capture failed\n");
      return std::nullopt;
    }

    phydb::Design *design = F.phydb ? F.phydb->GetDesignPtr() : nullptr;
    if (!design) {
      fprintf(stderr, "p2b: missing PhyDB design after placement\n");
      return std::nullopt;
    }
    const std::vector<std::string> all = ComponentInventory(
        *design, config_.policy.delay_site_ids, false);
    const std::vector<std::string> static_components = ComponentInventory(
        *design, config_.policy.delay_site_ids, true);
    std::vector<std::string> io_names;
    for (phydb::IOPin &pin : design->GetIoPinsRef()) io_names.push_back(pin.GetName());
    std::sort(io_names.begin(), io_names.end());
    measurement.static_inventory_digest = FnvDigest(static_components);
    measurement.io_inventory_digest = IoDigest(*design);
    const std::string placement_status_digest = PlacementStatusDigest(*design);
    measurement.constraint_id_digest = ConstraintDigest(measurement);
    std::vector<std::string> topology = all;
    for (const auto &replacement : current_candidate_.replacement_processes)
      topology.push_back(replacement.first + "=" + replacement.second);
    measurement.topology_identity = FnvDigest(topology);
    current_measurement_ = measurement;
    current_anchor_tmp_ = output_dir_ / "artifacts" /
                          (measurement.artifact_id + ".def.tmp");
    current_manifest_tmp_ = output_dir_ / "artifacts" /
                            (measurement.artifact_id + ".manifest.tmp");
    current_act_tmp_ = output_dir_ / "artifacts" /
                       (measurement.artifact_id + ".act.tmp");
    F.phydb->WriteDef(current_anchor_tmp_.string());
    if (!NonEmptyFile(current_anchor_tmp_)) {
      fprintf(stderr, "p2b: PhyDB did not produce candidate DEF\n");
      return std::nullopt;
    }
    FILE *act_file = fopen(current_act_tmp_.string().c_str(), "w");
    if (!act_file) {
      fprintf(stderr, "p2b: candidate ACT artifact could not be opened\n");
      return std::nullopt;
    }
    F.act_design->Print(act_file);
    fclose(act_file);
    if (!WriteManifest(current_manifest_tmp_, measurement, all, static_components,
                       io_names, placement_status_digest)) {
      fprintf(stderr, "p2b: manifest write failed\n");
      return std::nullopt;
    }
    return measurement;
  }

  bool CommitTrial() override {
    host_calls_.push_back("CommitTrial");
    if (!trial_active_ || !current_measurement_) return false;
    const std::filesystem::path anchor =
        output_dir_ / "artifacts" / (current_measurement_->artifact_id + ".def");
    const std::filesystem::path manifest =
        output_dir_ / "artifacts" /
        (current_measurement_->artifact_id + ".manifest");
    const std::filesystem::path act =
        output_dir_ / "artifacts" / (current_measurement_->artifact_id + ".act");
#ifdef DALI_P2B_TEST_HARNESS
    if (test_hooks_.before_commit) {
      test_hooks_.before_commit(*current_measurement_);
    }
#endif
    const std::vector<std::filesystem::path> destinations = {
        anchor, manifest, act};
    const std::vector<std::filesystem::path> sources = {
        current_anchor_tmp_, current_manifest_tmp_, current_act_tmp_};
    std::vector<std::filesystem::path> promoted;
    std::error_code error;
    for (std::size_t index = 0; index < sources.size(); ++index) {
      error.clear();
      std::filesystem::rename(sources[index], destinations[index], error);
      if (error) {
        for (const std::filesystem::path &path : promoted) {
          std::error_code ignored;
          std::filesystem::remove(path, ignored);
        }
        return false;
      }
      promoted.push_back(destinations[index]);
    }
    const Measurement committed_measurement = *current_measurement_;
    committed_candidate_ = current_candidate_;
    committed_anchor_ = anchor;
    committed_artifact_ = committed_measurement.artifact_id;
    committed_generation_ = committed_measurement.generation_id;
    committed_measurement_ = committed_measurement;
    trial_active_ = false;
#ifdef DALI_P2B_TEST_HARNESS
    if (test_hooks_.after_commit) {
      test_hooks_.after_commit(committed_measurement);
    }
#endif
    ClearCurrentTrialScratch();
    return true;
  }

  bool RollbackTrial() override {
    host_calls_.push_back("RollbackTrial anchor=" + committed_artifact_);
    std::error_code ignored;
    if (current_measurement_ && !current_anchor_tmp_.empty() &&
        std::filesystem::exists(current_anchor_tmp_)) {
      std::filesystem::rename(
          current_anchor_tmp_,
          output_dir_ / "artifacts" /
              (current_measurement_->artifact_id + ".rejected.def"),
          ignored);
      if (ignored) return false;
    }
    if (current_measurement_ && !current_manifest_tmp_.empty() &&
        std::filesystem::exists(current_manifest_tmp_)) {
      std::filesystem::rename(
          current_manifest_tmp_,
          output_dir_ / "artifacts" /
              (current_measurement_->artifact_id + ".rejected.manifest"),
          ignored);
      if (ignored) return false;
    }
    if (current_measurement_ && !current_act_tmp_.empty() &&
        std::filesystem::exists(current_act_tmp_)) {
      std::filesystem::rename(
          current_act_tmp_,
          output_dir_ / "artifacts" /
              (current_measurement_->artifact_id + ".rejected.act"),
          ignored);
      if (ignored) return false;
    }
    if (!current_measurement_) {
      if (!current_anchor_tmp_.empty())
        std::filesystem::remove(current_anchor_tmp_, ignored);
      if (!current_manifest_tmp_.empty())
        std::filesystem::remove(current_manifest_tmp_, ignored);
      if (!current_act_tmp_.empty())
        std::filesystem::remove(current_act_tmp_, ignored);
    }
    if (committed_artifact_.empty()) {
      trial_active_ = false;
      return true;
    }
    current_candidate_ = committed_candidate_;
    if (!ApplyCandidate(committed_candidate_)) {
      fprintf(stderr, "p2b: committed ACT map restoration failed\n");
      trial_active_ = true;
      return false;
    }
    bool restored = RebuildDerivedState(committed_artifact_, true);
    if (restored && F.phydb) {
      const std::filesystem::path restored_path =
          output_dir_ / ("restored-" + committed_artifact_ + ".def");
      F.phydb->WriteDef(restored_path.string());
      restored = NonEmptyFile(restored_path);
      if (restored) {
        std::error_code copy_error;
        std::filesystem::copy_file(
            committed_anchor_, restored_path,
            std::filesystem::copy_options::overwrite_existing, copy_error);
        restored = !copy_error && NonEmptyFile(restored_path);
      }
    }
    trial_active_ = !restored;
    if (restored) ClearCurrentTrialScratch();
    return restored;
  }

  const std::filesystem::path &committed_anchor() const { return committed_anchor_; }

  void WriteHostCalls(const std::filesystem::path &path) const {
    std::ofstream output(path);
    for (const std::string &call : host_calls_) output << call << "\n";
  }

 private:
  void ClearCurrentTrialScratch() {
    current_measurement_.reset();
    current_anchor_tmp_.clear();
    current_manifest_tmp_.clear();
    current_act_tmp_.clear();
  }

  bool ApplyCandidate(const Candidate &candidate) {
    std::vector<flow_delay_site_replacement> replacements;
    for (const auto &site : candidate.replacement_processes) {
      if (candidate.delay_parameters.find(site.first) ==
          candidate.delay_parameters.end()) {
        fprintf(stderr, "p2b: missing parameter for %s\n", site.first.c_str());
        return false;
      }
      replacements.push_back({site.first, site.second});
    }
    if (replacements.size() != config_.policy.delay_site_ids.size()) {
      fprintf(stderr, "p2b: replacement map has %zu sites\n", replacements.size());
      return false;
    }
    return flow_apply_delay_site_map(replacements);
  }

  bool RestoreAnchor(const std::string &anchor, bool preserve_delay_components) {
    if (anchor.empty() || anchor == config_.policy.controller.initial_anchor) return true;
    std::filesystem::path anchor_path(anchor);
    if (!std::filesystem::exists(anchor_path))
      anchor_path = output_dir_ / "artifacts" / (anchor + ".def");
    std::map<std::string, AnchorEntry> entries;
    if (!ReadAnchor(anchor_path.string(), &entries)) {
      fprintf(stderr, "p2b: cannot parse anchor %s\n", anchor_path.string().c_str());
      return false;
    }
    if (!F.phydb || !F.phydb->GetDesignPtr()) {
      fprintf(stderr, "p2b: no PhyDB while restoring %s\n", anchor.c_str());
      return false;
    }
    phydb::Design *design = F.phydb->GetDesignPtr();
    for (const auto &entry : entries) {
      phydb::Component *component = design->GetComponentPtr(entry.first);
      bool delay = false;
      for (const std::string &site : config_.policy.delay_site_ids)
        delay = delay || SiteMember(entry.first, site);
      if (!component && !delay && !GeneratedWellTap(entry.first)) {
        fprintf(stderr, "p2b: static component missing from current design: %s\n",
                entry.first.c_str());
        return false;
      }
    }
    for (phydb::Component &component : design->GetComponentsRef()) {
      auto entry = entries.find(component.GetName());
      bool delay = false;
      for (const std::string &site : config_.policy.delay_site_ids)
        delay = delay || SiteMember(component.GetName(), site);
      if (entry == entries.end()) {
        if (!delay && !GeneratedWellTap(component.GetName())) {
          fprintf(stderr, "p2b: current static component absent from anchor: %s\n",
                  component.GetName().c_str());
          return false;
        }
        component.SetPlacementStatus(phydb::PlaceStatus::UNPLACED);
        continue;
      }
      if (delay && !preserve_delay_components) {
        component.SetPlacementStatus(phydb::PlaceStatus::UNPLACED);
        continue;
      }
      component.SetLocation(entry->second.x, entry->second.y);
      component.SetOrientation(phydb::StrToCompOrient(entry->second.orient));
      component.SetPlacementStatus(phydb::StrToPlaceStatus(entry->second.status));
    }
    IoAnchor io_entries;
    if (!ReadIoAnchor(anchor_path.string(), &io_entries)) return false;
    for (const auto &entry : io_entries) {
      phydb::IOPin *pin = design->GetIoPinPtr(entry.first);
      if (!pin) return false;
      pin->SetPlacement(phydb::StrToPlaceStatus(entry.second.placement.status),
                        entry.second.placement.x, entry.second.placement.y,
                        phydb::StrToCompOrient(entry.second.placement.orient));
      if (!entry.second.layer.empty()) {
        pin->SetShape(entry.second.layer, entry.second.llx, entry.second.lly,
                      entry.second.urx, entry.second.ury);
      }
    }
    return true;
  }

  bool RebuildDerivedState(const std::string &anchor,
                          bool preserve_delay_components) {
    flow_reset_derived_state_direct ();
    ++trial_number_;
    const std::filesystem::path base = output_dir_ / "work" /
                                       ("trial-" + std::to_string(trial_number_));
    std::filesystem::create_directories(base);
    const std::string cell = (base / "design.cell").string();
    const std::string lef = (base / "design.lef").string();
    const std::string def = (base / "design.def").string();
    if (!flow_rebuild_cell_and_netlist(cell)) {
      fprintf(stderr, "p2b: cell/netlist rebuild failed\n");
      return false;
    }
    const auto &die = config_.lifecycle.fixed_die_grid;
    if (!flow_generate_layout_files(lef, cell, def,
                                    die.die_llx, die.die_lly,
                                    die.die_urx, die.die_ury)) {
      fprintf(stderr, "p2b: layout generation failed\n");
      return false;
    }
    std::filesystem::path input_def = def;
    if (preserve_delay_components && !anchor.empty() &&
        anchor != config_.policy.controller.initial_anchor) {
      input_def = std::filesystem::path(anchor);
      if (!std::filesystem::exists(input_def))
        input_def = output_dir_ / "artifacts" / (anchor + ".def");
    }
    if (!std::filesystem::exists(input_def)) {
      fprintf(stderr, "p2b: committed anchor DEF is missing: %s\n",
              input_def.string().c_str());
      return false;
    }
    if (!flow_build_phydb(lef, cell, input_def.string(),
                          config_.lifecycle.tech_config_path)) {
      fprintf(stderr, "p2b: PhyDB build failed\n");
      return false;
    }
    const int units = F.phydb->GetDesignPtr()->GetUnitsDistanceMicrons();
    F.phydb->SetPlacementGrids(die.grid_x / units, die.grid_y / units);
    if (!RestoreAnchor(anchor, preserve_delay_components)) {
      fprintf(stderr, "p2b: anchor restore failed: %s\n", anchor.c_str());
      return false;
    }
    if (!F.act_design->pass_find("taggedTG"))
      new ActDynamicPass(F.act_design, "taggedTG", "libacttpass.so", "tgraph");
    if (!timer_build_graph_direct()) {
      fprintf(stderr, "p2b: timing graph build failed\n");
      return false;
    }
    if (!timer_initialize_liberty_path(config_.lifecycle.liberty_path)) {
      fprintf(stderr, "p2b: timer init failed\n");
      return false;
    }
#if defined(DALI_P2B_TEST_HARNESS)
    timer_test_clobber_library_stack ();
#endif
    if (!timer_link_phydb_direct(F.phydb)) {
      fprintf(stderr, "p2b: timer/PhyDB link failed\n");
      return false;
    }
    F.dali = new dali::Dali(F.phydb, "0", "");
    return true;
  }

  dali::TimingDrivenPlacementConfig config_;
  std::filesystem::path output_dir_;
  Candidate committed_candidate_;
  Candidate current_candidate_;
  std::optional<Measurement> committed_measurement_;
  std::optional<Measurement> current_measurement_;
  std::filesystem::path committed_anchor_;
  std::filesystem::path current_anchor_tmp_;
  std::filesystem::path current_manifest_tmp_;
  std::filesystem::path current_act_tmp_;
  std::string committed_artifact_;
  std::string committed_generation_;
  std::string current_parent_artifact_;
  std::string current_parent_generation_;
#ifdef DALI_P2B_TEST_HARNESS
  TestHooks test_hooks_;
#endif
  int trial_number_ = 0;
  int measurement_number_ = 0;
  bool trial_active_ = false;
  std::vector<std::string> host_calls_;
};

void WriteP2BResult(const std::filesystem::path &path,
                   const dali::TimingDrivenPlacementResult &result) {
  std::ofstream summary(path);
  summary << "terminal_status " << static_cast<int>(result.terminal_status)
          << "\n"
          << "termination_reason "
          << static_cast<int>(result.termination_reason) << "\n"
          << "convergence_reason "
          << static_cast<int>(result.convergence_reason) << "\n"
          << "trials_attempted " << result.trials_attempted << "\n"
          << "history_size " << result.history.size() << "\n";
}

std::string ReadBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream bytes;
  bytes << input.rdbuf();
  return bytes.str();
}

}  // namespace

int run_timing_driven_placement_p2b (const char *config_path,
                                     const char *output_path)
{
  dali::TimingDrivenPlacementConfig config;
  std::string error;
  if (!dali::LoadTimingDrivenPlacementConfig(config_path, &config, &error)) {
    fprintf(stderr, "p2b config: %s\n", error.c_str());
    return 0;
  }
  P2BHost host(config, output_path);
  auto policy = std::make_unique<dali::DeterministicTimingDrivenCandidatePolicy>(
      config.policy.candidate_sequence);
  dali::TimingDrivenPlacementController controller(
      config.policy.controller, std::move(policy));
  JsonLinesObserver observer(std::filesystem::path(output_path) / "events.jsonl");
  const dali::TimingDrivenPlacementResult result = controller.Run(host, &observer);
  host.WriteHostCalls(std::filesystem::path(output_path) / "host_calls.txt");
  std::ofstream summary(std::filesystem::path(output_path) / "result.txt");
  summary << "terminal_status " << static_cast<int>(result.terminal_status) << "\n"
          << "termination_reason " << static_cast<int>(result.termination_reason) << "\n"
          << "convergence_reason " << static_cast<int>(result.convergence_reason) << "\n"
          << "trials_attempted " << result.trials_attempted << "\n"
          << "history_size " << result.history.size() << "\n";
  std::printf("P2B_RESULT status=%d reason=%d convergence=%d trials=%d history=%zu\n",
              static_cast<int>(result.terminal_status),
              static_cast<int>(result.termination_reason),
              static_cast<int>(result.convergence_reason), result.trials_attempted,
              result.history.size());
  return result.terminal_status == dali::TimingDrivenTerminalStatus::kConverged;
}

#ifdef DALI_P2B_TEST_HARNESS
int run_timing_driven_placement_p2b_rollback_failure_test(
    const char *config_path, const char *output_path) {
  dali::TimingDrivenPlacementConfig config;
  std::string error;
  if (!dali::LoadTimingDrivenPlacementConfig(config_path, &config, &error)) {
    fprintf(stderr, "p2b test config: %s\n", error.c_str());
    return 0;
  }
  const std::filesystem::path output(output_path);
  P2BHost::TestHooks hooks;
  hooks.after_commit = [output](const Measurement &measurement) {
      if (measurement.artifact_id != "p2b-trial-2") return;
      std::error_code error_code;
      std::filesystem::remove(
          output / "artifacts" / (measurement.artifact_id + ".def"),
          error_code);
    };
  P2BHost host(config, output, std::move(hooks));
  auto policy = std::make_unique<dali::DeterministicTimingDrivenCandidatePolicy>(
      config.policy.candidate_sequence);
  dali::TimingDrivenPlacementController controller(
      config.policy.controller, std::move(policy));
  JsonLinesObserver observer(output / "events.jsonl");
  const dali::TimingDrivenPlacementResult result = controller.Run(host, &observer);
  host.WriteHostCalls(output / "host_calls.txt");
  WriteP2BResult(output / "result.txt", result);
  return result.termination_reason ==
         dali::TimingDrivenTerminationReason::kRollbackFailure;
}

int run_timing_driven_placement_p2b_begin_failure_test(
    const char *config_path, const char *output_path) {
  dali::TimingDrivenPlacementConfig config;
  std::string error;
  if (!dali::LoadTimingDrivenPlacementConfig(config_path, &config, &error)) {
    fprintf(stderr, "p2b test config: %s\n", error.c_str());
    return 0;
  }
  const std::filesystem::path output(output_path);
  std::string committed_before;
  P2BHost::TestHooks hooks;
  hooks.begin = [output, &committed_before](
                    const Candidate &, const std::string &committed_artifact) {
    if (committed_artifact == "p2b-trial-2") {
      committed_before = ReadBytes(
          output / "artifacts" / "p2b-trial-2.def");
      return false;
    }
    return committed_artifact != "p2b-trial-2";
  };
  P2BHost host(config, output, std::move(hooks));
  auto policy = std::make_unique<dali::DeterministicTimingDrivenCandidatePolicy>(
      config.policy.candidate_sequence);
  dali::TimingDrivenPlacementController controller(
      config.policy.controller, std::move(policy));
  JsonLinesObserver observer(output / "events.jsonl");
  const dali::TimingDrivenPlacementResult result = controller.Run(host, &observer);
  host.WriteHostCalls(output / "host_calls.txt");
  WriteP2BResult(output / "result.txt", result);
  const std::filesystem::path committed_def =
      output / "artifacts" / "p2b-trial-2.def";
  const std::filesystem::path restored_def =
      output / "restored-p2b-trial-2.def";
  const bool committed_anchor_preserved =
      !committed_before.empty() &&
      ReadBytes(committed_def) == committed_before &&
      ReadBytes(restored_def) == committed_before;
  std::ofstream evidence(output / "begin_failure_evidence.txt");
  evidence << "committed_anchor_byte_identical "
           << (committed_anchor_preserved ? 1 : 0) << "\n";
  return result.termination_reason ==
             dali::TimingDrivenTerminationReason::kHostFailure &&
         committed_anchor_preserved;
}

int run_timing_driven_placement_p2b_commit_failure_test(
    const char *config_path, const char *output_path) {
  dali::TimingDrivenPlacementConfig config;
  std::string error;
  if (!dali::LoadTimingDrivenPlacementConfig(config_path, &config, &error)) {
    fprintf(stderr, "p2b test config: %s\n", error.c_str());
    return 0;
  }
  const std::filesystem::path output(output_path);
  P2BHost::TestHooks hooks;
  hooks.before_commit = [output](const Measurement &measurement) {
    if (measurement.artifact_id != "p2b-trial-2") return;
    std::error_code error_code;
    std::filesystem::remove(
        output / "artifacts" / (measurement.artifact_id + ".manifest.tmp"),
        error_code);
  };
  P2BHost host(config, output, std::move(hooks));
  auto policy = std::make_unique<dali::DeterministicTimingDrivenCandidatePolicy>(
      config.policy.candidate_sequence);
  dali::TimingDrivenPlacementController controller(
      config.policy.controller, std::move(policy));
  JsonLinesObserver observer(output / "events.jsonl");
  const dali::TimingDrivenPlacementResult result = controller.Run(host, &observer);
  host.WriteHostCalls(output / "host_calls.txt");
  WriteP2BResult(output / "result.txt", result);
  const std::filesystem::path artifact_dir = output / "artifacts";
  const bool partial_promotion_clean =
      std::filesystem::exists(artifact_dir / "p2b-trial-1.def") &&
      !std::filesystem::exists(artifact_dir / "p2b-trial-2.def") &&
      !std::filesystem::exists(artifact_dir / "p2b-trial-2.manifest") &&
      !std::filesystem::exists(artifact_dir / "p2b-trial-2.act") &&
      std::filesystem::exists(artifact_dir / "p2b-trial-2.rejected.act");
  std::ofstream evidence(output / "commit_failure_evidence.txt");
  evidence << "partial_promotion_clean "
           << (partial_promotion_clean ? 1 : 0) << "\n";
  return result.termination_reason ==
             dali::TimingDrivenTerminationReason::kCommitFailure &&
         partial_promotion_clean;
}
#endif

#endif
