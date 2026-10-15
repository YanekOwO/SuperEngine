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

#include "vm/disassembler.hpp"

#include "vm/function.hpp"
#include "vm/instruction.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"

#include <format>
#include <iomanip>

namespace serilang {

Disassembler::Disassembler(size_t indent) : indent_size_(indent) {}

void Disassembler::PrintIns(Code& chunk,
                            size_t& ip,
                            const std::string& indent) {
  using std::left;
  using std::right;
  using std::setw;

  // helpers -------------------------------------------------------
  const auto emit_addr = [&](size_t addr) {
    static constexpr int COL_ADDR = 4;
    out_ << indent << right << setw(COL_ADDR) << addr << "  ";
  };
  const auto emit_mnemonic = [&](std::string_view m) {
    static constexpr int COL_MNEMON = 12;
    out_ << left << setw(COL_MNEMON) << m;
  };
  const auto emit_operand = [&](auto v) {
    static constexpr int COL_OPERND = 10;
    out_ << right << setw(COL_OPERND) << v;
  };
  const auto string_at = [&](size_t index) {
    return index < chunk.const_pool.size()
               ? chunk.const_pool[index].template Get_if<String>()->str_
               : std::string("???");
  };

  // header (address column) --------------------------------------
  emit_addr(ip);

  // body ----------------------------------------------------------
  switch (static_cast<OpCode>(chunk[ip++])) {
    // ── 0. debug ────────────────────────────────────────────
    case OpCode::Nop: {
      const auto ins = chunk.Read<Nop>(ip);
      ip += sizeof(ins);
      emit_mnemonic("NOP");
    } break;
    case OpCode::DebugValue: {
      const auto ins = chunk.Read<DebugValue>(ip);
      ip += sizeof(ins);
      emit_mnemonic("DBG_VAL");
    } break;

    // ── 1. stack manipulation ───────────────────────────────
    case OpCode::Push: {
      const auto ins = chunk.Read<Push>(ip);
      ip += sizeof(ins);
      emit_mnemonic("PUSH");
      emit_operand(ins.const_index);
      if (ins.const_index < chunk.const_pool.size())
        out_ << "  ; " << chunk.const_pool[ins.const_index].Desc();
    } break;
    case OpCode::Dup: {
      const auto ins = chunk.Read<Dup>(ip);
      ip += sizeof(ins);
      emit_mnemonic("DUP");
      emit_operand(static_cast<uint32_t>(ins.top_ofs));
    } break;
    case OpCode::Swap: {
      const auto ins = chunk.Read<Swap>(ip);
      ip += sizeof(ins);
      emit_mnemonic("SWAP");
    } break;
    case OpCode::Pop: {
      const auto ins = chunk.Read<Pop>(ip);
      ip += sizeof(ins);
      emit_mnemonic("POP");
      emit_operand(+ins.count);
    } break;

      // ── 2. arithmetic / logic ───────────────────────────────
    case OpCode::UnaryOp: {
      const auto ins = chunk.Read<UnaryOp>(ip);
      ip += sizeof(ins);
      emit_mnemonic("UNARY");
      emit_operand(ToString(ins.op));
    } break;
    case OpCode::BinaryOp: {
      const auto ins = chunk.Read<BinaryOp>(ip);
      ip += sizeof(ins);
      emit_mnemonic("BINARY");
      emit_operand(ToString(ins.op));
    } break;

      // ── 3. locals / globals / up-values ─────────────────────
    case OpCode::LoadFast: {
      const auto ins = chunk.Read<LoadFast>(ip);
      ip += sizeof(ins);
      emit_mnemonic("LD_FAST");
      emit_operand(+ins.slot);
      out_ << "  ; " << chunk.fast_locals.at(ins.slot);
    } break;
    case OpCode::StoreFast: {
      const auto ins = chunk.Read<StoreFast>(ip);
      ip += sizeof(ins);
      emit_mnemonic("ST_FAST");
      emit_operand(+ins.slot);
      out_ << "  ; " << chunk.fast_locals.at(ins.slot);
    } break;
    case OpCode::LoadGlobal: {
      const auto ins = chunk.Read<LoadGlobal>(ip);
      ip += sizeof(ins);
      emit_mnemonic("LD_GLOBAL");
      emit_operand(ins.name_index);
      out_ << "  ; " << string_at(ins.name_index);
    } break;
    case OpCode::StoreGlobal: {
      const auto ins = chunk.Read<StoreGlobal>(ip);
      ip += sizeof(ins);
      emit_mnemonic("ST_GLOBAL");
      emit_operand(ins.name_index);
      out_ << "  ; " << string_at(ins.name_index);
    } break;
    case OpCode::LoadUpvalue: {
      const auto ins = chunk.Read<LoadUpvalue>(ip);
      ip += sizeof(ins);
      emit_mnemonic("LD_UPVAL");
      emit_operand(+ins.slot);
    } break;
    case OpCode::StoreUpvalue: {
      const auto ins = chunk.Read<StoreUpvalue>(ip);
      ip += sizeof(ins);
      emit_mnemonic("ST_UPVAL");
      emit_operand(+ins.slot);
    } break;
    case OpCode::CloseUpvalues: {
      const auto ins = chunk.Read<CloseUpvalues>(ip);
      ip += sizeof(ins);
      emit_mnemonic("CLOSE_UPS");
      emit_operand(+ins.from_slot);
    } break;

      // ── 4. control-flow ─────────────────────────────────────
    case OpCode::Jump: {
      const auto ins = chunk.Read<Jump>(ip);
      ip += sizeof(ins);
      long tgt = static_cast<long>(ip) + ins.offset;
      emit_mnemonic("JUMP");
      emit_operand((ins.offset >= 0 ? "+" : "") + std::to_string(ins.offset));
      out_ << " -> " << tgt;
    } break;
    case OpCode::JumpIfTrue: {
      const auto ins = chunk.Read<JumpIfTrue>(ip);
      ip += sizeof(ins);
      long tgt = static_cast<long>(ip) + ins.offset;
      emit_mnemonic("JIF_TRUE");
      emit_operand((ins.offset >= 0 ? "+" : "") + std::to_string(ins.offset));
      out_ << " -> " << tgt;
    } break;
    case OpCode::JumpIfFalse: {
      const auto ins = chunk.Read<JumpIfFalse>(ip);
      ip += sizeof(ins);
      long tgt = static_cast<long>(ip) + ins.offset;
      emit_mnemonic("JIF_FALSE");
      emit_operand((ins.offset >= 0 ? "+" : "") + std::to_string(ins.offset));
      out_ << " -> " << tgt;
    } break;
    case OpCode::Return: {
      const auto ins = chunk.Read<Return>(ip);
      ip += sizeof(ins);
      emit_mnemonic("RETURN");
    } break;

      // ── 5. functions & calls ────────────────────────────────
    case OpCode::MakeFunction: {
      const auto ins = chunk.Read<MakeFunction>(ip);
      ip += sizeof(ins);
      emit_mnemonic("MAKE_FUNCION");
      emit_operand("");
      out_ << "entry=" << ins.entry << "  nargs=" << ins.nparam
           << "  nparam=" << ins.nparam;
    } break;
    case OpCode::Call: {
      const auto ins = chunk.Read<Call>(ip);
      ip += sizeof(ins);
      emit_mnemonic("CALL");
      emit_operand("");
      out_ << "nargs=" << ins.argcnt << "  nkwargs=" << ins.kwargcnt;
    } break;

      // ── 6. objects / classes ───────────────────────────────
    case OpCode::MakeList: {
      const auto ins = chunk.Read<MakeList>(ip);
      ip += sizeof(ins);
      emit_mnemonic("MAKE_LIST");
      emit_operand("");
      out_ << "nelms=" << ins.nelms;
    } break;
    case OpCode::MakeDict: {
      const auto ins = chunk.Read<MakeDict>(ip);
      ip += sizeof(ins);
      emit_mnemonic("MAKE_DICT");
      emit_operand("");
      out_ << "nelms=" << ins.nelms;
    } break;
    case OpCode::MakeClass: {
      const auto ins = chunk.Read<MakeClass>(ip);
      ip += sizeof(ins);
      emit_mnemonic("MAKE_CLASS");
      emit_operand("");
      out_ << std::format("name={}({})  nmem={}  nstatic={}  nproperty={}",
                          ins.name_index, string_at(ins.name_index), ins.nmemfn,
                          ins.nstaticfn, ins.nproperty);
    } break;
    case OpCode::GetField: {
      const auto ins = chunk.Read<GetField>(ip);
      ip += sizeof(ins);
      emit_mnemonic("GET_FIELD");
      emit_operand(ins.name_index);
      out_ << "  ; " << string_at(ins.name_index);
    } break;
    case OpCode::SetField: {
      const auto ins = chunk.Read<SetField>(ip);
      ip += sizeof(ins);
      emit_mnemonic("SET_FIELD");
      emit_operand(ins.name_index);
      out_ << "  ; " << string_at(ins.name_index);

    } break;
    case OpCode::GetItem: {
      const auto ins = chunk.Read<GetItem>(ip);
      ip += sizeof(ins);
      emit_mnemonic("GET_ITEM");
    } break;
    case OpCode::SetItem: {
      const auto ins = chunk.Read<SetItem>(ip);
      ip += sizeof(ins);
      emit_mnemonic("SET_ITEM");
    } break;

      // ── 7. coroutines / fibers ──────────────────────────────
    case OpCode::MakeFiber: {
      const auto ins = chunk.Read<MakeFiber>(ip);
      ip += sizeof(ins);
      emit_mnemonic("MAKE_FIBER");
      emit_operand("");
      out_ << "  narg=" << ins.argcnt << ",nkwarg=" << ins.kwargcnt;
    } break;
    case OpCode::Await: {
      const auto ins = chunk.Read<Await>(ip);
      ip += sizeof(ins);
      emit_mnemonic("AWAIT");
    } break;
    case OpCode::Yield: {
      const auto ins = chunk.Read<Yield>(ip);
      ip += sizeof(ins);
      emit_mnemonic("YIELD");
    } break;

      // ── 8. exceptions ──────────────────────────────────────
    case OpCode::Throw: {
      const auto ins = chunk.Read<Throw>(ip);
      ip += sizeof(ins);
      emit_mnemonic("THROW");
    } break;
    case OpCode::TryBegin: {
      const auto ins = chunk.Read<TryBegin>(ip);
      ip += sizeof(ins);
      long tgt = static_cast<long>(ip) + ins.handler_rel_ofs;
      emit_mnemonic("TRY_BEGIN");
      emit_operand("+" + std::to_string(ins.handler_rel_ofs));
      out_ << " -> " << tgt;
    } break;
    case OpCode::TryEnd: {
      const auto ins = chunk.Read<TryEnd>(ip);
      ip += sizeof(ins);
      emit_mnemonic("TRY_END");
    } break;

      // ── unknown op ─────────────────────────────────────────
    default:
      emit_mnemonic("<unknown op>");
      break;
  }

  out_ << '\n';
}

void Disassembler::DumpImpl(Code& chunk, const std::string& indent) {
  if (seen_.contains(&chunk))
    return;
  seen_.insert(&chunk);

  // --- top-level disassembly of this chunk -----------------------------
  for (std::size_t ip = 0; ip < chunk.code.size();)
    PrintIns(chunk, ip, indent);

  // --- dive into nested chunks found in the constant-pool --------------
  for (std::size_t idx = 0; idx < chunk.const_pool.size(); ++idx) {
    const std::string sub_indent = indent + std::string(indent_size_, ' ');
    Value& v = chunk.const_pool[idx];
    Code* subChunk = nullptr;

    if (auto fn = v.Get_if<Function>())
      subChunk = fn->chunk;
    if (auto c = v.Get_if<Code>())
      subChunk = c;

    if (subChunk) {
      out_ << '\n'
           << indent << "; ── nested chunk @const[" << idx << "] ───────────\n";
      DumpImpl(*subChunk, sub_indent);
    }
  }
}

std::string Disassembler::Dump(Code& chunk) {
  DumpImpl(chunk, "");
  std::string result = out_.str();

  out_.clear();
  out_.str("");
  seen_.clear();

  return result;
}

}  // namespace serilang
