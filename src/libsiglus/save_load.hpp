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

#pragma once

#include "libsiglus/save_manager.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

class SDLSurface;

namespace serilang {
struct Fiber;
class Promise;
}  // namespace serilang

namespace libsiglus {
struct SiglusRuntime;
struct SiglusMachine;

// Checkpoints describe script execution; saving a slot never captures the menu
// that requested the save. Restoration runs outside all native VM calls.
class SiglusSaveLoad {
 public:
  enum class Source { Disk, Quick, End, Inner, Selection, Backlog };
  explicit SiglusSaveLoad(SiglusRuntime& runtime);
  ~SiglusSaveLoad();
  SaveManager& manager() { return manager_; }
  const SaveManager& manager() const { return manager_; }

  void RequestCheckpoint(SiglusMachine& machine,
                         serilang::Fiber& fiber,
                         bool explicit_result = true);
  void BeginMessageBlock(SiglusMachine& machine, serilang::Fiber& fiber);
  void ClearCheckpoint();
  void AppendMessage(std::string_view message);
  void AppendName(std::string_view name);
  void Reset(bool preserve_backlog = false);
  // Returns true when a load/restart owns script execution for this drive.
  bool Process();
  bool pending() const;
  bool CheckpointPending() const { return checkpoint_.has_value(); }

  bool Save(int slot, bool warning, bool sound, Source source = Source::Disk);
  bool Load(SiglusMachine& machine,
            int slot,
            bool warning,
            bool sound,
            bool fade,
            Source source = Source::Disk);
  void OpenMenu(bool save);
  void LoadGlobal();
  bool FlushGlobal();
  void Shutdown();
  void RequestQuit(bool fade = false);
  bool PauseGameTime() const;
  void RenderOverlay();

  void SelectBacklog(CheckpointId id);
  void OpenBacklogLoad(CheckpointId id);
  CheckpointId selected_backlog() const { return selected_backlog_; }

 private:
  struct CheckpointRequest {
    SiglusMachine* machine;
    serilang::Fiber* fiber;
    std::weak_ptr<char> lifetime;
    bool explicit_result;
  };
  struct PreparedLoad;
  SiglusRuntime& runtime_;
  std::uint64_t game_fingerprint_ = 0;
  SaveManager manager_;
  std::optional<CheckpointRequest> checkpoint_;
  std::unique_ptr<PreparedLoad> load_;
  std::chrono::steady_clock::time_point load_deadline_;
  std::chrono::steady_clock::time_point transition_start_;
  std::optional<std::chrono::steady_clock::time_point> fade_in_deadline_;
  std::optional<std::chrono::steady_clock::time_point> quit_deadline_;
  std::shared_ptr<SDLSurface> fade_surface_;
  bool after_load_ = false;
  std::function<void()> activate_clock_;
  CheckpointId selected_backlog_ = 0;
  std::map<CheckpointId, SlotPayload> backlog_;
  CheckpointId last_backlog_checkpoint_ = 0;
  std::uint64_t backlog_counter_ = 0;
  std::shared_ptr<serilang::Promise> end_capture_;
  bool end_sound_ = false;

  SaveLocalExtra Extra() const;
  bool Confirm(bool save, bool quick, bool selection = false) const;
  void AppendCheckpointMessage(std::string_view message);
  std::string Capture(const CheckpointRequest& request);
  std::unique_ptr<PreparedLoad> Prepare(SlotPayload payload);
  void Commit(PreparedLoad& load);
};

}  // namespace libsiglus
