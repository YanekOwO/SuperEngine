// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2013 Elliot Glaysher
// Copyright (C) 2025 Serina Sakurai
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

#include "core/mutator.hpp"

#include <functional>
#include <string>

class ObjectParameter;

using ObjectParameterMutator =
    Mutator<std::function<void(ObjectParameter&, int)>, ObjectParameter>;

class ObjectMutator {
  using DoneFn = std::function<void(ObjectParameter&)>;
  ObjectParameterMutator mutator_;
  int repr_;
  std::string name_;
  DoneFn on_complete_;

 public:
  ObjectMutator(ObjectParameterMutator mut,
                int repr = 0,
                std::string name = "unknown");

  // Please use DeepCopy instead
  ObjectMutator(const ObjectMutator&) = delete;
  ObjectMutator& operator=(const ObjectMutator&) = delete;
  ObjectMutator(ObjectMutator&&) noexcept = default;
  ObjectMutator& operator=(ObjectMutator&&) noexcept = default;

  ObjectMutator DeepCopy() const;

  inline void SetRepr(int in) { repr_ = in; }
  inline void SetName(std::string in) { name_.swap(in); }
  inline int repr() const { return repr_; }
  inline const std::string& name() const { return name_; }
  inline bool OperationMatches(int repr, const std::string& name) const {
    return repr_ == repr && name_ == name;
  }

  void OnComplete(DoneFn fn);

  bool Update(ObjectParameter& pm);
  void SetToEnd(ObjectParameter& pm);
};
