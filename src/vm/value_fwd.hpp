// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#pragma once

#include <memory>
#include <variant>

namespace serilang {

enum class CallResultMode { Keep, Discard };

struct Code;
struct Class;
struct Instance;
struct NativeClass;
struct NativeInstance;
struct Upvalue;
struct Fiber;
struct List;
struct Dict;
struct Function;
class NativeFunction;
struct BoundMethod;

class Value;
class IObject;

using TempValue = std::variant<Value, std::unique_ptr<IObject>>;

template <typename T>
concept is_object = std::derived_from<T, IObject>;

}  // namespace serilang
