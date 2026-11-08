// -----------------------------------------------------------------------
// Internal helpers shared by Siglus flow-control bindings.
// -----------------------------------------------------------------------

#pragma once

#include "core/script_call.hpp"
#include "vm/value.hpp"

#include <string>

namespace serilang {
struct Function;
}

namespace libsiglus::binding {
class Loader;

serilang::Value ResolveScriptCall(Loader& loader, const ScriptCall& call);
struct UserCommand;
const UserCommand& ResolveSceneUserCommand(Loader& loader,
                                           std::string scene_name,
                                           const std::string& command_name);

serilang::Value ResolveSceneEntry(Loader& loader,
                                  std::string scene_name,
                                  int zlabel);

serilang::Value ResolveSceneCommand(Loader& loader,
                                    std::string scene_name,
                                    const std::string& command_name);

}  // namespace libsiglus::binding
