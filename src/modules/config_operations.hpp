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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#pragma once

#include "machine/rlmachine.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/rlop_store.hpp"

#include <functional>
#include <utility>

// Resolve borrowed configuration through the executing machine. Module
// prototypes must never retain references to a particular machine's settings.
struct ConfigIntSetter : RLOpcode<IntConstant_T> {
  explicit ConfigIntSetter(std::function<void(RLMachine&, int)> action)
      : action_(std::move(action)) {}
  void operator()(RLMachine& machine, int value) override {
    action_(machine, value);
  }

 private:
  std::function<void(RLMachine&, int)> action_;
};

struct ConfigIntGetter : RLStoreOpcode<> {
  explicit ConfigIntGetter(std::function<int(RLMachine&)> read)
      : read_(std::move(read)) {}
  int operator()(RLMachine& machine) override { return read_(machine); }

 private:
  std::function<int(RLMachine&)> read_;
};

struct ConfigAction : RLOpcode<> {
  explicit ConfigAction(std::function<void(RLMachine&)> action)
      : action_(std::move(action)) {}
  void operator()(RLMachine& machine) override { action_(machine); }

 private:
  std::function<void(RLMachine&)> action_;
};

struct ConfigIntIntSetter : RLOpcode<IntConstant_T, IntConstant_T> {
  explicit ConfigIntIntSetter(std::function<void(RLMachine&, int, int)> action)
      : action_(std::move(action)) {}
  void operator()(RLMachine& machine, int first, int second) override {
    action_(machine, first, second);
  }

 private:
  std::function<void(RLMachine&, int, int)> action_;
};

struct ConfigIntArgumentGetter : RLStoreOpcode<IntConstant_T> {
  explicit ConfigIntArgumentGetter(std::function<int(RLMachine&, int)> read)
      : read_(std::move(read)) {}
  int operator()(RLMachine& machine, int value) override {
    return read_(machine, value);
  }

 private:
  std::function<int(RLMachine&, int)> read_;
};
