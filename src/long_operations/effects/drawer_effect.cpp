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

#include "long_operations/effects/drawer_effect.hpp"

#include "systems/graphics_system.hpp"

#include <sstream>

namespace lop {

namespace DrawerEffectDetails {

// Rotator implementations
Rotator::Rotator(Size size, Direction direction)
    : screen_(size), direction_(direction) {}

Size Rotator::GetSize() const { return Rotate(screen_); }

Size Rotator::Rotate(Size in) const {
  switch (direction_) {
    case Direction::LeftToRight:
    case Direction::RightToLeft:
      return Size(in.height(), in.width());
    default:
      return in;
  }
}

Rect Rotator::Rotate(Rect in) const {
  switch (direction_) {
    case Direction::BottomToTop:
      return Rect(Point(screen_) - Size(in.lower_right()), in.size());
    case Direction::LeftToRight:
      return Rect(Point(in.y(), screen_.height() - in.x2()),
                  Size(in.size().height(), in.size().width()));
    case Direction::RightToLeft:
      return Rect(Point(screen_.width() - in.y2(), in.x()),
                  Size(in.size().height(), in.size().width()));
    default:
      return in;
  }
}

// DrawInstruction implementation
std::string DrawInstruction::ToString() const {
  auto rect_to_string = [](Rect rect) -> std::string {
    std::ostringstream oss;
    oss << '(' << rect.x() << ',' << rect.y() << ',';
    oss << rect.x2() << ',' << rect.y2() << ')';
    return oss.str();
  };

  std::ostringstream oss;
  oss << "src: " << rect_to_string(src_from) << " -> " << rect_to_string(src_to)
      << '\n';
  oss << "dst: " << rect_to_string(dst_from) << " -> "
      << rect_to_string(dst_to);
  return oss.str();
}

// Strategy implementations
Rect ScrollStrategy::ComputeSrcRect(int amount_visible,
                                    const Size& size) const {
  return Rect::GRP(0, size.height() - amount_visible, size.width(),
                   size.height());
}

Rect ScrollStrategy::ComputeDstRect(int amount_visible,
                                    const Size& size) const {
  return Rect::GRP(0, 0, size.width(), size.height() - amount_visible);
}

Rect SquashStrategy::ComputeSrcRect(int amount_visible,
                                    const Size& size) const {
  return Rect::GRP(0, 0, size.width(), size.height());
}

Rect SquashStrategy::ComputeDstRect(int amount_visible,
                                    const Size& size) const {
  return Rect::GRP(0, 0, size.width(), size.height());
}

Rect SlideStrategy::ComputeSrcRect(int amount_visible, const Size& size) const {
  return Rect::GRP(0, size.height() - amount_visible, size.width(),
                   size.height());
}

Rect SlideStrategy::ComputeDstRect(int amount_visible, const Size& size) const {
  return Rect::GRP(0, 0, size.width(), size.height() - amount_visible);
}

Rect NoneStrategy::ComputeSrcRect(int amount_visible, const Size& size) const {
  return Rect::GRP(0, 0, size.width(), amount_visible);
}

Rect NoneStrategy::ComputeDstRect(int amount_visible, const Size& size) const {
  return Rect::GRP(0, amount_visible, size.width(), size.height());
}

// Composer implementations
Composer::Composer(Size src, Size dst, Size screen, Direction direction)
    : src_rotator_(src, direction),
      dst_rotator_(dst, direction),
      screen_(Rotator(screen, direction).GetSize()) {}

DrawInstruction Composer::Compose(const Strategy& on_effect,
                                  const Strategy& off_effect,
                                  int amount_visible) const {
  DrawInstruction instruction;
  instruction.src_to =
      src_rotator_.Rotate(Rect::GRP(0, 0, screen_.width(), amount_visible));
  instruction.dst_to = dst_rotator_.Rotate(
      Rect::GRP(0, amount_visible, screen_.width(), screen_.height()));
  instruction.src_from = src_rotator_.Rotate(
      on_effect.ComputeSrcRect(amount_visible, src_rotator_.GetSize()));
  instruction.dst_from = dst_rotator_.Rotate(
      off_effect.ComputeDstRect(amount_visible, dst_rotator_.GetSize()));
  return instruction;
}

DrawInstruction Composer::Compose(const Strategy& on_effect,
                                  const Strategy& off_effect,
                                  float percentage_visible) const {
  int amount_visible = static_cast<int>(percentage_visible * screen_.height());
  return Compose(on_effect, off_effect, amount_visible);
}

}  // namespace DrawerEffectDetails

}  // namespace lop
