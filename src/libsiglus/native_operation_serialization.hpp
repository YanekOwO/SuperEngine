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

#include "core/frame_counter.hpp"
#include "long_operations/siglus_movie.hpp"
#include "utilities/serialization.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace lop {
class LongOperation;
class SiglusMovieOperation;
struct SiglusWipeState;
}  // namespace lop
namespace serilang {
class Promise;
}

class TextWindow;
class Selection;
class Group;

namespace libsiglus {
struct SiglusMachine;
namespace binding {
void RestoreWipeOperation(SiglusMachine& machine,
                          int owner,
                          std::shared_ptr<lop::SiglusWipeState> state);
void RestoreMovieOperation(SiglusMachine& machine,
                           lop::SiglusMovieOperation* operation,
                           std::shared_ptr<serilang::Promise> completion);
}  // namespace binding
struct SiglusMachine;
class ExecutionWriter;
class ExecutionReader;
struct TextoutOperationState {
  std::shared_ptr<TextWindow> window;
  std::string text;
  int phase = 0;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) { ar & window & text & phase; }
};
struct MessageWaitOperationState {
  std::shared_ptr<TextWindow> window;
  std::uint32_t pace_frame = 0;
  int phase = 0, after_wait = 0, result = 0;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & window & pace_frame & phase & after_wait & result;
  }
};
struct SelectionWaitOperationState {
  std::shared_ptr<Selection> selection;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) { ar & selection; }
};
struct GroupSelectionOperationState {
  std::shared_ptr<Group> group;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) { ar & group; }
};
struct WipeOperationState {
  int duration = 0;
  InterpolationMode interpolation = InterpolationMode::Linear;
  std::uint32_t promise = 0;
  int owner = 0, key_mode = -1, result = 0;
  int boundaries = 0, finish_boundary = -1;
  bool cancelled = true, wait_flag = true, primed = false;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & duration & interpolation & promise & owner & key_mode & result &
        boundaries & finish_boundary & cancelled & wait_flag & primed;
  }
};
struct MovieOperationState {
  std::string path;
  lop::MovieRect rectangle;
  std::uint32_t promise = 0;
  int volume = 255;
  bool wait_key = false, cancelled = false, relative_path = false;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & path & rectangle.x & rectangle.y & rectangle.width & rectangle.height &
        promise & volume & wait_key & cancelled & relative_path;
  }
};
using NativeOperationState = std::variant<TextoutOperationState,
                                          MessageWaitOperationState,
                                          SelectionWaitOperationState,
                                          GroupSelectionOperationState,
                                          WipeOperationState,
                                          MovieOperationState>;
NativeOperationState CaptureNativeOperation(lop::LongOperation& operation,
                                            ExecutionWriter& execution);
std::unique_ptr<lop::LongOperation> RestoreNativeOperation(
    SiglusMachine& machine,
    ExecutionReader& execution,
    const NativeOperationState& state);
}  // namespace libsiglus
