// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include "libsiglus/bindings/registry.hpp"

#include "srbind/srbind.hpp"
#include "systems/sound_system.hpp"
#include "systems/text_system.hpp"
#include "vm/object.hpp"

#include <algorithm>
#include <string>

namespace libsiglus::binding {

// The script menus edit user preferences, independently of script overrides.
void BindMenuConfig(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  auto* mod = vm.builtins_->at("syscom").Get<serilang::Module*>();
  srbind::module_ m(vm.gc_.get(), mod->globals.get());
  auto* config = &runtime.host().global_config;
  const auto defaults = runtime.host().gameexe
                            ? MakeSiglusGlobalConfig(*runtime.host().gameexe)
                            : GlobalConfig{};
  const auto integer = [&](const std::string& name, int GlobalConfig::* field) {
    m.def("get_" + name, [config, field] { return config->*field; });
    m.def("set_" + name,
          [config, field](int value) { config->*field = std::max(0, value); });
    m.def("set_" + name + "_default",
          [config, field, value = defaults.*field] { config->*field = value; });
  };
  const auto boolean = [&](const std::string& name,
                           bool GlobalConfig::* field) {
    m.def("get_" + name, [config, field] { return config->*field ? 1 : 0; });
    m.def("set_" + name,
          [config, field](int value) { config->*field = value != 0; });
    m.def("set_" + name + "_default",
          [config, field, value = defaults.*field] { config->*field = value; });
  };
  integer("auto_mode_moji_wait", &GlobalConfig::auto_mode_moji_wait);
  integer("auto_mode_min_wait", &GlobalConfig::auto_mode_min_wait);
  integer("message_speed", &GlobalConfig::message_speed);
  boolean("message_nowait", &GlobalConfig::message_speed_nowait);
  boolean("font_bold", &GlobalConfig::font_futoku);
  integer("font_decoration", &GlobalConfig::font_shadow);
  m.def("get_font_name", [config] { return config->font_name; });
  m.def("set_font_name",
        [config](std::string name) { config->font_name = std::move(name); });
  m.def("set_font_name_default",
        [config, value = defaults.font_name] { config->font_name = value; });
  for (const auto [name, field] :
       {std::pair{"sleep_onoff", &GlobalSystemConfig::low_priority},
        {"wheel_next_message_onoff",
         &GlobalSystemConfig::allow_wheel_next_message},
        {"koe_dont_stop_onoff", &GlobalSystemConfig::continue_koe_playback},
        {"skip_unread_message_onoff",
         &GlobalSystemConfig::allow_skip_unread_message}}) {
    m.def(std::string("get_") + name,
          [config, field] { return config->system.*field ? 1 : 0; });
    m.def(std::string("set_") + name,
          [config, field](int value) { config->system.*field = value != 0; });
    m.def(std::string("set_") + name + "_default",
          [config, field, value = defaults.system.*field] {
            config->system.*field = value;
          });
  }
  for (const auto [name, get, set] :
       {std::tuple{"r", &RGBAColour::r, &RGBAColour::set_red},
        {"g", &RGBAColour::g, &RGBAColour::set_green},
        {"b", &RGBAColour::b, &RGBAColour::set_blue},
        {"a", &RGBAColour::a, &RGBAColour::set_alpha}}) {
    const auto prefix = std::string("filter_color_") + name;
    m.def("get_" + prefix,
          [config, get] { return (config->filter_color.*get)(); });
    m.def("set_" + prefix, [config, set](int value) {
      (config->filter_color.*set)(std::clamp(value, 0, 255));
    });
    m.def("set_" + prefix + "_default",
          [config, set, value = (defaults.filter_color.*get)()] {
            (config->filter_color.*set)(value);
          });
  }
  m.def("get_charakoe_onoff",
        [config](int id) { return config->chrkoe[id].enable ? 1 : 0; });
  m.def("set_charakoe_onoff", [config](int id, int value) {
    config->chrkoe[id].enable = value != 0;
  });
  m.def("set_charakoe_onoff_default", [config, defaults](int id) {
    auto it = defaults.chrkoe.find(id);
    config->chrkoe[id].enable =
        it == defaults.chrkoe.end() || it->second.enable;
  });
  m.def("set_charakoe_volume_default", [config, defaults](int id) {
    auto it = defaults.chrkoe.find(id);
    config->chrkoe[id].volume =
        it == defaults.chrkoe.end() ? 255 : it->second.volume;
  });
  m.def("get_window_mode",
        [config] { return config->screen_size_mode == 1 ? 1 : 0; });
  m.def("set_window_mode", [&runtime, config](int value) {
    config->screen_size_mode = value ? 1 : config->screen_size_mode_window;
    if (runtime.host().presenter)
      runtime.host().presenter->Resize(runtime.host().presenter->display_size(),
                                       value != 0);
  });
  // SDL continuously processes its output stream, including silent buffers.
  m.def("get_play_silent_sound_onoff", [] { return 1; });
}

RLVM_REGISTER(SiglusBindingRegistry, "5_menu_config", BindMenuConfig)

}  // namespace libsiglus::binding
