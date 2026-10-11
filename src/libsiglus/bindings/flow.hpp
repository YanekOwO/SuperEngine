// -----------------------------------------------------------------------
// Internal helpers shared by Siglus flow-control bindings.
// -----------------------------------------------------------------------

#pragma once

#include <string>

namespace serilang {
struct Function;
}

namespace libsiglus::binding {
class Loader;
struct UserCommand;
const UserCommand& ResolveSceneUserCommand(Loader& loader,
                                           std::string scene_name,
                                           const std::string& command_name);

serilang::Function* ResolveSceneEntry(Loader& loader,
                                      std::string scene_name,
                                      int zlabel);

serilang::Function* ResolveSceneCommand(Loader& loader,
                                        std::string scene_name,
                                        const std::string& command_name);

}  // namespace libsiglus::binding
