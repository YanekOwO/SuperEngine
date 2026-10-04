// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#pragma once

#include "core/frame_counter.hpp"

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

template <class Setter, class Context = void>
class Mutator {
 public:
  Mutator(Setter setter, FrameCounter fc)
      : setter_(std::move(setter)), fc_(std::move(fc)) {}

  bool Update()
    requires std::same_as<Context, void>
  {
    Apply(static_cast<int>(fc_.ReadFrame()));
    return fc_.IsFinished();
  }

  template <class T = Context>
  bool Update(T& context)
    requires(!std::same_as<T, void> && std::same_as<T, Context>)
  {
    Apply(context, static_cast<int>(fc_.ReadFrame()));
    return fc_.IsFinished();
  }

  void SetToEnd()
    requires std::same_as<Context, void>
  {
    fc_.EndTimer();
    Update();
  }

  template <class T = Context>
  void SetToEnd(T& context)
    requires(!std::same_as<T, void> && std::same_as<T, Context>)
  {
    fc_.EndTimer();
    Update(context);
  }

 private:
  void Apply(int value)
    requires std::same_as<Context, void>
  {
    if constexpr (std::is_invocable_r_v<int&, Setter&>) {
      std::invoke(setter_) = value;
    } else {
      static_assert(std::is_invocable_v<Setter&, int>,
                    "Mutator setter must accept an int or return int&");
      std::invoke(setter_, value);
    }
  }

  template <class T = Context>
  void Apply(T& context, int value)
    requires(!std::same_as<T, void> && std::same_as<T, Context>)
  {
    if constexpr (std::is_invocable_r_v<int&, Setter&, T&>) {
      std::invoke(setter_, context) = value;
    } else {
      static_assert(
          std::is_invocable_v<Setter&, T&, int>,
          "Contextual Mutator setter must accept (context, int) or return "
          "int&");
      std::invoke(setter_, context, value);
    }
  }

  Setter setter_;
  FrameCounter fc_;
};
