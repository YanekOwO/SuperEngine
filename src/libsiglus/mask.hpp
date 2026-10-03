// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/frame_counter.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Clock;
class SDLSurface;

namespace libsiglus {

class MaskValue {
 public:
  enum class EventType { OneShot, Loop, Turn };

  explicit MaskValue(std::shared_ptr<Clock> clock);

  void Reset();
  void SetValue(int value);
  int GetValue() const { return value_; }
  int GetRenderValue() const { return render_value_; }

  void SetEvent(int value, int duration, int delay, int speed_type);
  void LoopEvent(int start, int end, int duration, int delay, int speed_type);
  void TurnEvent(int start, int end, int duration, int delay, int speed_type);
  void EndEvent();
  bool CheckEvent() const { return frame_counter_.has_value(); }
  void Execute();

 private:
  void StartEvent(EventType event_type,
                  int start,
                  int end,
                  int duration,
                  int delay,
                  int speed_type);

  std::shared_ptr<Clock> clock_;
  int value_ = 0;
  int render_value_ = 0;
  std::optional<FrameCounter> frame_counter_;
};

class MaskElement {
 public:
  explicit MaskElement(std::shared_ptr<Clock> clock);

  void Reset();
  void Create(std::string filename, std::shared_ptr<SDLSurface> surface);
  void Execute();

  MaskValue& x() { return x_; }
  const MaskValue& x() const { return x_; }
  MaskValue& y() { return y_; }
  const MaskValue& y() const { return y_; }
  const std::string& filename() const { return filename_; }
  const std::shared_ptr<SDLSurface>& surface() const { return surface_; }

 private:
  MaskValue x_;
  MaskValue y_;
  std::string filename_;
  std::shared_ptr<SDLSurface> surface_;
};

class MaskList {
 public:
  MaskList(std::size_t size, std::shared_ptr<Clock> clock);

  std::size_t size() const { return elements_.size(); }
  MaskElement& At(int index);
  const MaskElement& At(int index) const;
  void Reset();
  void Execute();

 private:
  std::vector<MaskElement> elements_;
};

}  // namespace libsiglus
