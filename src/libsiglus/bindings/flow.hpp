// -----------------------------------------------------------------------
// Internal helpers shared by Siglus flow-control bindings.
// -----------------------------------------------------------------------

#pragma once

#include "vm/value_fwd.hpp"

#include <string>

namespace serilang {
class VM;
}

namespace libsiglus::binding {
class Loader;

serilang::Function* ResolveSceneEntry(Loader& loader,
                                      std::string scene_name,
                                      const std::string& entry_name);

}  // namespace libsiglus::binding
