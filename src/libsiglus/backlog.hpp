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

#include "core/button_action_table.hpp"
#include "core/event_listener.hpp"
#include "core/message_back.hpp"
#include "core/object.hpp"
#include "core/object_internal/drawer/string.hpp"
#include "utilities/serialization.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace libsiglus {
struct SiglusRuntime;

// A modal view of the recorded dialogue. Script continuations own a session,
// while the runtime owns its presentation and input routing.
class SiglusBacklog final : public EventListener {
 public:
  struct Session {
    bool open = false;
    int owner = 0;
    int scroll = 0;
    int voice_row = -1;
    std::size_t voice_offset = 0;

   private:
    friend class boost::serialization::access;
    void serialize(auto& ar, unsigned int) {
      ar & open & owner & scroll & voice_row & voice_offset;
    }
  };

  explicit SiglusBacklog(SiglusRuntime& runtime);
  bool CanOpen() const;
  bool IsOpen() const { return session_ && session_->open; }
  std::shared_ptr<Session> Open(int owner);
  void Close();
  void Scroll(int pixels);
  void OnEvent(std::shared_ptr<Event> event) override;
  void Render();
  std::optional<std::pair<int, int>> ReplayVoice(std::size_t row);
  bool RequestRollback(std::size_t row);
  std::shared_ptr<Session> Capture() const { return session_; }
  void Restore(std::shared_ptr<Session> session);
  int MaxScroll() const { return max_scroll_; }
  std::size_t RowCount() const { return rows_.size(); }

 private:
  enum class Action { Close, Up, Down, Slider, Voice, Load };
  struct Row {
    MessageBack::MsgbkEntry entry;
    GraphicsObject object;
    int top = 0, height = 0;
  };
  struct Button {
    GraphicsObject object;
    Action action;
    Point position;
    int action_no = 0, sound = -1;
    std::optional<std::size_t> row;
  };
  SiglusRuntime& runtime_;
  std::shared_ptr<Session> session_;
  std::vector<Row> rows_;
  std::vector<Button> buttons_;
  std::optional<GraphicsObject> background_, filter_;
  ButtonActionTable actions_;
  Rect window_, clip_;
  StringObjectStyle style_, active_style_;
  int message_action_ = 0;
  int message_x_ = 0, content_height_ = 0, max_scroll_ = 0;
  int slider_top_ = 0, slider_bottom_ = 0, slider_offset_ = 0;
  Point mouse_;
  std::optional<std::size_t> pressed_, pressed_row_;
  std::optional<KeyCode> closing_key_;

  void Build();
  void Position();
  void ConsumeInput();
  std::optional<GraphicsObject> Image(const std::string& name);
  std::optional<std::size_t> Hit();
  std::optional<std::size_t> HitRow() const;
  bool RowActionEnabled(std::size_t row) const;
  void Activate(Button& button);
};

}  // namespace libsiglus
