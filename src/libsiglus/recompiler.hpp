// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "libsiglus/element.hpp"
#include "libsiglus/parser.hpp"
#include "libsiglus/token.hpp"
#include "utilities/expected.hpp"
#include "vm/gc.hpp"
#include "vm/object.hpp"
#include "vm/value.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace libsiglus {

struct AllocationPlan {
  std::vector<uint16_t> slots;
  uint16_t total_fast_locals;
};
expected<AllocationPlan, std::string> resolve_fast_slots(
    const std::vector<Parser::ParsedToken>& tokens);

struct CompileError {
  std::string message;
  std::optional<token::Token_t> token;
  std::string ToString() const;
};

struct UserCommandMetadata {
  std::string name;
  int source_entry = -1;
  int token_line = -1;
  std::vector<Type> args;

  bool operator==(const UserCommandMetadata&) const = default;
};

class Recompiler {
 public:
  explicit Recompiler(std::shared_ptr<serilang::GarbageCollector> gc);
  ~Recompiler();

  inline bool Ok() const { return errors_.empty(); }
  inline std::span<const CompileError> GetErrors() const { return errors_; }
  inline std::span<const UserCommandMetadata> GetUserCommands() const {
    return user_commands_;
  }
  const std::unordered_map<int, int>& GetZlabels() const {
    return zlabel_lines_;
  }
  const std::unordered_map<int, int>& GetLabels() const { return label_lines_; }
  int GetMainLine() const { return main_line_; }
  inline void ClearErrors() { errors_.clear(); }
  inline serilang::Code* GetCode() const { return cur_chunk_; }

  void SetSceneId(int scene_id) { scene_id_ = scene_id; }
  void Compile(const std::vector<Parser::ParsedToken>& tokens);

 private:
  void Gen(token::Token_t tok, int lineno = -1);
  void Finish();

  // Data members
  std::shared_ptr<serilang::GarbageCollector> gc_;
  serilang::Code* cur_chunk_ = nullptr;
  std::size_t initial_jump_site_ = 0;
  std::size_t main_entry_ = 0;
  bool is_finalized_ = false;
  int main_line_ = 1;
  std::vector<std::size_t> return_sites_;
  std::unordered_map<int, int> label_lines_;
  std::unordered_map<int, int> zlabel_lines_;

  std::optional<int> scene_id_;
  std::vector<UserCommandMetadata> user_commands_;

 public:
  serilang::Module* module_ = nullptr;
  bool is_debug_ = false;
  bool trace_enabled_ = false;

 private:
  // Error handling
  std::vector<CompileError> errors_;
  inline void AddError(std::string msg,
                       std::optional<token::Token_t> tok = std::nullopt) {
    errors_.emplace_back(std::move(msg), std::move(tok));
  }

  // Emit helpers
  template <typename T>
  inline void emit(T ins) {
    cur_chunk_->Append(std::move(ins));
  }
  inline std::size_t code_size() const { return cur_chunk_->code.size(); }
  void emit_current_chunk();
  // Banks have already been pushed; retain the result/error while releasing
  // them on either exit from the call, including after suspension.
  void emit_banked_call(uint32_t argument_count);

  // Constant pool
  std::unordered_map<serilang::Value, uint32_t> const_pool_;
  uint32_t constant(serilang::Value v);
  uint32_t emit_const_nil();
  uint32_t emit_const(int v);
  uint32_t emit_const(std::string v);
  uint32_t intern_name(std::string v);

  // Fast locals
  std::vector<uint16_t> tvar_slots_;
  uint16_t variable_fast_slot(int id);
  uint16_t curcall_fast_slot(int id);

  void emit_store_global(std::string id);
  void emit_load_global(std::string id);
  void emit_init_value(Type type, int size = 0);

  void patch(std::size_t site, std::size_t target);

 private:
  void emit_val(const Value& v);

  // Token codegen
  void emit_tok(const token::ElmAlias& tk);
  void emit_tok(const token::Command& tk);
  void emit_tok(const token::Name& tk);
  void emit_tok(const token::Textout& tk);
  void emit_tok(const token::GetProperty& tk);
  void emit_tok(const token::Operate1& tk);
  void emit_tok(const token::Operate2& tk);
  void emit_tok(const token::Label& tk);
  void emit_tok(const token::Zlabel& tk);
  void emit_tok(const token::Goto& tk);
  void emit_tok(const token::GotoIf& tk);
  void emit_tok(const token::Gosub& tk);
  void emit_tok(const token::Assign& tk);
  void emit_tok(const token::Duplicate& tk);
  void emit_tok(const token::Subroutine& tk);
  void emit_tok(const token::LocalVar& tk);
  void emit_tok(const token::Return& tk);
  void emit_tok(const token::Eof& tk);

  // Element/AccessChain codegen
  void emit_elm(const elm::AccessChain& e, const Value* assign = nullptr);
  void emit_elm_root(const std::monostate& r);
  void emit_elm_root(const elm::Usrcmd& r);
  void emit_elm_root(const elm::Usrprop& r);
  void emit_elm_root(const elm::Arg& r);
  void emit_elm_root(const elm::Farcall& r);
  void emit_elm_root(const elm::Jump& r);
  void emit_elm_node(const elm::Member& nd);
  void emit_elm_node(const elm::Call& nd);
  void emit_elm_node(const elm::Subscript& nd);
  void emit_elm_node(const elm::SetMember& nd);
};

/*
Recompiler generated code:

  Jump bootstrap

dispatch_entry:
  __builtin_init_call(entry, arguments)
  Goto entry[0]

  ... compiled code for every scene token ...
  ... banked call sites push banks and guard their own cleanup ...

  Push nil                  // EOF / fallthrough result
return_entry:
  Return

bootstrap:
  %%dispatch = MakeFunction(this_code, dispatch_entry,
                            one_parameter, variadic)
  Return nil

*/

}  // namespace libsiglus
