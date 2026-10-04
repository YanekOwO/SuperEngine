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

#include "vm/vm.hpp"

#include "machine/op.hpp"
#include "utilities/expected.hpp"
#include "vm/call_frame.hpp"
#include "vm/dict.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/gc.hpp"
#include "vm/instruction.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"
#include "vm/operator_protocol.hpp"
#include "vm/primops.hpp"
#include "vm/promise.hpp"
#include "vm/string.hpp"
#include "vm/value.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace serilang {

using helper::pop;
using helper::push;

namespace {

enum class ScriptDispatchResult { NotHandled, Handled, Error };

std::optional<Value> ResolveMagic(Instance* inst, std::string_view name) {
  if (!inst || name.empty())
    return std::nullopt;

  if (auto it = inst->fields.find(name); it != inst->fields.end())
    return it->second;

  if (auto it = inst->klass->memfns.find(name); it != inst->klass->memfns.end())
    return Value(it->second);

  if (auto it = inst->klass->fields.find(name); it != inst->klass->fields.end())
    return it->second;

  return std::nullopt;
}

std::optional<Value> ResolveMagic(NativeInstance* inst, std::string_view name) {
  if (!inst || name.empty())
    return std::nullopt;

  if (auto it = inst->fields.find(name); it != inst->fields.end())
    return it->second;

  if (auto it = inst->klass->methods.find(name);
      it != inst->klass->methods.end())
    return it->second;

  return std::nullopt;
}

ScriptDispatchResult CallMagicUnary(VM& vm,
                                    Fiber& f,
                                    Value callee,
                                    Value self) {
  const size_t base = f.op_stack.size();
  f.op_stack.emplace_back(std::move(callee));
  f.op_stack.emplace_back(std::move(self));
  try {
    f.op_stack[base].Call(vm, f, 1, 0);
  } catch (RuntimeError& e) {
    f.op_stack.resize(base);
    push(f.op_stack, nil);
    vm.Error(f, e.message());
    return ScriptDispatchResult::Error;
  }
  return ScriptDispatchResult::Handled;
}

ScriptDispatchResult CallMagicBinary(VM& vm,
                                     Fiber& f,
                                     Value callee,
                                     Value self,
                                     Value other) {
  const size_t base = f.op_stack.size();
  f.op_stack.emplace_back(std::move(callee));
  f.op_stack.emplace_back(std::move(self));
  f.op_stack.emplace_back(std::move(other));
  try {
    f.op_stack[base].Call(vm, f, 2, 0);
  } catch (RuntimeError& e) {
    f.op_stack.resize(base);
    push(f.op_stack, nil);
    vm.Error(f, e.message());
    return ScriptDispatchResult::Error;
  }
  return ScriptDispatchResult::Handled;
}

ScriptDispatchResult TryDispatchUnaryMagic(VM& vm,
                                           Fiber& f,
                                           Op op,
                                           Value& operand) {
  using namespace opproto;

  const std::string_view name = UnaryMagic(op);
  if (name.empty())
    return ScriptDispatchResult::NotHandled;

  if (auto* inst = operand.Get_if<Instance>()) {
    if (auto callee = ResolveMagic(inst, name))
      return CallMagicUnary(vm, f, std::move(*callee), Value(inst));
  }

  if (auto* ninst = operand.Get_if<NativeInstance>()) {
    if (auto callee = ResolveMagic(ninst, name))
      return CallMagicUnary(vm, f, std::move(*callee), Value(ninst));
  }

  return ScriptDispatchResult::NotHandled;
}

ScriptDispatchResult TryDispatchBinaryMagic(VM& vm,
                                            Fiber& f,
                                            Op op,
                                            Value& lhs,
                                            Value& rhs) {
  using namespace opproto;

  const auto names = BinaryMagic(op);
  if (names.lhs.empty() && names.rhs.empty())
    return ScriptDispatchResult::NotHandled;

  if (!names.inplace.empty()) {
    // TODO: when augmented assignment information reaches the VM, dispatch
    //       __iop__ here. For now fall back to __op__/__rop__.
  }

  if (!names.lhs.empty()) {
    if (auto* inst = lhs.Get_if<Instance>()) {
      if (auto callee = ResolveMagic(inst, names.lhs))
        return CallMagicBinary(vm, f, std::move(*callee), Value(inst),
                               std::move(rhs));
    }
    if (auto* ninst = lhs.Get_if<NativeInstance>()) {
      if (auto callee = ResolveMagic(ninst, names.lhs))
        return CallMagicBinary(vm, f, std::move(*callee), Value(ninst),
                               std::move(rhs));
    }
  }

  if (!names.rhs.empty()) {
    if (auto* inst = rhs.Get_if<Instance>()) {
      if (auto callee = ResolveMagic(inst, names.rhs))
        return CallMagicBinary(vm, f, std::move(*callee), Value(inst),
                               std::move(lhs));
    }
    if (auto* ninst = rhs.Get_if<NativeInstance>()) {
      if (auto callee = ResolveMagic(ninst, names.rhs))
        return CallMagicBinary(vm, f, std::move(*callee), Value(ninst),
                               std::move(lhs));
    }
  }

  return ScriptDispatchResult::NotHandled;
}

}  // namespace

// -----------------------------------------------------------------------
// class VM
VM::VM(std::shared_ptr<GarbageCollector> gc)
    : gc_(gc),
      globals_(std::make_shared<std::unordered_map<std::string, Value>>()),
      builtins_(std::make_shared<std::unordered_map<std::string, Value>>()) {}

VM::VM(std::shared_ptr<GarbageCollector> gc,
       std::unordered_map<std::string, Value> globals,
       std::unordered_map<std::string, Value> builtins)
    : gc_(gc),
      globals_(std::make_shared<std::unordered_map<std::string, Value>>(
          std::move(globals))),
      builtins_(std::make_shared<std::unordered_map<std::string, Value>>(
          std::move(builtins))) {}

Value VM::Evaluate(Code* chunk) {
  Fiber* f = AddFiber(chunk);
  f->state = FiberState::Running;
  return Run();
}

Fiber* VM::AddFiber(Code* chunk) {
  Function* fn = gc_->Allocate<Function>(chunk);
  Fiber* fiber = gc_->Allocate<Fiber>();
  push(fiber->op_stack, Value(fn));
  fn->Call(*this, *fiber, 0, 0);
  fibres_.push_back(fiber);
  scheduler_.PushTask(fiber);
  return fiber;
}

void VM::CollectGarbage() {
  gc_->UnmarkAll();
  GCVisitor collector(*gc_);
  collector.MarkSub(last_);
  collector.MarkSub(main_fiber_);
  for (auto& [k, v] : *globals_)
    collector.MarkSub(v);
  for (auto& [k, v] : *builtins_)
    collector.MarkSub(v);
  for (auto& it : fibres_)
    collector.MarkSub(it);
  for (auto& [name, mod] : module_cache_)
    collector.MarkSub(mod);
  gc_->Sweep();
}

Value VM::AddTrack(TempValue&& t) { return gc_->TrackValue(std::move(t)); }

Value VM::Run() {
  // Seed: enqueue any New fibers so they can be processed.
  for (auto* f : fibres_)
    if (f->state == FiberState::New)
      scheduler_.PushTask(f);

  while (!scheduler_.IsIdle() || CountPendingPromises() > 0) {
    if (stop_requested_)
      break;
    scheduler_.DrainExpiredTimers();
    if (stop_requested_)
      break;

    if (Fiber* next = scheduler_.NextTask()) {
      if (next->state == FiberState::Running ||
          next->state == FiberState::New) {
        next->state = FiberState::Running;
        try {
          ExecuteFiber(next);
        } catch (const RuntimeError& e) {
          // Uncaught exception terminates fiber
          next->Kill(unexpected(e.message()));
        }

        // If still running and not finished, requeue for fairness.
        if (!stop_requested_ && next->state == FiberState::Running &&
            !next->frames.empty())
          scheduler_.PushTask(next);
      }
    } else {
      scheduler_.WaitForNext();
    }

    // run garbage collector periodically
    if (gc_threshold_ > 0 && gc_->AllocatedBytes() >= gc_threshold_) {
      CollectGarbage();
      gc_threshold_ *= 2;
    }

    // Remove dead fibers and record last result
    SweepDeadFibres();
  }

  // Final sweep of dead fibers to update last_
  SweepDeadFibres();

  return last_;
}

//------------------------------------------------------------------------------
// Exec helpers
//------------------------------------------------------------------------------
std::unordered_map<std::string, Value>* VM::GetNamespace(Fiber& f) {
  if (f.frames.empty())
    return nullptr;
  Function* fn = f.frames.back().fn;
  if (fn == nullptr || !fn->globals)
    return nullptr;
  return fn->globals.get();
}

void VM::Error(Fiber& f) {
  Value exc = pop(f.op_stack);
  while (true) {
    if (f.frames.empty()) {
      f.Kill(unexpected(exc.Str()));
      return;
    }

    CallFrame& fr = f.frames.back();
    if (!fr.handlers.empty()) {
      ExceptionHandler h = std::move(fr.handlers.back());
      fr.handlers.pop_back();
      f.op_stack.resize(h.stack_top);
      push(f.op_stack, std::move(exc));
      fr.ip = h.handler_ip;
      break;
    } else {
      f.op_stack.resize(fr.bp);
      f.frames.pop_back();
    }
  }
}

void VM::Error(Fiber& f, std::string exc) {
  String* excstr = gc_->Allocate<String>(std::move(exc));
  push(f.op_stack, Value(excstr));
  Error(f);
}

void VM::Return(Fiber& f) {
  auto& frame = f.frames.back();
  Value ret = f.op_stack.empty() ? Value() : std::move(f.op_stack.back());

  // shrink stack to base pointer + 1 (for return value)
  f.op_stack.resize(frame.bp + 1);
  f.frames.pop_back();

  if (f.frames.empty()) {
    f.Kill(ret);
  } else {
    // push return value back on stack
    f.op_stack.back() = std::move(ret);
  }
}

//------------------------------------------------------------------------------
// Core interpreter loop
//------------------------------------------------------------------------------

void VM::ExecuteFiber(Fiber* fib) {
  fib->state = FiberState::Running;

  for (int i = 0; i < 256 && !stop_requested_ && !fib->frames.empty(); ++i) {
    auto& frame = fib->frames.back();
    auto* chunk = frame.fn->chunk;
    auto& ip = frame.ip;

    if (ip >= chunk->code.size()) {
      Return(*fib);
      return;
    }

    try {
      switch (static_cast<OpCode>((*chunk)[ip++])) {
          //------------------------------------------------------------------
          // 0. Debug
          //------------------------------------------------------------------
        case OpCode::Nop: {
          const auto ins = chunk->Read<serilang::Nop>(ip);
          ip += sizeof(ins);
        } break;

        case OpCode::DebugValue: {
          const auto ins = chunk->Read<serilang::DebugValue>(ip);
          ip += sizeof(ins);

          [[maybe_unused]] Value val = pop(fib->op_stack);
          [[maybe_unused]] std::string desc = val.Desc();
        } break;

        //------------------------------------------------------------------
        // 1. Stack Manipulation
        //------------------------------------------------------------------
        case OpCode::Push: {
          const auto ins = chunk->Read<serilang::Push>(ip);
          ip += sizeof(ins);
          push(fib->op_stack, chunk->const_pool[ins.const_index]);
        } break;

        case OpCode::Dup: {
          const auto ins = chunk->Read<serilang::Dup>(ip);
          ip += sizeof(ins);
          push(fib->op_stack, fib->op_stack.end()[-ins.top_ofs - 1]);
        } break;

        case OpCode::Swap: {
          const auto ins = chunk->Read<serilang::Swap>(ip);
          ip += sizeof(ins);
          std::iter_swap(fib->op_stack.end() - 1, fib->op_stack.end() - 2);
        } break;

        case OpCode::Pop: {
          const auto ins = chunk->Read<serilang::Pop>(ip);
          ip += sizeof(ins);
          if (fib->op_stack.size() < ins.count) {
            Error(*fib, "stack underflow");
            return;
          }
          fib->op_stack.resize(fib->op_stack.size() - ins.count);
        } break;

        //------------------------------------------------------------------
        // 2. Unary / Binary
        //------------------------------------------------------------------
        case OpCode::UnaryOp: {
          const auto ins = chunk->Read<serilang::UnaryOp>(ip);
          ip += sizeof(ins);
          auto v = pop(fib->op_stack);

          switch (TryDispatchUnaryMagic(*this, *fib, ins.op, v)) {
            case ScriptDispatchResult::Handled:
              break;
            case ScriptDispatchResult::Error:
              return;
            case ScriptDispatchResult::NotHandled: {
              try {
                std::optional<TempValue> result =
                    primops::EvaluateUnary(ins.op, v);
                if (!result)
                  throw UndefinedOperator(ins.op, {v});
                push(fib->op_stack, AddTrack(std::move(*result)));

              } catch (RuntimeError& e) {
                Error(*fib, e.message());
                return;
              }

              break;
            }
          }
        } break;

        case OpCode::BinaryOp: {
          const auto ins = chunk->Read<serilang::BinaryOp>(ip);
          ip += sizeof(ins);
          auto rhs = pop(fib->op_stack);
          auto lhs = pop(fib->op_stack);

          switch (TryDispatchBinaryMagic(*this, *fib, ins.op, lhs, rhs)) {
            case ScriptDispatchResult::Handled:
              break;
            case ScriptDispatchResult::Error:
              return;
            case ScriptDispatchResult::NotHandled: {
              try {
                std::optional<TempValue> result =
                    primops::EvaluateBinary(ins.op, lhs, rhs);
                if (!result)
                  throw UndefinedOperator(ins.op, {lhs, rhs});
                push(fib->op_stack, AddTrack(std::move(*result)));
              } catch (RuntimeError& e) {
                Error(*fib, e.message());
                return;
              }

              break;
            }
          }
        } break;

        //------------------------------------------------------------------
        // 3. Locals / globals / upvalues
        //------------------------------------------------------------------
        case OpCode::LoadFast: {
          const auto ins = chunk->Read<serilang::LoadFast>(ip);
          ip += sizeof(ins);
          const auto slot = static_cast<std::size_t>(ins.slot);
          std::optional<Value>& fast_local = fib->GetFastLocal(slot);
          if (!fast_local.has_value()) {
            Error(*fib,
                  std::format("NameError: variable {} is unbound (slot={})",
                              fib->GetFastLocalName(slot), slot));
            return;  // switch -> LoadFast
          }

          push(fib->op_stack, fast_local.value());
        } break;

        case OpCode::StoreFast: {
          const auto ins = chunk->Read<serilang::StoreFast>(ip);
          ip += sizeof(ins);
          const auto slot = static_cast<std::size_t>(ins.slot);
          std::optional<Value>& fast_local = fib->GetFastLocal(slot);
          fast_local = pop(fib->op_stack);
        } break;

        case OpCode::LoadGlobal: {
          const auto ins = chunk->Read<serilang::LoadGlobal>(ip);
          ip += sizeof(ins);

          const String* name =
              chunk->const_pool[ins.name_index].template Get_if<const String>();
          const std::string& name_str = name->str_;

          auto* d = GetNamespace(*fib);
          if (d == nullptr || !d->contains(name_str))
            d = globals_.get();
          if (d == nullptr || !d->contains(name_str))
            d = builtins_.get();
          auto it = d->find(name_str);
          if (it == d->cend()) {
            Error(*fib, "NameError: '" + name_str + "' is not defined");
            return;
          }
          push(fib->op_stack, it->second);
        } break;

        case OpCode::StoreGlobal: {
          const auto ins = chunk->Read<serilang::StoreGlobal>(ip);
          ip += sizeof(ins);

          const String* name =
              chunk->const_pool[ins.name_index].template Get_if<const String>();
          const std::string& name_str = name->str_;

          Value val = pop(fib->op_stack);

          auto* dst = GetNamespace(*fib);
          if (dst == nullptr)
            dst = globals_.get();
          (*dst)[name_str] = std::move(val);
        } break;

        //------------------------------------------------------------------
        // 4. Control flow
        //------------------------------------------------------------------
        case OpCode::Jump: {
          const auto ins = chunk->Read<serilang::Jump>(ip);
          ip += sizeof(ins);
          frame.ip += ins.offset;
        } break;

        case OpCode::JumpIfTrue: {
          const auto ins = chunk->Read<serilang::JumpIfTrue>(ip);
          ip += sizeof(ins);
          auto cond = pop(fib->op_stack);
          if (cond.IsTruthy())
            frame.ip += ins.offset;
        } break;

        case OpCode::JumpIfFalse: {
          const auto ins = chunk->Read<serilang::JumpIfFalse>(ip);
          ip += sizeof(ins);
          auto cond = pop(fib->op_stack);
          if (!cond.IsTruthy())
            frame.ip += ins.offset;
        } break;

        case OpCode::Return: {
          const auto ins = chunk->Read<serilang::Return>(ip);
          ip += sizeof(ins);
          Return(*fib);
        }
          return;  // unwind to caller

        //------------------------------------------------------------------
        // 5. Function calls
        //------------------------------------------------------------------
        case OpCode::MakeFunction: {
          const auto ins = chunk->Read<serilang::MakeFunction>(ip);
          ip += sizeof(ins);
          std::unordered_map<std::string, size_t> param_index;
          param_index.reserve(ins.nparam);
          size_t name_idx = fib->op_stack.size() - ins.nparam;
          for (uint32_t i = 0; i < ins.nparam; ++i) {
            param_index.emplace(
                fib->op_stack[name_idx + i].Get_if<String>()->str_, i);
          }
          fib->op_stack.resize(name_idx);

          Code* chunk =
              fib->op_stack.end()[-1 - static_cast<size_t>(ins.ndefault) * 2]
                  .Get<Code*>();
          std::unordered_map<size_t, Value> defaults;
          defaults.reserve(ins.ndefault);
          for (size_t i = fib->op_stack.size() - ins.ndefault * 2;
               i < fib->op_stack.size(); i += 2) {
            std::string const& k = fib->op_stack[i].Get_if<String>()->str_;
            Value v = std::move(fib->op_stack[i + 1]);
            defaults.emplace(param_index.at(k), std::move(v));
          }
          fib->op_stack.resize(fib->op_stack.size() - ins.ndefault * 2);

          Function* fn = gc_->Allocate<Function>(chunk);
          ArgumentList& fnargs = fn->arglist;
          if (fib->frames.back().fn != nullptr &&
              fib->frames.back().fn->globals)
            fn->globals = fib->frames.back().fn->globals;
          else
            fn->globals = globals_;
          fn->entry = ins.entry;
          fnargs.nparam = ins.nparam;
          fnargs.param_index = std::move(param_index);
          fnargs.defaults = std::move(defaults);
          fnargs.has_vararg = ins.has_vararg;
          fnargs.has_kwarg = ins.has_kwarg;

          fib->op_stack.back() = Value(fn);
        } break;

        case OpCode::Call: {
          const auto ins = chunk->Read<serilang::Call>(ip);
          ip += sizeof(ins);
          Value& callee =
              fib->op_stack.end()[-static_cast<size_t>(ins.argcnt) -
                                  2 * static_cast<size_t>(ins.kwargcnt) - 1];
          callee.Call(*this, *fib, ins.argcnt, ins.kwargcnt);
        } break;

        //------------------------------------------------------------------
        // 6. Object ops
        //------------------------------------------------------------------
        case OpCode::MakeList: {
          const auto ins = chunk->Read<serilang::MakeList>(ip);
          ip += sizeof(ins);
          std::vector<Value> elms(
              std::make_move_iterator(fib->op_stack.end() - ins.nelms),
              std::make_move_iterator(fib->op_stack.end()));
          fib->op_stack.resize(fib->op_stack.size() - ins.nelms);
          fib->op_stack.emplace_back(gc_->Allocate<List>(std::move(elms)));
        } break;

        case OpCode::MakeDict: {
          const auto ins = chunk->Read<serilang::MakeDict>(ip);
          ip += sizeof(ins);
          Dict::map_t elms;
          std::vector<Value> order;
          order.reserve(ins.nelms);
          for (size_t i = fib->op_stack.size() - 2 * ins.nelms;
               i < fib->op_stack.size(); i += 2) {
            const auto& key = fib->op_stack[i];
            Value val = std::move(fib->op_stack[i + 1]);
            auto it = elms.find(key);
            if (it == elms.end()) {
              order.push_back(key);
              elms.emplace(key, std::move(val));
            } else {
              it->second = std::move(val);
            }
          }
          fib->op_stack.resize(fib->op_stack.size() - 2 * ins.nelms);
          fib->op_stack.emplace_back(
              gc_->Allocate<Dict>(std::move(elms), std::move(order)));
        } break;

        case OpCode::MakeClass: {
          const auto ins = chunk->Read<serilang::MakeClass>(ip);
          ip += sizeof(ins);
          auto klass = gc_->Allocate<Class>();
          klass->name =
              chunk->const_pool[ins.name_index].Get_if<String>()->str_;
          for (int i = 0; i < ins.nstaticfn; i++) {
            Function* fn = pop(fib->op_stack).template Get<Function*>();
            std::string name = pop(fib->op_stack).Get_if<String>()->str_;
            klass->fields.try_emplace(std::move(name), fn);
          }
          for (int i = 0; i < ins.nmemfn; i++) {
            Function* fn = pop(fib->op_stack).template Get<Function*>();
            std::string name = pop(fib->op_stack).Get_if<String>()->str_;
            klass->memfns.try_emplace(std::move(name), fn);
          }

          push(fib->op_stack, Value(klass));
        } break;

        case OpCode::GetField: {
          const auto ins = chunk->Read<serilang::GetField>(ip);
          ip += sizeof(ins);
          Value receiver = pop(fib->op_stack);
          const std::string& name =
              chunk->const_pool[ins.name_index].Get_if<String>()->str_;
          TempValue result = nil;
          result = receiver.Member(name);
          push(fib->op_stack, AddTrack(std::move(result)));
        } break;

        case OpCode::SetField: {
          const auto ins = chunk->Read<serilang::SetField>(ip);
          ip += sizeof(ins);
          Value val = pop(fib->op_stack);
          Value receiver = pop(fib->op_stack);
          std::string const& name =
              chunk->const_pool[ins.name_index].Get_if<String>()->str_;
          receiver.SetMember(name, std::move(val));
        } break;

        case OpCode::GetItem: {
          const auto ins = chunk->Read<serilang::GetItem>(ip);
          ip += sizeof(ins);

          Value& receiver = fib->op_stack.end()[-2];
          receiver.GetItem(*this, *fib);
        } break;
        case OpCode::SetItem: {
          const auto ins = chunk->Read<serilang::SetItem>(ip);
          ip += sizeof(ins);

          Value& receiver = fib->op_stack.end()[-3];
          receiver.SetItem(*this, *fib);
        } break;

        //------------------------------------------------------------------
        // 7. Coroutines
        //------------------------------------------------------------------
        case OpCode::MakeFiber: {
          const auto ins = chunk->Read<serilang::MakeFiber>(ip);
          ip += sizeof(ins);
          size_t base =
              fib->op_stack.size() - ins.argcnt - 2 * ins.kwargcnt - 1;
          Value fn = fib->op_stack[base];

          Fiber* f = gc_->Allocate<Fiber>(/*reserve=*/16 + ins.argcnt +
                                          2 * ins.kwargcnt);
          std::move(fib->op_stack.begin() + base, fib->op_stack.end(),
                    std::back_inserter(f->op_stack));
          fib->op_stack.resize(base);

          fn.Call(*this, *f, ins.argcnt, ins.kwargcnt);

          push(fib->op_stack, Value(f));
          fibres_.push_back(f);
          scheduler_.PushTask(f);
        } break;

        case OpCode::Await: {
          const auto ins = chunk->Read<serilang::Await>(ip);
          ip += sizeof(ins);
          Value awaiter(fib);
          Value awaited = pop(fib->op_stack);

          fib->state = FiberState::Suspended;
          auto cb = [vm = this, fib, awaited_fib = awaited.Get_if<Fiber>()](
                        const expected<Value, std::string>& result) {
            if (result.has_value())
              push(fib->op_stack, result.value());
            else
              vm->Error(*fib, result.error());
            vm->scheduler_.PushMicroTask(fib);

            if (awaited_fib && awaited_fib->state == FiberState::Suspended)
              vm->scheduler_.PushTask(awaited_fib);
          };

          bool stopped = Await(awaiter, awaited, std::move(cb));
          if (stopped)
            return;  // switch -> await

        } break;

        case OpCode::Yield: {
          const auto ins = chunk->Read<serilang::Yield>(ip);
          ip += sizeof(ins);

          Value val = pop(fib->op_stack);
          fib->Yield(std::move(val));
        }
          return;  // switch -> yield

        //------------------------------------------------------------------
        // 8. Exceptions
        //------------------------------------------------------------------
        case OpCode::Throw: {
          const auto ins = chunk->Read<serilang::Throw>(ip);
          ip += sizeof(ins);
          Error(*fib);
          return;
        } break;

        case OpCode::TryBegin: {
          const auto ins = chunk->Read<serilang::TryBegin>(ip);
          ip += sizeof(ins);
          frame.handlers.push_back(
              {static_cast<uint32_t>(ip + ins.handler_rel_ofs),
               fib->op_stack.size()});
        } break;

        case OpCode::TryEnd: {
          const auto ins = chunk->Read<serilang::TryEnd>(ip);
          ip += sizeof(ins);
          if (!frame.handlers.empty())
            frame.handlers.pop_back();
        } break;

          //------------------------------------------------------------------
          // 9. Unimplemented
          //------------------------------------------------------------------
        default:
          throw std::runtime_error(
              "VM internal error: unimplemented instruction at bytecode "
              "offset " +
              std::to_string(ip - 1));
          break;
      }
    } catch (const RuntimeError& e) {
      // Script-visible runtime failures are converted into VM exceptions.
      Error(*fib, e.message());
      return;
    }

    if (stop_requested_ || fib->state != FiberState::Running)
      break;
  }

  if (!stop_requested_ && fib->state != FiberState::Dead && fib->frames.empty())
    fib->Kill(nil);
}

bool VM::Await(Value& awaiter,
               Value& awaited,
               std::function<void(const expected<Value, std::string>&)> waker) {
  assert(waker);

  std::shared_ptr<Promise> promise = nullptr;
  Fiber* await_fib = nullptr;

  if (Fiber* tf = awaited.Get_if<Fiber>()) {
    promise = tf->completion_promise;
    await_fib = tf;
  }
  if (Future* ft = awaited.Get_if<Future>()) {
    promise = ft->promise;
  }

  if (promise == nullptr) {
    waker(unexpected("object is not awaitable: " + awaited.Desc()));
    return false;
  }

  bool is_pending = promise->AddWaker(std::move(waker));
  if (!is_pending)  // ready
    return false;

  // pending
  if (promise->status == Promise::Status::New) {
    promise->status = Promise::Status::Pending;
    if (promise->initial_await)
      promise->initial_await(*this, awaiter, awaited);
    pending_promises_.emplace_back(promise);
  }
  scheduler_.PushMicroTask(await_fib);

  return true;
}

void VM::TrackPendingPromise(std::shared_ptr<Promise> promise) {
  if (!promise || promise->HasResult())
    return;

  if (promise->status == Promise::Status::New)
    promise->status = Promise::Status::Pending;

  auto already_tracked = std::ranges::any_of(
      pending_promises_,
      [&promise](const auto& tracked) { return tracked.lock() == promise; });
  if (!already_tracked)
    pending_promises_.emplace_back(std::move(promise));
}

void VM::SweepDeadFibres() {
  std::erase_if(fibres_, [this](Fiber* f) {
    if (f->state != FiberState::Dead)
      return false;

    std::shared_ptr<Promise> promise = f->completion_promise;
    bool has_pending_result = promise->GetWakeCount() == 0;
    if (has_pending_result) {
      if (!promise->result.has_value()) [[unlikely]]
        last_ = nil;  // unreachable
      else if (promise->result->has_value())
        last_ = promise->result->value();  // success case
      else
        throw UnhandledError(promise->result->error());  // error case
    }
    return true;
  });
}

std::size_t VM::CountPendingPromises() {
  std::erase_if(pending_promises_, [](std::weak_ptr<Promise> p) {
    std::shared_ptr<Promise> promise = p.lock();
    if (!promise)
      return true;
    return promise->HasResult();
  });

  return pending_promises_.size();
}

}  // namespace serilang
