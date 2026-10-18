// -----------------------------------------------------------------------
//
// This file is part of RLVM
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
// -----------------------------------------------------------------------

#pragma once

#include "libsiglus/element_code.hpp"
#include "libsiglus/function.hpp"
#include "libsiglus/value.hpp"

#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace libsiglus::elm {

// Parsed representation of a Siglus "element" expression. Parsing starts from a
// root value or a synthetic top-level symbol and then appends member, call, and
// subscript nodes to reconstruct the access path encoded in bytecode.

struct Root;
struct Node;

// User-defined command reference emitted as `@scene.entry:name(...)`.
struct Usrcmd {
  int scene, entry;
  std::string_view name;
  Invoke arguments;

  std::string ToDebugString() const;
  bool operator==(const Usrcmd&) const = default;
};

// User-defined property reference emitted as `@scene.idx:name`.
struct Usrprop {
  int scene, idx;
  std::string_view name;
  std::string ToDebugString() const;
  bool operator==(const Usrprop&) const = default;
};

// Reference to the current function's positional argument (`arg_N`).
struct Arg {
  int id;
  std::string ToDebugString() const;
  bool operator==(const Arg&) const = default;
};

struct Farcall {
  Value scn_name;              // Scene name; encoded as a string value.
  Value zlabel = Integer(0);   // Target label; encoded as an integer value.
  std::vector<Value> intargs;  // Positional integer argument block.
  std::vector<Value> strargs;  // Positional string argument block.

  std::string ToDebugString() const;
  bool operator==(const Farcall&) const = default;
};

struct Root {
  // Special root variants that do not naturally fit in the node chain.
  // `std::monostate` is used for synthetic symbol roots such as `syscom`,
  // where the first visible identifier is stored as a Member in `nodes`.
  using var_t = std::variant<std::monostate, Usrcmd, Usrprop, Arg, Farcall>;
  var_t var;
  Type type;  // Static type of the root before walking `nodes`.

  template <typename... Ts>
    requires std::constructible_from<var_t, Ts...>
  Root(Type type, Ts&&... params)
      : var(std::forward<Ts>(params)...), type(type) {}
  template <typename... Ts>
    requires std::constructible_from<var_t, Ts...>
  Root(Ts&&... params)
      : var(std::forward<Ts>(params)...), type(Type::Invalid) {}

  inline std::string ToDebugString() const {
    return std::visit(
        [](const auto& x) -> std::string {
          if constexpr (std::same_as<std::decay_t<decltype(x)>, std::monostate>)
            return "";
          else
            return x.ToDebugString();
        },
        var);
  }
  bool operator==(const Root&) const = default;
};

// -----------------------------------------------------------------------
struct CallFlags {
  // Marks calls that came from an explicit bind payload. The default models an
  // implicit call.
  bool is_explicit = false;
  // Simple callables do not need Siglus' overload id at runtime. Ambiguous
  // element ids that dispatch to different exported names remain non-simple.
  bool is_nonsimple = false;
  // Calls that model blocking commands should return an awaitable.
  bool await_result = false;
  bool context = false;

  constexpr CallFlags operator|(const CallFlags f) const {
    return {.is_explicit = is_explicit || f.is_explicit,
            .is_nonsimple = is_nonsimple || f.is_nonsimple,
            .await_result = await_result || f.await_result,
            .context = context || f.context};
  }
};
[[maybe_unused]] constexpr CallFlags EXPLICIT{.is_explicit = true};
[[maybe_unused]] constexpr CallFlags NONSIMPLE{.is_nonsimple = true};
[[maybe_unused]] constexpr CallFlags AWAIT{.await_result = true};
[[maybe_unused]] constexpr CallFlags CONTEXT{.context = true};

struct Member {
  std::string_view name;  // Reconstructed exported/native member name.
  // Return type to use when this member is materialized as an implicit call.
  Type call_return_type = Type::None;
  // Marks zero-argument ops that should parse as `member()` even without an
  // explicit bind payload.
  bool implicit_call = false;
  // Simple callables do not need Siglus' overload id at runtime. Ambiguous
  // element ids that dispatch to different exported names remain non-simple.
  bool is_simple = true;
  // Calls that model blocking commands should return an awaitable.
  bool await_result = false;
  bool selection_context = false;

  std::string ToDebugString() const;
  bool operator==(const Member&) const = default;
};

// Call nodes preserve the raw argument bags attached to an element. Ambiguous
// native dispatches also retain Siglus' integer overload id; simple calls drop
// it once the selected member name is known.
struct Call {
  // Present for explicit binds from bytecode. Implicit zero-argument calls use
  // `nullopt` because there is no standalone overload token to preserve. Simple
  // explicit calls also use `nullopt` because their selected member fully
  // identifies the runtime function.
  std::optional<int> overload_id;
  std::vector<Value> args;
  std::vector<std::pair<int, Value>> kwargs;
  bool is_simple = true;
  bool await_result = false;
  bool context = false;
  std::optional<int> kidoku;
  std::string ToDebugString() const;
  bool operator==(const Call&) const = default;
};

struct Subscript {
  Value idx;  // Index expression; `-1` may be used as a placeholder on errors.
  std::string ToDebugString() const;
  bool operator==(const Subscript&) const = default;
};

// A property setter encoded as a Siglus command. Unlike an assignment token,
// the command produces a nil result for its destination temporary.
struct SetMember {
  std::string_view name;
  Value value;
  std::string ToDebugString() const;
  bool operator==(const SetMember&) const = default;
};

struct Node {
  using var_t = std::variant<Member, Call, Subscript, SetMember>;
  var_t var;
  Type type;  // Static type after evaluating this step in the chain.

  template <typename... Ts>
    requires std::constructible_from<var_t, Ts...>
  Node(Type type, Ts&&... params)
      : var(std::forward<Ts>(params)...), type(type) {}
  template <typename... Ts>
    requires std::constructible_from<var_t, Ts...>
  Node(Ts&&... params)
      : var(std::forward<Ts>(params)...), type(Type::Invalid) {}

  inline std::string ToDebugString() const {
    return std::visit([](const auto& x) { return x.ToDebugString(); }, var);
  }
  bool operator==(const Node&) const = default;

  // Builds a call node from parsed bind data. Implicit calls and simple
  // explicit calls intentionally drop the overload id because the selected
  // member is sufficient to identify the runtime function.
  static Node BuildCall(Invoke inv, CallFlags flags = {});
};

// Normalized AST for an element bytecode sequence.
//
// Example:
//   53, -1, 8, 1  ->  frame_action_ch[8].start()
//
// Most elements are reconstructed as a left-to-right chain of members,
// subscripts, and calls rooted at either a special object (`syscom`, `system`,
// `arg_0`, ...) or one of the dedicated Root variants above. `GetType()`
// returns the type of the final node, or `root.type` when the chain is empty.
struct AccessChain {
  Root root;
  std::vector<Node> nodes;
  // Some text-related element encodings carry a kidoku marker alongside the
  // access chain. When present, the parser stores it here for later tokens.
  std::optional<int> kidoku = std::nullopt;

  std::string ToDebugString() const;
  Type GetType() const;
  bool operator==(const AccessChain&) const = default;
};

}  // namespace libsiglus::elm
