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

#include "libsiglus/native_operation_serialization.hpp"
#include "long_operations/siglus_wait.hpp"
#include "utilities/serialization.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Counter;
class GraphicsObject;
class QuakeState;
struct Effect;

namespace libsiglus {
struct SiglusMachine;
class ExecutionWriter;
class ExecutionReader;

struct PollingWaitDescription {
  enum class Kind {
    Counter,
    MaskEvent,
    ObjectEvent,
    ObjectEvents,
    ObjectMovie,
    EffectEvent,
    Quake,
    Voice,
    SoundEffect,
    Bgm,
    Pcm
  };
  Kind kind = Kind::Counter;
  std::shared_ptr<Counter> counter;
  std::shared_ptr<GraphicsObject> object;
  std::shared_ptr<Effect> effect;
  std::shared_ptr<QuakeState> quake;
  int value = 0;
  int index = 0;
  int parameter = 0;
  std::string name;
  bool fast_forward = false;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & kind & counter & object & effect & quake & value & index & parameter &
        name & fast_forward;
  }
};

struct NativeWaitState {
  std::uint32_t promise = 0;
  int owner = -1;
  bool key_skip = false, started = false;
  int interval = 5;
  std::variant<PollingWaitDescription, NativeOperationState> value;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & promise & owner & key_skip & started & interval & value;
  }
};

lop::NativeWaitScope DescribePollingWait(lop::NativeWaitScope scope,
                                         PollingWaitDescription description);
std::vector<NativeWaitState> CaptureNativeWaits(SiglusMachine& machine,
                                                ExecutionWriter& execution);
void PrepareNativeWaits(SiglusMachine& machine,
                        ExecutionReader& execution,
                        const std::vector<NativeWaitState>& states);
void ActivateNativeWaits(SiglusMachine& machine);

}  // namespace libsiglus
