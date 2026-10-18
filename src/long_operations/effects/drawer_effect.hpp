// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
// Copyright (C) 2024 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#pragma once

#include "long_operations/effects/effect.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <string>

namespace lop {

namespace DrawerEffectDetails {

enum class Direction { TopToBottom, BottomToTop, LeftToRight, RightToLeft };

class Rotator {
 public:
  Rotator(Size size, Direction direction);

  Size GetSize() const;

  Size Rotate(Size in) const;

  Rect Rotate(Rect in) const;

 private:
  Size screen_;
  Direction direction_;
};

struct DrawInstruction {
  Rect src_from, src_to;
  Rect dst_from, dst_to;

  std::string ToString() const;
};

class Strategy {
 public:
  virtual ~Strategy() = default;
  virtual Rect ComputeSrcRect(int amount_visible, const Size& size) const = 0;
  virtual Rect ComputeDstRect(int amount_visible, const Size& size) const = 0;
};

class ScrollStrategy : public Strategy {
 public:
  Rect ComputeSrcRect(int amount_visible, const Size& size) const override;
  Rect ComputeDstRect(int amount_visible, const Size& size) const override;
};

class SquashStrategy : public Strategy {
 public:
  Rect ComputeSrcRect(int amount_visible, const Size& size) const override;
  Rect ComputeDstRect(int amount_visible, const Size& size) const override;
};

class SlideStrategy : public Strategy {
 public:
  Rect ComputeSrcRect(int amount_visible, const Size& size) const override;
  Rect ComputeDstRect(int amount_visible, const Size& size) const override;
};

class NoneStrategy : public Strategy {
 public:
  Rect ComputeSrcRect(int amount_visible, const Size& size) const override;
  Rect ComputeDstRect(int amount_visible, const Size& size) const override;
};

class Composer {
 public:
  Composer(Size src, Size dst, Size screen, Direction direction);

  DrawInstruction Compose(const Strategy& on_effect,
                          const Strategy& off_effect,
                          int amount_visible) const;

  DrawInstruction Compose(const Strategy& on_effect,
                          const Strategy& off_effect,
                          float percentage_visible) const;

 private:
  Rotator src_rotator_;
  Rotator dst_rotator_;
  Size screen_;
};

}  // namespace DrawerEffectDetails

// Implement variations on \#SEL transition styles #15 (Scroll on, Scroll off),
// #16 (Scroll on, Squash off), #17 (Squash on, Scroll off), #18 (Squash on,
// Squash off), #20 (Slide on), #21 (Slide off). These effects are all very
// similar and are implemented by passing an enum of effect direction, and
// specifying two strategy classes, which describes how the surface should be
// drawn
template <class OnStrategy, class OffStrategy>
class DrawerEffect : public EffectLongOperation {
 public:
  static_assert(std::is_base_of_v<DrawerEffectDetails::Strategy, OnStrategy>,
                "OnStrategy must derive from Strategy");
  static_assert(std::is_base_of_v<DrawerEffectDetails::Strategy, OffStrategy>,
                "OffStrategy must derive from Strategy");

  DrawerEffect(RLMachine& machine,
               DrawerEffectDetails::Direction direction,
               std::shared_ptr<SDLSurface> src,
               std::shared_ptr<SDLSurface> dst,
               const Size& screen,
               int time)
      : EffectLongOperation(machine, src, dst, screen, time),
        direction_(direction) {}

  bool BlitOriginalImage() const final { return false; }

  void PerformEffectForTime(int current_time) final {
    const int duration = this->duration();
    const float percentage_visible =
        duration == 0 ? 1.0f : static_cast<float>(current_time) / duration;

    DrawerEffectDetails::Composer composer(
        src_surface().GetSize(), dst_surface().GetSize(), size(), direction_);
    auto draw_instruction =
        composer.Compose(OnStrategy(), OffStrategy(), percentage_visible);

    src_surface().RenderToScreen(draw_instruction.src_from,
                                 draw_instruction.src_to, 255);
    dst_surface().RenderToScreen(draw_instruction.dst_from,
                                 draw_instruction.dst_to, 255);
  }

 private:
  DrawerEffectDetails::Direction direction_;
};

}  // namespace lop
