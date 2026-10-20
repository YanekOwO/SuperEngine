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

#include "long_operations/long_operation.hpp"
#include "machine/rlmachine.hpp"

class GraphicsObject;
class GraphicsSystem;
class System;
class TextSystem;

namespace lop {

class MenuReseter final : public LongOperation {
 public:
  explicit MenuReseter(System& system) : system_(system) {}
  Task<int> Run(WaitContext& waits) override;

 private:
  System& system_;
};

class RestoreTextSystemVisibility final : public LongOperation {
 public:
  explicit RestoreTextSystemVisibility(TextSystem& text) : text_(text) {}
  Task<int> Run(WaitContext& waits) override;

 private:
  TextSystem& text_;
};

class ClearAndRestoreWindow final : public LongOperation {
 public:
  ClearAndRestoreWindow(TextSystem& text, int window)
      : text_(text), window_(window) {}
  Task<int> Run(WaitContext& waits) override;

 private:
  TextSystem& text_;
  int window_;
};

// Blocking GAN playback temporarily enables automatic graphics updates.
class WaitForGanToFinish final : public LongOperation {
 public:
  WaitForGanToFinish(RLMachine& machine, int layer, int parent, int object);
  Task<int> Run(WaitContext& waits) override;

 private:
  GraphicsObject& GetObject();
  RLMachine& machine_;
  GraphicsSystem& graphics_;
  RLMachine::DCScreenUpdateMode mode_;
  int layer_;
  int parent_;
  int object_;
};

}  // namespace lop
