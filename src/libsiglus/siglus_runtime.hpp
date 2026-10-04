// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#pragma once

#include "core/asset_scanner.hpp"
#include "core/event_listener.hpp"
#include "core/gameexe.hpp"
#include "core/interaction_manager.hpp"
#include "core/memory_internal/memory.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "srbind/srbind.hpp"
#include "systems/system.hpp"
#include "vm/value.hpp"

#include <filesystem>
#include <functional>
#include <memory>

namespace serilang {
class VM;
};  // namespace serilang

class IntListFacade;
class StrListFacade;
class BgmTable;
class KidokuTable;
class IPlatformImplementor;
class MaskList;

namespace libsiglus {
class Archive;
class SiglusSceneRenderer;

struct SiglusRuntime {
  std::filesystem::path base_pth, save_pth;
  std::shared_ptr<Gameexe> gameexe;
  std::shared_ptr<Archive> archive;
  std::unique_ptr<Memory> memory;
  std::unique_ptr<serilang::VM> vm;
  std::unique_ptr<binding::Loader> loader;
  std::shared_ptr<AssetScanner> asset_scanner;

  std::unique_ptr<System> system;
  std::unique_ptr<Stage> stage;
  std::unique_ptr<InteractionManager> interaction_manager;

  std::shared_ptr<Gameexe> local_config, global_config;
  std::shared_ptr<BgmTable> bgm_table;
  std::shared_ptr<KidokuTable> kidoku_table;
  std::shared_ptr<MaskList> mask_list;

  std::shared_ptr<SiglusSceneRenderer> renderer;
  std::shared_ptr<EventListener> system_event_listener;
  std::shared_ptr<InputListener> input_event_listener;
  std::function<void()> exec_sdl_callback;
  std::function<void()> reset_local_memory;
  std::shared_ptr<IPlatformImplementor> platform_implementor;

  std::shared_ptr<srbind::class_<IntListFacade>> ilist_cls;
  std::shared_ptr<srbind::class_<StrListFacade>> slist_cls;

  SiglusRuntime() = default;
  ~SiglusRuntime();
  SiglusRuntime(const SiglusRuntime&) = delete;
  SiglusRuntime& operator=(const SiglusRuntime&) = delete;
  SiglusRuntime(SiglusRuntime&&) noexcept = default;
  SiglusRuntime& operator=(SiglusRuntime&&) noexcept = default;
};

}  // namespace libsiglus
