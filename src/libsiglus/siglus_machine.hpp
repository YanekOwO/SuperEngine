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
#include "core/capture.hpp"
#include "core/counter.hpp"
#include "core/event_listener.hpp"
#include "core/game_config.hpp"
#include "core/gameexe.hpp"
#include "core/interaction_manager.hpp"
#include "core/memory_internal/memory.hpp"
#include "core/quake.hpp"
#include "core/selection.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "srbind/srbind.hpp"
#include "systems/system.hpp"
#include "vm/value.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace lop {
struct MovieFrame;
}

namespace serilang {
class VM;
class Promise;
struct Fiber;
};  // namespace serilang

template <class... Ts>
class IntListFacade;
template <class... Ts>
class StrListFacade;
class BgmTable;
class KidokuTable;
class IPlatformImplementor;
class MaskList;

namespace libsiglus {
using SiglusIntListFacade = ::IntListFacade<serilang::Fiber&>;
using SiglusStrListFacade = ::StrListFacade<serilang::Fiber&>;

namespace binding {
class FrameActionManager;
}  // namespace binding
class Archive;
class SiglusSceneRenderer;

using SiglusWipeEndHook = std::function<void()>;

struct SiglusMachine {
  std::filesystem::path base_pth, save_pth;
  std::shared_ptr<Gameexe> gameexe;
  std::shared_ptr<Archive> archive;
  std::unique_ptr<Memory> memory;
  LocalConfig local_config;
  GlobalConfig global_config;
  std::unique_ptr<serilang::VM> vm;
  std::unique_ptr<binding::Loader> loader;
  std::shared_ptr<AssetScanner> asset_scanner;

  std::unique_ptr<Stage> stage;
  std::unique_ptr<System> system;
  std::unique_ptr<InteractionManager> interaction_manager;

  // ------------------------------------------------------------------------------
  std::shared_ptr<SelState> selection_state;
  std::shared_ptr<EventListener> selection_event_listener;
  void StartSelection(std::shared_ptr<Selection> session);
  void FinishSelection(std::shared_ptr<Selection> session);
  void CancelSelection(std::shared_ptr<Selection> session);
  void UpdateSelection(int game_ms);
  // Overrides this exact selection key for the lifetime of this machine.
  // Choices are original option indices; -1 requests player cancellation.
  void RegisterDebugSelectionChoice(int scene, int kidoku, int choice);
  // Appends a choice for the next selection, ahead of indexed overrides.
  void QueueDebugSelectionChoice(int choice);
  // Called once after input or automation accepts a new selection result.
  void OnSelectionChosen(const Selection& session);

 private:
  std::map<std::pair<int, int>, int> debug_selection_choices_;
  std::deque<int> debug_selection_choice_queue_;
  bool updating_selection_ = false;

  // ------------------------------------------------------------------------------
 public:
  std::shared_ptr<BgmTable> bgm_table;
  std::shared_ptr<KidokuTable> kidoku_table;
  std::shared_ptr<CounterList> counter_list;
  std::shared_ptr<::QuakeList> screen_quakes;
  std::shared_ptr<binding::FrameActionManager> frame_action_manager;
  std::shared_ptr<MaskList> mask_list;

  std::shared_ptr<SiglusSceneRenderer> renderer;
  std::weak_ptr<lop::MovieFrame> movie_frame;
  std::function<void()> GetFrameRenderer() const;
  void RenderFrame(bool display = true);

  // ------------------------------------------------------------------------------
  std::shared_ptr<CaptureManager> captures;
  // Queue eagerly and complete at most one request after each system frame.
  serilang::Value RequestCapture(CaptureRequest request);
  void ProcessCaptureFrame();
  void ResetCaptures();
  inline bool HasPendingCaptures() const { return !pending_captures_.empty(); }
  inline bool CaptureDisplayEnabled() const {
    return pending_captures_.empty() ||
           pending_captures_.front().request.display;
  }

 private:
  struct PendingCapture {
    CaptureRequest request;
    std::shared_ptr<serilang::Promise> promise;
  };
  std::deque<PendingCapture> pending_captures_;

  // ------------------------------------------------------------------------------
 public:
  std::shared_ptr<EventListener> system_event_listener;
  std::shared_ptr<EventListener> mwnd_button_event_listener;
  std::shared_ptr<InputListener> input_event_listener;
  std::vector<std::function<void()>> reset_local_memory;
  std::shared_ptr<SiglusWipeEndHook> finish_active_wipe =
      std::make_shared<SiglusWipeEndHook>();
  std::function<void()> update_wipe_after_frame_actions;
  std::shared_ptr<IPlatformImplementor> platform_implementor;

  std::shared_ptr<srbind::class_<SiglusIntListFacade>> ilist_cls;
  std::shared_ptr<srbind::class_<SiglusStrListFacade>> slist_cls;
  std::function<serilang::Value(std::shared_ptr<Counter>)> make_counter;
  std::function<serilang::Value(std::shared_ptr<GraphicsObject>)> make_object;

  SiglusMachine() = default;
  ~SiglusMachine();
  SiglusMachine(const SiglusMachine&) = delete;
  SiglusMachine& operator=(const SiglusMachine&) = delete;
  SiglusMachine(SiglusMachine&&) noexcept = delete;
  SiglusMachine& operator=(SiglusMachine&&) noexcept = delete;

  // Schedule the configured startup scene, or an explicit scene ID override.
  void Start(std::optional<int> start_scene = std::nullopt);
  void Run();

  // Advance one frame using given deltas.
  void Update(std::chrono::milliseconds real_delta,
              std::chrono::milliseconds game_delta);

  // Create an inactive caller-owned counter.
  std::shared_ptr<Counter> CreateCounter();

 private:
  std::vector<std::weak_ptr<Counter>> counters_;
};

}  // namespace libsiglus
