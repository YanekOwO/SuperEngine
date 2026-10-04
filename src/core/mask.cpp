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

#include "core/mask.hpp"

#include <algorithm>
#include <utility>

// ------------------------------------------------------------------------------
// class Mask
void Mask::Parameters::Reset() { x = y = 0; }

void Mask::Reset() {
  param_.Reset();
  filename_.clear();
  surface_.reset();
  mutators_.clear();
}

void Mask::Create(std::string filename, std::shared_ptr<SDLSurface> surface) {
  Reset();
  filename_ = std::move(filename);
  surface_ = std::move(surface);
}

void Mask::Execute() {
  std::erase_if(mutators_,
                [this](auto& mutator) { return mutator.Update(param_); });
}

void Mask::AddMutator(ParameterMutator mutator) {
  mutators_.emplace_back(std::move(mutator));
}

bool Mask::IsMutatorRunning(Parameter parameter) const {
  return std::any_of(
      mutators_.cbegin(), mutators_.cend(),
      [parameter](const auto& mutator) { return mutator.Matches(parameter); });
}

void Mask::EndMutator(Parameter parameter) {
  std::erase_if(mutators_, [this, parameter](auto& mutator) {
    if (!mutator.Matches(parameter))
      return false;
    mutator.SetToEnd(param_);
    return true;
  });
}

// ------------------------------------------------------------------------------
// class MaskList
MaskList::MaskList(std::size_t size) : elements_(size) {}

void MaskList::Reset() {
  for (Mask& element : elements_)
    element.Reset();
}

void MaskList::Execute() {
  for (Mask& element : elements_)
    element.Execute();
}
