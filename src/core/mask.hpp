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

#include "core/mutator.hpp"

#include <memory>
#include <string>
#include <vector>

class SDLSurface;

class Mask {
 public:
  struct Parameters {
    int x = 0;
    int y = 0;

    void Reset();
  };

  using Parameter = int Parameters::*;
  using ParameterMutator = Mutator<Parameter, Parameters>;

  Mask() = default;

  void Reset();
  void Create(std::string filename, std::shared_ptr<SDLSurface> surface);
  void Execute();

  void AddMutator(ParameterMutator mutator);
  bool IsMutatorRunning(Parameter parameter) const;
  void EndMutator(Parameter parameter);

  inline Parameters& Param() { return param_; }
  inline const Parameters& Param() const { return param_; }
  inline const std::string& filename() const { return filename_; }
  inline const std::shared_ptr<SDLSurface>& surface() const { return surface_; }

 private:
  Parameters param_;
  std::string filename_;
  std::shared_ptr<SDLSurface> surface_;
  std::vector<ParameterMutator> mutators_;
};

class MaskList {
 public:
  explicit MaskList(std::size_t size);

  inline std::size_t size() const { return elements_.size(); }
  inline Mask& At(int index) { return elements_.at(index); }
  inline const Mask& At(int index) const { return elements_.at(index); }
  void Reset();
  void Execute();

 private:
  std::vector<Mask> elements_;
};
