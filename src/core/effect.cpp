// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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

#include "core/effect.hpp"

#include "core/object_internal/object_parameter.hpp"

#include <algorithm>
#include <utility>

void Effect::Reset() { *this = Effect(); }

bool Effect::Contains(int order, int layer) const {
  return std::pair(begin_order, begin_layer) <= std::pair(order, layer) &&
         std::pair(order, layer) <= std::pair(end_order, end_layer);
}

void Effect::ApplyTo(ObjectParameter& param) const {
  param.position_x += x;
  param.position_y += y;
  param.z_depth += z;
  param.SetMono(ObjectParameter::ComposeEffectLevel(param.mono(), mono));
  param.SetInvert(ObjectParameter::ComposeEffectLevel(param.invert(), reverse));
  param.SetBright(ObjectParameter::ComposeEffectLevel(param.Bright(), bright));
  param.SetDark(ObjectParameter::ComposeEffectLevel(param.Dark(), dark));

  const int child_rate = param.colour_level();
  if (child_rate + color_rate > 0) {
    const int denominator = 255 * 255 - (255 - child_rate) * (255 - color_rate);
    const int parent_rate = color_rate * 255 * 255 / denominator;
    param.SetColourRed(
        (param.colour_red() * (255 - parent_rate) + color_r * parent_rate) /
        255);
    param.SetColourGreen(
        (param.colour_green() * (255 - parent_rate) + color_g * parent_rate) /
        255);
    param.SetColourBlue(
        (param.colour_blue() * (255 - parent_rate) + color_b * parent_rate) /
        255);
  }
  param.SetColourLevel(
      ObjectParameter::ComposeEffectLevel(child_rate, color_rate));
  param.SetTintRed(std::clamp(param.tint_red() + color_add_r, 0, 255));
  param.SetTintGreen(std::clamp(param.tint_green() + color_add_g, 0, 255));
  param.SetTintBlue(std::clamp(param.tint_blue() + color_add_b, 0, 255));
}

int Effect::Get(int idx) const {
  switch (idx) {
    case 0:
      return x;
    case 1:
      return y;
    case 2:
      return z;
    case 3:
      return mono;
    case 4:
      return reverse;
    case 5:
      return bright;
    case 6:
      return dark;
    case 7:
      return color_r;
    case 8:
      return color_g;
    case 9:
      return color_b;
    case 10:
      return color_rate;
    case 11:
      return color_add_r;
    case 12:
      return color_add_g;
    case 13:
      return color_add_b;
    case 28:
      return begin_order;
    case 29:
      return end_order;
    case 31:
      return wipe_copy;
    case 32:
      return wipe_erase;
    case 33:
      return begin_layer;
    case 34:
      return end_layer;
    default:
      throw std::runtime_error("Invalid stage effect property " +
                               std::to_string(idx));
  }
}

void Effect::Set(int idx, int value) {
  const int level = std::clamp(value, 0, 255);
  switch (idx) {
    case 0:
      x = value;
      break;
    case 1:
      y = value;
      break;
    case 2:
      z = value;
      break;
    case 3:
      mono = level;
      break;
    case 4:
      reverse = level;
      break;
    case 5:
      bright = level;
      break;
    case 6:
      dark = level;
      break;
    case 7:
      color_r = level;
      break;
    case 8:
      color_g = level;
      break;
    case 9:
      color_b = level;
      break;
    case 10:
      color_rate = level;
      break;
    case 11:
      color_add_r = level;
      break;
    case 12:
      color_add_g = level;
      break;
    case 13:
      color_add_b = level;
      break;
    case 28:
      begin_order = value;
      break;
    case 29:
      end_order = value;
      break;
    case 31:
      wipe_copy = value != 0;
      break;
    case 32:
      wipe_erase = value != 0;
      break;
    case 33:
      begin_layer = value;
      break;
    case 34:
      end_layer = value;
      break;
    default:
      throw std::runtime_error("Invalid stage effect property " +
                               std::to_string(idx));
  }
}

std::string_view Effect::GetPropertyName(int idx) {
  switch (idx) {
    case 0:
      return "x";
    case 1:
      return "y";
    case 2:
      return "z";
    case 3:
      return "mono";
    case 4:
      return "reverse";
    case 5:
      return "bright";
    case 6:
      return "dark";
    case 7:
      return "color_r";
    case 8:
      return "color_g";
    case 9:
      return "color_b";
    case 10:
      return "color_rate";
    case 11:
      return "color_add_r";
    case 12:
      return "color_add_g";
    case 13:
      return "color_add_b";
    case 28:
      return "begin_order";
    case 29:
      return "end_order";
    case 31:
      return "wipe_copy";
    case 32:
      return "wipe_erase";
    case 33:
      return "begin_layer";
    case 34:
      return "end_layer";
    default:
      throw std::runtime_error("Invalid stage effect property " +
                               std::to_string(idx));
  }
}
