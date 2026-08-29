#ifndef ACTFLOW_INTERACT_DALI_QT_GUI_BRIDGE_H_
#define ACTFLOW_INTERACT_DALI_QT_GUI_BRIDGE_H_

#include <string>

namespace dali {
class Dali;
}

/** Install Dali's optional Qt snapshot sink for an interact-owned session. */
bool InstallDaliQtGui(dali::Dali *dali, const std::string &pause_mode);

#endif
