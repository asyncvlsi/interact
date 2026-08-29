#include "dali_qt_gui_bridge.h"

#include <dali/dali.h>

#if defined(INTERACT_HAS_DALI_QT_GUI)
#include <dali/gui/qt_placement_snapshot_sink.h>

#include <memory>
#endif

bool InstallDaliQtGui(dali::Dali *dali, const std::string &pause_mode) {
  if (dali == nullptr) return false;

#if defined(INTERACT_HAS_DALI_QT_GUI)
  dali->SetGuiSnapshotSinkFactory(
      [] { return std::make_unique<dali::QtPlacementSnapshotSink>(); });
  if (!dali->ExecuteCommandLine("set gui_pause " + pause_mode) ||
      !dali->ExecuteCommandLine("set gui_debug true")) {
    return false;
  }
  return true;
#else
  (void)pause_mode;
  return false;
#endif
}
