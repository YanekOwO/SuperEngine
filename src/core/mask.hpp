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

#include "core/serialization_context.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/serialization.hpp"

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

   private:
    friend class boost::serialization::access;
    void serialize(auto& ar, unsigned int) { ar & x & y; }
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

 private:
  friend class boost::serialization::access;
  void save(auto& ar, unsigned int) const {
    std::vector<int> properties;
    for (const auto& mutator : mutators_) {
      if (mutator.setter() == &Parameters::x)
        properties.push_back(0);
      else if (mutator.setter() == &Parameters::y)
        properties.push_back(1);
      else
        throw std::runtime_error("Unknown saved mask event property");
    }
    ar & param_ & filename_;
    SerializeBoundedSequence(ar, properties);
    SerializeBoundedSequence(ar, mutators_);
  }
  void load(auto& ar, unsigned int) {
    std::vector<int> properties;
    ar & param_ & filename_;
    SerializeBoundedSequence(ar, properties);
    SerializeBoundedSequence(ar, mutators_);
    if (properties.size() != mutators_.size())
      throw std::runtime_error("Invalid saved mask event count");
    for (std::size_t i = 0; i < properties.size(); ++i) {
      if (properties[i] < 0 || properties[i] > 1)
        throw std::runtime_error("Invalid saved mask event property");
      mutators_[i].SetSetter(properties[i] == 0 ? &Parameters::x
                                                : &Parameters::y);
    }
    surface_ = filename_.empty()
                   ? nullptr
                   : SerializationContext::Get(ar).LoadSurface(filename_);
  }
  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

class MaskList {
 public:
  explicit MaskList(std::size_t size = 0);

  inline std::size_t size() const { return elements_.size(); }
  inline Mask& At(int index) { return elements_.at(index); }
  inline const Mask& At(int index) const { return elements_.at(index); }
  void Reset();
  void Execute();

 private:
  std::vector<Mask> elements_;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    SerializeBoundedSequence(ar, elements_);
  }
};
