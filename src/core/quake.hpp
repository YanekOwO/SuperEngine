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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct QuakeTransform {
  double scale = 1.0;
  double translate_x = 0.0;
  double translate_y = 0.0;

  bool IsIdentity() const;

  // Compose |next| after the transform already stored in this object.
  void Then(const QuakeTransform& next);
};

class QuakeState {
 public:
  enum class Type : int {
    None = -1,
    Vector = 0,
    Directional = 1,
    Zoom = 2,
  };

  struct StartParams {
    int type = 0;
    int direction = 0;
    int power = 0;
    int period_ms = 1000;
    int count = 0;
    int end_count = 0;
    int center_x = 0;
    int center_y = 0;
    int begin_order = 0;
    int end_order = 0;
  };

  QuakeState();

  void Reset();
  // Returns false for unsupported quake types and leaves the current quake
  // untouched, as the legacy dispatcher does.
  bool Start(StartParams params);
  void End(int duration_ms = 0);
  void Advance(std::int64_t elapsed_ms);

  int Check() const;
  bool IsActive() const { return type_ != Type::None; }
  bool Contains(int order) const;
  QuakeTransform CurrentTransform() const;

 private:
  void UpdateTransform();

  Type type_ = Type::None;
  int direction_ = 0;
  int power_ = 0;
  int period_ms_ = 0;
  int count_ = 0;
  int end_count_ = 0;
  int center_x_ = 0;
  int center_y_ = 0;
  int begin_order_ = 0;
  int end_order_ = 0;
  std::int64_t elapsed_ms_ = 0;
  bool ending_ = false;
  int end_duration_ms_ = 0;
  std::int64_t end_elapsed_ms_ = 0;
  QuakeTransform transform_;
};

class QuakeList {
 public:
  explicit QuakeList(std::size_t size = 0);

  inline std::size_t size() const { return elements_.size(); }
  std::weak_ptr<QuakeState> GetHandle(int index) const;
  void Advance(std::int64_t elapsed_ms);
  void Reset();
  bool IsActive() const;
  QuakeTransform TransformForOrder(int order) const;

 private:
  std::vector<std::shared_ptr<QuakeState>> elements_;
};

QuakeTransform ComposeQuakeTransforms(
    int order,
    const QuakeList* screen_quakes,
    const std::vector<std::shared_ptr<QuakeState>>& stage_quakes);
