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

#include <gtest/gtest.h>

#include "mock_clock.hpp"

#include "vm/dict.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/instruction.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"
#include "vm/promise.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <chrono>
#include <limits>
#include <utility>

namespace serilang_test {

using namespace serilang;

class VMTest : public ::testing::Test {
 protected:
  std::shared_ptr<GarbageCollector> gc;
  VM vm;
  VMTest() : gc(std::make_shared<GarbageCollector>()), vm(gc) {}

  template <typename T>
    requires std::constructible_from<Value, T>
  inline Value to_value(T&& param) {
    return Value(std::forward<T>(param));
  }
  inline Value to_value(const char* s) {
    String* str = gc->Allocate<String>(s);
    return Value(str);
  }

  // Small helper: wrapper to create Value array
  template <typename... Ts>
  inline std::vector<Value> value_vector(Ts&&... param) {
    return std::vector<Value>{to_value(std::forward<Ts>(param))...};
  }

  inline void append_ins(Code* chunk, std::initializer_list<Instruction> ins) {
    for (const auto& it : ins)
      chunk->Append(it);
  }

  Value CallMember(
      IObject* receiver,
      std::string_view item,
      const std::vector<Value>& pos = {},
      const std::vector<std::pair<std::string, Value>>& kwargs = {}) {
    TempValue method = receiver->GetMember(item);
    Value callee = gc->TrackValue(std::move(method));

    auto* fiber = gc->Allocate<Fiber>();
    fiber->op_stack.emplace_back(callee);
    for (const Value& arg : pos)
      fiber->op_stack.emplace_back(arg);
    for (const auto& [key, value] : kwargs) {
      fiber->op_stack.emplace_back(gc->Allocate<String>(key));
      fiber->op_stack.emplace_back(value);
    }

    callee.Call(vm, *fiber, static_cast<uint8_t>(pos.size()),
                static_cast<uint8_t>(kwargs.size()));

    EXPECT_EQ(fiber->op_stack.size(), 1u);
    return fiber->op_stack.back();
  }

  Value run_and_get(Code* chunk) { return vm.Evaluate(chunk); }

  Code* CatchPropertyAccess(Value receiver, bool assign = true) {
    auto* code = gc->Allocate<Code>();
    code->const_pool = value_vector(receiver, 5, "value", 77, 0);
    code->Append(Push{3});
    const size_t try_begin = code->code.size();
    code->Append(TryBegin{});
    code->Append(Push{0});
    if (assign)
      append_ins(code, {Push{1}, SetField{2}});
    else
      append_ins(code, {GetField{2}, Pop{}});
    append_ins(code, {TryEnd{}, Push{4}, Return{}});
    const size_t handler = code->code.size();
    append_ins(code, {Pop{}, Return{}});
    const size_t try_body = try_begin + 1 + sizeof(TryBegin);
    code->Write<int32_t>(try_begin + 1,
                         static_cast<int32_t>(handler - try_body));
    return code;
  }
};

TEST_F(VMTest, BinaryAdd) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(1.0, 2.0);
  append_ins(chunk, {Push{0}, Push{1}, BinaryOp{Op::Add}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 3.0);
}

TEST_F(VMTest, UnaryNeg) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(5.0);
  append_ins(chunk, {Push{0}, UnaryOp{Op::Sub}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, -5.0);
}

TEST_F(VMTest, DupAndSwap) {
  // Program:   (1 2) swap ⇒ (2 1) dup ⇒ (2 1 1) add,add ⇒ 2+1+1 = 4
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(1.0, 2.0);
  append_ins(chunk, {Push{0},  // 1
                     Push{1},  // 2
                     Swap{},   // 2 1
                     Dup{},    // 2 1 1
                     BinaryOp{Op::Add}, BinaryOp{Op::Add}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 4.0);
}

TEST_F(VMTest, StoreLoadFast) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(42.0);
  chunk->fast_locals = {"local0"};

  // bootstrap main-closure: we need one local slot
  //   local0 = 42;  return local0;
  append_ins(chunk, {Push{0}, StoreFast{0}, LoadFast{0}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 42.0);
}

TEST_F(VMTest, StoreLoadFastAtUint16MaxSlot) {
  constexpr auto kMaxSlot = std::numeric_limits<uint16_t>::max();

  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(99.0);
  chunk->fast_locals.resize(static_cast<std::size_t>(kMaxSlot) + 1);
  chunk->fast_locals.back() = "max_local";

  append_ins(chunk,
             {Push{0}, StoreFast{kMaxSlot}, LoadFast{kMaxSlot}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 99.0);
}

TEST_F(VMTest, FunctionCall) {
  // Layout:
  //  0  PUSH                 1  ; <code>
  //  5  MAKE_FUNCION          entry=13  nargs=0
  // 22  CALL                  nargs=0  nkwargs=0
  // 31  RETURN
  // 33  PUSH                 0  ; <double: 7.000000>
  // 38  RETURN
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(7.0, Value(chunk));
  append_ins(chunk, {Push(1), MakeFunction{.entry = 33}, Call{0, 0}, Return{},
                     Push{0}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 7.0);
}

//      if (1 < 2) push 222 else push 111
TEST_F(VMTest, ConditionalJump) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(1.0, 2.0, 111.0, 222.0);

  //  0 1 < 2 ?
  //  if-true  +2   (skip the ‘else’ push)
  //  push 222
  //  jump +1 (skip then-push)
  //  push 111
  //  return
  append_ins(chunk, {Push{0}, Push{1}, BinaryOp{Op::Less}, JumpIfFalse{10},
                     Push{3}, Jump{5}, Push{2}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 222.0);
}

TEST_F(VMTest, ReturnNil) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(std::monostate(), "2.unused");

  // return nil;
  append_ins(chunk, {Push{0}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, std::monostate());
}

TEST_F(VMTest, CallNative) {
  auto* chunk = gc->Allocate<Code>();

  int call_count = 0;
  auto fn = gc->Allocate<NativeFunction>(
      "my_function",
      [&call_count](VM& vm, Fiber& f, uint8_t nargs, uint8_t nkwargs) -> Value {
        ++call_count;
        EXPECT_EQ(nargs, 2);
        EXPECT_EQ(nkwargs, 0);
        std::vector<Value> args(f.op_stack.end() - 2, f.op_stack.end());
        f.op_stack.resize(f.op_stack.size() - 2);
        EXPECT_EQ(args[0], 1);
        EXPECT_EQ(args[1], "foo");
        return Value();
      });

  chunk->const_pool = value_vector(fn, 1, "foo");
  append_ins(chunk, {Push{0}, Push{1}, Push{2}, Call{2, 0}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(out, std::monostate());
}

TEST_F(VMTest, BuiltinCallsDiscardTheirResults) {
  auto* text = gc->Allocate<String>("abc");
  auto* list = gc->Allocate<List>(value_vector(1));
  auto* dict = gc->Allocate<Dict>();
  struct CallCase {
    IObject* receiver;
    std::string_view member;
    std::vector<Value> args;
  };
  for (const auto& call :
       std::vector<CallCase>{{text, "upper", {}},
                             {text, "len", {}},
                             {list, "append", value_vector(2)},
                             {dict, "setdefault", value_vector("a", 3)}}) {
    Value callee = gc->TrackValue(call.receiver->GetMember(call.member));
    auto* fiber = gc->Allocate<Fiber>();
    fiber->op_stack = value_vector(17, callee);
    fiber->op_stack.insert(fiber->op_stack.end(), call.args.begin(),
                           call.args.end());
    callee.Call(vm, *fiber, call.args.size(), 0, CallResultMode::Discard);
    ASSERT_EQ(fiber->op_stack.size(), 1u);
    EXPECT_EQ(fiber->op_stack.front(), 17);
  }
  ASSERT_EQ(list->items.size(), 2u);
  EXPECT_EQ(list->items.back(), 2);
  EXPECT_EQ(dict->map.at(to_value("a")), 3);
}

TEST_F(VMTest, ScriptPropertyUsesReceiverAndDiscardsSetterReturn) {
  auto* getter_code = gc->Allocate<Code>();
  getter_code->fast_locals = {"getter", "self"};
  getter_code->const_pool = value_vector("_value", 3);
  append_ins(getter_code,
             {LoadFast{1}, GetField{0}, Push{1}, BinaryOp{Op::Mul}, Return{}});
  auto* getter = gc->Allocate<Function>(getter_code);
  getter->arglist.nparam = 1;

  auto* setter_code = gc->Allocate<Code>();
  setter_code->fast_locals = {"setter", "self", "value"};
  setter_code->const_pool = value_vector("_value", 999);
  append_ins(setter_code,
             {LoadFast{1}, LoadFast{2}, SetField{0}, Push{1}, Return{}});
  auto* setter = gc->Allocate<Function>(setter_code);
  setter->arglist.nparam = 2;

  auto* code = gc->Allocate<Code>();
  code->const_pool = value_vector("Example", "value", getter, setter, 5, 100);
  append_ins(code, {Push{5}, Push{1}, Push{2}, Push{3}, MakeClass{0, 0, 0, 1},
                    Call{0, 0}, Dup{}, Push{4}, SetField{1}, GetField{1},
                    BinaryOp{Op::Add}, Return{}});
  EXPECT_EQ(run_and_get(code), 115);
}

TEST_F(VMTest, PropertyAccessorsCannotBeShadowedByInstanceFields) {
  auto* klass = gc->Allocate<Class>();
  auto* getter = gc->Allocate<NativeFunction>(
      "getter", [](VM&, Fiber& fiber, uint8_t nargs, uint8_t) -> Value {
        EXPECT_EQ(nargs, 1);
        fiber.op_stack.pop_back();
        return 42;
      });
  klass->properties["value"] = Property{Value(getter), nil};
  auto* instance = gc->Allocate<Instance>(klass);
  instance->fields["value"] = 99;

  auto* code = gc->Allocate<Code>();
  code->const_pool = value_vector(instance, "value");
  append_ins(code, {Push{0}, GetField{1}, Return{}});
  EXPECT_EQ(run_and_get(code), 42);
  EXPECT_EQ(run_and_get(CatchPropertyAccess(Value(instance))), 77);
  EXPECT_EQ(instance->fields.at("value"), 99);
}

TEST_F(VMTest, MissingPropertyGetterIsCatchable) {
  auto* klass = gc->Allocate<Class>();
  auto* setter = gc->Allocate<NativeFunction>(
      "setter", [](VM&, Fiber& fiber, uint8_t, uint8_t) -> Value {
        fiber.op_stack.resize(fiber.op_stack.size() - 2);
        return nil;
      });
  klass->properties["value"] = Property{nil, Value(setter)};
  auto* instance = gc->Allocate<Instance>(klass);
  EXPECT_EQ(run_and_get(CatchPropertyAccess(Value(instance), false)), 77);
}

TEST_F(VMTest, SetterArgumentFailurePreservesCallerHandlerStack) {
  auto* setter_code = gc->Allocate<Code>();
  setter_code->fast_locals = {"setter", "self", "value", "extra"};
  auto* setter = gc->Allocate<Function>(setter_code);
  setter->arglist.nparam = 3;
  auto* klass = gc->Allocate<Class>();
  klass->properties["value"] = Property{nil, Value(setter)};
  auto* instance = gc->Allocate<Instance>(klass);
  EXPECT_EQ(run_and_get(CatchPropertyAccess(Value(instance))), 77);
}

TEST_F(VMTest, SetterExceptionsPreserveCallerHandlerStack) {
  auto* setter_code = gc->Allocate<Code>();
  setter_code->fast_locals = {"setter", "self", "value"};
  setter_code->const_pool = value_vector("script setter failed");
  append_ins(setter_code, {Push{0}, Throw{}});
  auto* script_setter = gc->Allocate<Function>(setter_code);
  script_setter->arglist.nparam = 2;
  auto* native_setter = gc->Allocate<NativeFunction>(
      "setter", [](VM&, Fiber&, uint8_t, uint8_t) -> Value {
        throw RuntimeError("native setter failed");
      });

  for (Value setter : value_vector(script_setter, native_setter)) {
    auto* klass = gc->Allocate<Class>();
    klass->properties["value"] = Property{nil, setter};
    auto* instance = gc->Allocate<Instance>(klass);
    EXPECT_EQ(run_and_get(CatchPropertyAccess(Value(instance))), 77);
  }
}

TEST_F(VMTest, SuspendedSetterDiscardsResultAfterResuming) {
  auto* pending = gc->Allocate<Future>();
  auto gate = pending->promise;
  auto* setter_code = gc->Allocate<Code>();
  setter_code->fast_locals = {"setter", "self", "value"};
  setter_code->const_pool = value_vector(pending, "_value", 999);
  append_ins(setter_code, {Push{0}, Await{}, Pop{}, LoadFast{1}, LoadFast{2},
                           SetField{1}, Push{2}, Return{}});
  auto* setter = gc->Allocate<Function>(setter_code);
  setter->arglist.nparam = 2;
  auto* klass = gc->Allocate<Class>();
  klass->properties["value"] = Property{nil, Value(setter)};
  auto* instance = gc->Allocate<Instance>(klass);
  auto* code = gc->Allocate<Code>();
  code->const_pool = value_vector(instance, "value", "_value", 7, 3);
  append_ins(code, {Push{4}, Push{0}, Push{3}, SetField{1}, Push{0},
                    GetField{2}, BinaryOp{Op::Add}, Return{}});
  auto* fiber = vm.AddFiber(code);
  auto completion = fiber->completion_promise;

  EXPECT_EQ(vm.Run(std::chrono::milliseconds(1)), nil);
  ASSERT_EQ(fiber->state, FiberState::Suspended);
  EXPECT_FALSE(completion->HasResult());
  vm.CollectGarbage();
  gate->Resolve(nil);
  EXPECT_EQ(vm.Run(), 10);
}

TEST_F(VMTest, NativePropertiesDispatchGettersAndSetters) {
  auto* klass = gc->Allocate<NativeClass>();
  auto* instance = gc->Allocate<NativeInstance>(klass);
  int stored = 0;
  instance->SetForeign(&stored);
  auto* getter = gc->Allocate<NativeFunction>(
      "getter", [](VM&, Fiber& fiber, uint8_t nargs, uint8_t) -> Value {
        EXPECT_EQ(nargs, 1);
        auto* self = fiber.op_stack.back().Get_if<NativeInstance>();
        fiber.op_stack.pop_back();
        return *self->GetForeign<int>();
      });
  auto* setter = gc->Allocate<NativeFunction>(
      "setter", [](VM&, Fiber& fiber, uint8_t nargs, uint8_t) -> Value {
        EXPECT_EQ(nargs, 2);
        int value = fiber.op_stack.back().Get<int>();
        fiber.op_stack.pop_back();
        auto* self = fiber.op_stack.back().Get_if<NativeInstance>();
        fiber.op_stack.pop_back();
        *self->GetForeign<int>() = value;
        return 999;
      });
  klass->properties["value"] = Property{Value(getter), Value(setter)};
  auto* code = gc->Allocate<Code>();
  code->const_pool = value_vector(instance, "value", 8, 10);
  append_ins(code, {Push{3}, Push{0}, Push{2}, SetField{1}, Push{0},
                    GetField{1}, BinaryOp{Op::Add}, Return{}});
  EXPECT_EQ(run_and_get(code), 18);
  EXPECT_EQ(stored, 8);
}

TEST_F(VMTest, ScriptItemAssignmentDiscardsSetterReturn) {
  auto* setter_code = gc->Allocate<Code>();
  setter_code->fast_locals = {"setter", "self", "index", "value"};
  setter_code->const_pool = value_vector("stored", 999);
  append_ins(setter_code,
             {LoadFast{1}, LoadFast{3}, SetField{0}, Push{1}, Return{}});
  auto* setter = gc->Allocate<Function>(setter_code);
  setter->arglist.nparam = 3;
  auto* klass = gc->Allocate<Class>();
  klass->memfns["__setitem__"] = setter;
  auto* instance = gc->Allocate<Instance>(klass);
  auto* code = gc->Allocate<Code>();
  code->const_pool = value_vector(instance, 0, 8, 42, "stored");
  append_ins(code, {Push{3}, Push{0}, Push{1}, Push{2}, SetItem{}, Push{0},
                    GetField{4}, BinaryOp{Op::Add}, Return{}});
  EXPECT_EQ(run_and_get(code), 50);
  EXPECT_EQ(instance->fields.at("stored"), 8);

  auto* explicit_call = gc->Allocate<Code>();
  explicit_call->const_pool = value_vector(instance, "__setitem__", 0, 9);
  append_ins(explicit_call,
             {Push{0}, GetField{1}, Push{2}, Push{3}, Call{2, 0}, Return{}});
  EXPECT_EQ(run_and_get(explicit_call), 999);
}

TEST_F(VMTest, NativeItemAssignmentDiscardsSetterReturn) {
  int stored = 0;
  auto* klass = gc->Allocate<NativeClass>();
  auto* instance = gc->Allocate<NativeInstance>(klass);
  auto* setter = gc->Allocate<NativeFunction>(
      "__setitem__",
      [&stored, instance](VM&, Fiber& fiber, uint8_t nargs, uint8_t) -> Value {
        EXPECT_EQ(nargs, 3);
        stored = fiber.op_stack.back().Get<int>();
        fiber.op_stack.pop_back();
        EXPECT_EQ(fiber.op_stack.back(), 0);
        fiber.op_stack.pop_back();
        EXPECT_EQ(fiber.op_stack.back(), Value(instance));
        fiber.op_stack.pop_back();
        return 999;
      });
  klass->methods["__setitem__"] = Value(setter);
  auto* code = gc->Allocate<Code>();
  code->const_pool = value_vector(instance, 0, 8, 42);
  append_ins(code, {Push{3}, Push{0}, Push{1}, Push{2}, SetItem{}, Return{}});
  EXPECT_EQ(run_and_get(code), 42);
  EXPECT_EQ(stored, 8);
  EXPECT_EQ(CallMember(instance, "__setitem__", value_vector(0, 9)), 999);
  EXPECT_EQ(stored, 9);
}

TEST_F(VMTest, NativePropertyRootsTemporaryReceiverDuringGc) {
  struct Payload {
    std::shared_ptr<bool> destroyed;
    ~Payload() { *destroyed = true; }
  };
  auto destroyed = std::make_shared<bool>(false);
  auto* klass = gc->Allocate<NativeClass>();
  klass->finalize = [](void* ptr) { delete static_cast<Payload*>(ptr); };
  auto* getter = gc->Allocate<NativeFunction>(
      "getter", [destroyed](VM& vm, Fiber& fiber, uint8_t, uint8_t) -> Value {
        fiber.op_stack.pop_back();
        vm.CollectGarbage();
        EXPECT_FALSE(*destroyed);
        return 42;
      });
  klass->properties["value"] = Property{Value(getter), nil};
  auto* create = gc->Allocate<NativeFunction>(
      "create", [klass, destroyed](VM& vm, Fiber&, uint8_t, uint8_t) -> Value {
        auto* instance = vm.gc_->Allocate<NativeInstance>(klass);
        instance->SetForeign(new Payload{destroyed});
        return Value(instance);
      });
  auto* code = gc->Allocate<Code>();
  code->const_pool = value_vector(create, klass, "value");
  append_ins(code, {Push{0}, Call{0, 0}, GetField{2}, Return{}});
  EXPECT_EQ(run_and_get(code), 42);
  vm.CollectGarbage();
  EXPECT_TRUE(*destroyed);
}

TEST_F(VMTest, InlineCallbackCompletesFullPrefixWithoutRunningOtherFibers) {
  Code* immediate = gc->Allocate<Code>();
  immediate->const_pool = value_vector(42);
  for (int i = 0; i < 512; ++i)
    immediate->Append(Nop{});
  append_ins(immediate, {Push{0}, Return{}});
  auto* callback = gc->Allocate<Function>(immediate);

  Code* unrelated = gc->Allocate<Code>();
  unrelated->const_pool = value_vector(9);
  append_ins(unrelated, {Push{0}, Return{}});
  Fiber* second = vm.AddFiber(unrelated);
  const FiberState previous_state = second->state;

  vm.InvokeUntilBlocked(*callback, {});
  Fiber* first = vm.fibres_.back();
  EXPECT_EQ(first->state, FiberState::Dead);
  ASSERT_TRUE(first->completion_promise->result.has_value());
  ASSERT_TRUE(first->completion_promise->result->has_value());
  EXPECT_EQ(first->completion_promise->result->value(), 42);
  EXPECT_EQ(second->state, previous_state);
  EXPECT_FALSE(second->completion_promise->HasResult());
  EXPECT_EQ(vm.Run(), 9);
}

TEST_F(VMTest, InlineCallbackContinuesThroughAlreadyResolvedAwait) {
  auto* ready = gc->Allocate<Future>();
  ready->promise->Resolve(17);
  Code* code = gc->Allocate<Code>();
  code->const_pool = value_vector(ready);
  append_ins(code, {Push{0}, Await{}, Return{}});
  auto* callback = gc->Allocate<Function>(code);

  vm.InvokeUntilBlocked(*callback, {});

  Fiber* fiber = vm.fibres_.back();
  EXPECT_EQ(fiber->state, FiberState::Dead);
  ASSERT_TRUE(fiber->completion_promise->result.has_value());
  ASSERT_TRUE(fiber->completion_promise->result->has_value());
  EXPECT_EQ(fiber->completion_promise->result->value(), 17);
  EXPECT_TRUE(vm.scheduler_.IsIdle());
}

TEST_F(VMTest, InlineCallbackResumesSuspendedContinuationOnScheduler) {
  auto* pending = gc->Allocate<Future>();
  auto gate = pending->promise;
  Code* code = gc->Allocate<Code>();
  code->const_pool = value_vector(pending);
  append_ins(code, {Push{0}, Await{}, Return{}});
  auto* callback = gc->Allocate<Function>(code);

  vm.InvokeUntilBlocked(*callback, {});
  EXPECT_EQ(vm.fibres_.back()->state, FiberState::Suspended);
  EXPECT_FALSE(vm.fibres_.back()->completion_promise->HasResult());
  EXPECT_TRUE(vm.scheduler_.IsIdle());
  vm.CollectGarbage();

  gate->Resolve(73);
  auto completion = vm.fibres_.back()->completion_promise;
  EXPECT_EQ(vm.Run(), nil);
  ASSERT_TRUE(completion->result.has_value());
  ASSERT_TRUE(completion->result->has_value());
  EXPECT_EQ(completion->result->value(), 73);
}

TEST_F(VMTest, InlineCallbackRootsArgumentsAndFunctionDuringNativeGc) {
  auto* collect = gc->Allocate<NativeFunction>(
      "collect", [](VM& current, Fiber&, uint8_t, uint8_t) -> Value {
        current.CollectGarbage();
        return nil;
      });
  Code* code = gc->Allocate<Code>();
  code->fast_locals = {"callee", "argument"};
  code->const_pool = value_vector(collect);
  append_ins(code, {Push{0}, Call{0, 0}, Pop{}, LoadFast{1}, Return{}});
  auto* callback = gc->Allocate<Function>(code);
  callback->arglist.nparam = 1;
  auto* argument = gc->Allocate<String>("kept by callback local");

  vm.InvokeUntilBlocked(*callback, {Value(argument)});

  auto completion = vm.fibres_.back()->completion_promise;
  ASSERT_TRUE(completion->result.has_value());
  ASSERT_TRUE(completion->result->has_value());
  EXPECT_EQ(completion->result->value(), "kept by callback local");
  vm.gc_threshold_ = 1;
  EXPECT_EQ(vm.Run(), nil);
  vm.CollectGarbage();
  EXPECT_TRUE(vm.scheduler_.IsIdle());
}

TEST_F(VMTest, InlineCallbackPreservesNativeCallersReturnValue) {
  Code* callback_code = gc->Allocate<Code>();
  callback_code->const_pool = value_vector(99);
  append_ins(callback_code, {Push{0}, Return{}});
  auto* callback = gc->Allocate<Function>(callback_code);
  Fiber* caller = nullptr;
  auto* starter = gc->Allocate<NativeFunction>(
      "starter",
      [callback, &caller](VM& current, Fiber& fiber, uint8_t,
                          uint8_t) -> Value {
        caller = &fiber;
        current.InvokeUntilBlocked(*callback, {});
        EXPECT_NE(current.fibres_.back(), caller);
        return 42;
      });
  Code* code = gc->Allocate<Code>();
  code->const_pool = value_vector(starter, callback);
  append_ins(code, {Push{0}, Call{0, 0}, Return{}});

  EXPECT_EQ(run_and_get(code), 42);
}

TEST_F(VMTest, InlineCallbackFailurePropagatesOnce) {
  Code* code = gc->Allocate<Code>();
  code->const_pool = value_vector("callback failure");
  append_ins(code, {Push{0}, Throw{}});
  auto* callback = gc->Allocate<Function>(code);

  try {
    vm.InvokeUntilBlocked(*callback, {});
    FAIL() << "Expected callback failure";
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.message(), "callback failure");
  }
  EXPECT_NO_THROW(vm.Run());
}

TEST_F(VMTest, InlineCallbackArgumentFailurePropagatesOnce) {
  Code* code = gc->Allocate<Code>();
  code->fast_locals = {"callee", "argument"};
  append_ins(code, {LoadFast{1}, Return{}});
  auto* callback = gc->Allocate<Function>(code);
  callback->arglist.nparam = 1;

  EXPECT_THROW(vm.InvokeUntilBlocked(*callback, {}), RuntimeError);
  ASSERT_EQ(vm.fibres_.size(), 1u);
  EXPECT_EQ(vm.fibres_.back()->state, FiberState::Dead);
  EXPECT_NO_THROW(vm.Run());
}

TEST_F(VMTest, InlineCallbackCanCatchNativeStandardException) {
  auto* throwing = gc->Allocate<NativeFunction>(
      "throwing", [](VM&, Fiber&, uint8_t, uint8_t) -> Value {
        throw std::runtime_error("native callback failure");
      });
  Code* code = gc->Allocate<Code>();
  code->fast_locals = {"error"};
  code->const_pool = value_vector(throwing, 0);
  const size_t try_begin = code->code.size();
  code->Append(TryBegin{});
  append_ins(code, {Push{0}, Call{0, 0}, TryEnd{}, Push{1}, Return{}});
  const size_t handler = code->code.size();
  append_ins(code, {StoreFast{0}, LoadFast{0}, Return{}});
  const size_t try_body = try_begin + 1 + sizeof(TryBegin);
  code->Write<int32_t>(try_begin + 1, static_cast<int32_t>(handler - try_body));
  auto* callback = gc->Allocate<Function>(code);

  vm.InvokeUntilBlocked(*callback, {});
  auto completion = vm.fibres_.back()->completion_promise;
  ASSERT_TRUE(completion->result.has_value());
  ASSERT_TRUE(completion->result->has_value());
  EXPECT_EQ(completion->result->value(), "native callback failure");
  EXPECT_EQ(vm.Run(), nil);
}

TEST_F(VMTest, SuspendedCallbackFailureRemainsReportable) {
  auto* pending = gc->Allocate<Future>();
  auto gate = pending->promise;
  Code* code = gc->Allocate<Code>();
  code->const_pool = value_vector(pending, "continuation failure");
  append_ins(code, {Push{0}, Await{}, Pop{}, Push{1}, Throw{}});
  auto* callback = gc->Allocate<Function>(code);

  vm.InvokeUntilBlocked(*callback, {});
  gate->Resolve(nil);
  EXPECT_THROW(vm.Run(), UnhandledError);
}

TEST_F(VMTest, RejectedCallbackReportsBeforeDeadFiberBatchThreshold) {
  struct AdvancingPoller : IPoller {
    explicit AdvancingPoller(MockClock* clock) : clock(clock) {}

    void Wait(std::chrono::milliseconds timeout) override {
      clock->AdvanceTime(timeout.count() > 0 ? timeout
                                             : std::chrono::milliseconds(1));
    }

    MockClock* clock;
  };
  auto clock = std::make_unique<MockClock>();
  auto poller = std::make_unique<AdvancingPoller>(clock.get());
  vm.scheduler_ = Scheduler(std::move(poller), std::move(clock));

  auto* waiting = gc->Allocate<Future>();
  Code* waiting_code = gc->Allocate<Code>();
  waiting_code->const_pool = value_vector(waiting);
  append_ins(waiting_code, {Push{0}, Await{}, Return{}});
  auto* waiting_callback = gc->Allocate<Function>(waiting_code);
  for (int i = 0; i < 8; ++i)
    vm.InvokeUntilBlocked(*waiting_callback, {});

  auto* rejected = gc->Allocate<Future>();
  auto gate = rejected->promise;
  Code* rejected_code = gc->Allocate<Code>();
  rejected_code->const_pool = value_vector(rejected);
  append_ins(rejected_code, {Push{0}, Await{}, Return{}});
  auto* rejected_callback = gc->Allocate<Function>(rejected_code);
  vm.InvokeUntilBlocked(*rejected_callback, {});
  ASSERT_EQ(vm.fibres_.size(), 9u);

  vm.scheduler_.PushCallbackAfter(
      [gate] { gate->Reject("callback timer failure"); },
      std::chrono::milliseconds(1));
  bool fallback_fired = false;
  vm.scheduler_.PushCallbackAfter(
      [this, &fallback_fired] {
        fallback_fired = true;
        vm.RequestStop();
      },
      std::chrono::milliseconds(2));

  try {
    vm.Run();
    FAIL() << "Expected rejected callback failure";
  } catch (const UnhandledError& error) {
    EXPECT_STREQ(error.what(), "callback timer failure");
  }
  EXPECT_FALSE(fallback_fired);
  EXPECT_FALSE(vm.IsStopRequested());
}

TEST_F(VMTest, RequestStopInterruptsRunningFiber) {
  auto* chunk = gc->Allocate<Code>();

  int call_count = 0;
  auto* fn = gc->Allocate<NativeFunction>(
      "stop", [&call_count](VM& vm, Fiber&, uint8_t, uint8_t) -> Value {
        ++call_count;
        vm.RequestStop();
        return Value();
      });

  chunk->const_pool = value_vector(fn, 123);
  append_ins(chunk, {Push{0}, Call{0, 0}, Push{1}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(call_count, 1);
  EXPECT_TRUE(vm.IsStopRequested());
  EXPECT_EQ(out, std::monostate());
}

TEST_F(VMTest, RequestStopFromTimerDoesNotWaitForLaterTimers) {
  struct RecordingPoller : IPoller {
    void Wait(std::chrono::milliseconds timeout) override {
      ++wait_calls;
      last_timeout = timeout;
    }

    int wait_calls = 0;
    std::chrono::milliseconds last_timeout{};
  };

  auto poller = std::make_unique<RecordingPoller>();
  auto clock = std::make_unique<MockClock>();
  auto* poller_ptr = poller.get();
  auto* clock_ptr = clock.get();
  vm.scheduler_ = Scheduler(std::move(poller), std::move(clock));

  auto pending = std::make_shared<Promise>();
  vm.TrackPendingPromise(pending);

  bool stop_callback_invoked = false;
  const auto now = clock_ptr->GetTime();
  vm.scheduler_.PushCallbackAt(
      [this, &stop_callback_invoked] {
        stop_callback_invoked = true;
        vm.RequestStop();
      },
      now);
  vm.scheduler_.PushCallbackAt([] {}, now + std::chrono::milliseconds(5000));

  std::ignore = vm.Run();

  EXPECT_TRUE(stop_callback_invoked);
  EXPECT_TRUE(vm.IsStopRequested());
  EXPECT_EQ(poller_ptr->wait_calls, 0);
}

TEST_F(VMTest, TrackedTimerStartsNextSceneAfterLastFiberFinishes) {
  struct AdvancingPoller : IPoller {
    explicit AdvancingPoller(MockClock* clock) : clock(clock) {}

    void Wait(std::chrono::milliseconds timeout) override {
      clock->AdvanceTime(timeout.count() > 0 ? timeout
                                             : std::chrono::milliseconds(1));
    }

    MockClock* clock;
  };
  auto clock = std::make_unique<MockClock>();
  auto poller = std::make_unique<AdvancingPoller>(clock.get());
  vm.scheduler_ = Scheduler(std::move(poller), std::move(clock));

  Code* initial = gc->Allocate<Code>();
  initial->const_pool = value_vector(1);
  append_ins(initial, {Push{0}, Return{}});
  auto initial_completion = vm.AddFiber(initial)->completion_promise;

  Code* menu = gc->Allocate<Code>();
  menu->const_pool = value_vector(42);
  append_ins(menu, {Push{0}, Return{}});
  auto* menu_entry = gc->Allocate<Function>(menu);
  (*vm.globals_)["menu_entry"] = Value(menu_entry);

  auto pending = std::make_shared<Promise>();
  vm.TrackPendingPromise(pending);
  bool timer_fired = false;
  vm.scheduler_.PushCallbackAfter(
      [this, pending, initial_completion, menu_entry, &timer_fired] {
        timer_fired = true;
        EXPECT_TRUE(initial_completion->HasResult());
        vm.CollectGarbage();
        pending->Resolve(nil);
        vm.AddFiber(menu_entry->chunk);
      },
      std::chrono::milliseconds(1));

  EXPECT_EQ(vm.Run(), 42);
  EXPECT_TRUE(timer_fired);
  EXPECT_TRUE(pending->HasResult());
  EXPECT_EQ(vm.CountPendingPromises(), 0u);
}

TEST_F(VMTest, NativeExceptionsEscapeVM) {
  auto* chunk = gc->Allocate<Code>();
  auto* fn = gc->Allocate<NativeFunction>(
      "explode", [](VM&, Fiber&, uint8_t, uint8_t) -> Value {
        throw std::runtime_error("native failure");
      });

  chunk->const_pool = value_vector(fn);
  append_ins(chunk, {Push{0}, Call{0, 0}, Return{}});

  try {
    std::ignore = run_and_get(chunk);
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    EXPECT_STREQ(e.what(), "native failure");
  }
}

TEST_F(VMTest, InvalidOpcodeReportsInternalVmError) {
  auto* chunk = gc->Allocate<Code>();
  chunk->code.push_back(std::byte{0xff});

  try {
    std::ignore = run_and_get(chunk);
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    EXPECT_STREQ(
        e.what(),
        "VM internal error: unimplemented instruction at bytecode offset 0");
  }
}

TEST_F(VMTest, UnhandledVmErrorReportsOriginalError) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(std::monostate(), 0);
  append_ins(chunk, {Push{0}, Push{1}, GetItem{}, Return{}});

  try {
    std::ignore = run_and_get(chunk);
    FAIL() << "expected UnhandledError";
  } catch (const UnhandledError& e) {
    EXPECT_STREQ(e.what(), "'<nil>' object has no item '0'");
  }
}

TEST_F(VMTest, Nop) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(123);
  append_ins(chunk, {Nop{}, Push{0}, Nop{}, Nop{}, Return{}, Nop{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 123);
}

TEST_F(VMTest, DebugValue) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(123, 234);
  append_ins(chunk, {Push{1}, Push{0}, DebugValue{}, Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 234);
}

TEST_F(VMTest, MultipleFibres) {
  auto* chunk1 = gc->Allocate<Code>();
  chunk1->const_pool = value_vector(1);
  append_ins(chunk1, {Push{0}, Return{}});
  // fiber1: return 1;
  auto* chunk2 = gc->Allocate<Code>();
  chunk2->const_pool = value_vector(3, 2, 1);
  append_ins(chunk2, {Push{0}, Push{1}, Push{2}, BinaryOp(Op::Add),
                      BinaryOp(Op::Mul), Return{}});
  // fiber2: return 3*(2+1);

  Fiber* f1 = vm.AddFiber(chunk1);
  Fiber* f2 = vm.AddFiber(chunk2);
  f1->state = FiberState::Running;
  f2->state = FiberState::Running;
  std::ignore = vm.Run();

  EXPECT_EQ(f1->state, FiberState::Dead);
  EXPECT_EQ(f2->state, FiberState::Dead);
  EXPECT_EQ(f1->completion_promise->result->value(), 1);
  EXPECT_EQ(f2->completion_promise->result->value(), 9);
}

TEST_F(VMTest, YieldFiber) {
  auto chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(1, 2, 3);
  append_ins(chunk, {Push(0), Yield{}, Push(1), Yield{}, Push(2), Return{}});
  Fiber* f = vm.AddFiber(chunk);
  std::shared_ptr<Promise> promise = f->completion_promise;

  vm.scheduler_.PushTask(f);
  std::ignore = vm.Run();
  EXPECT_EQ(f->state, FiberState::Suspended);
  EXPECT_EQ(promise->result->value(), 1);

  promise = f->completion_promise;
  vm.scheduler_.PushTask(f);
  std::ignore = vm.Run();
  EXPECT_EQ(f->state, FiberState::Suspended);
  EXPECT_EQ(promise->result->value(), 2);

  promise = f->completion_promise;
  vm.scheduler_.PushTask(f);
  std::ignore = vm.Run();
  EXPECT_EQ(f->state, FiberState::Dead);
  EXPECT_EQ(promise->result->value(), 3);
}

TEST_F(VMTest, SpawnFiber) {
  // Create a simple function that immediately returns 123 when run in a fiber.
  auto* code = gc->Allocate<Code>();
  code->const_pool = value_vector(123);
  append_ins(code, {Push{0}, Return{}});
  auto* fn = gc->Allocate<Function>(code);

  // Main program: spawn the function, then await its completion promise.
  // Stack after MakeFiber: (fiber)
  // GetField("completion") -> (promise)
  // Await -> (123)
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(fn, "completion");
  append_ins(chunk, {Push{0},          // fn
                     MakeFiber{0, 0},  // -> fiber
                     Await{},          // -> 123
                     Return{}});

  Value out = run_and_get(chunk);
  EXPECT_EQ(out, 123);
}

TEST_F(VMTest, AwaitFiber) {
  Code* print_code = gc->Allocate<Code>();
  print_code->const_pool = value_vector("print", std::monostate());
  print_code->fast_locals = {"fn", "val"};
  append_ins(print_code, {Push{1}, Yield{}, LoadFast{1},
                          Return{}});  // yield nil; return arg0;
  Function* print_fn = gc->Allocate<Function>(print_code);
  print_fn->arglist.nparam = 1;

  Code* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(print_fn, "foo", "boo", std::monostate());
  append_ins(chunk, {
                        Push{0}, Push{1}, MakeFiber{.argcnt = 1, .kwargcnt = 0},
                        // handle1 = print_corout("foo");
                        Push{0}, Push{2}, MakeFiber{.argcnt = 1, .kwargcnt = 0},
                        // handle2 = print_corout("boo");
                        Await{}, Pop{},  // await handle2;
                        Dup{}, Await{}, Pop{}, Await{},
                        Return{}  // await handle1; return await handle1;
                    });

  Value result = run_and_get(chunk);
  EXPECT_EQ(result, "foo");
}

TEST_F(VMTest, FunctionGlobal) {
  Code* code = gc->Allocate<Code>();
  code->const_pool = value_vector("one");
  code->fast_locals = {"fn", "arg1"};
  append_ins(code, {LoadFast{1}, LoadGlobal{0}, BinaryOp(Op::Add), Return{}});
  Function* fn = gc->Allocate<Function>(code);
  fn->arglist.nparam = 1;
  fn->globals = std::make_shared<std::unordered_map<std::string, Value>>();
  (*fn->globals)["one"] = to_value("one");

  Code* chunk = gc->Allocate<Code>();
  chunk->const_pool =
      value_vector(fn, code, "one", 1, "arg1", "first", "second");
  append_ins(chunk,
             {Push{3}, StoreGlobal{2},  // global: one = 1
              Push{5}, Push{0}, Push{2}, Call{.argcnt = 1, .kwargcnt = 0},
              // first: fn("one") -> "one" + "one";
              // function global: one = "one"
              Push{6}, Push{1}, Push{4}, MakeFunction{0, 1, 0, false, false},
              Push{3}, Call{1, 0},
              // second: fn(1) -> 1 + 1;
              // function global is current namespace
              MakeDict{.nelms = 2}, Return{}});

  Value result = run_and_get(chunk);
  auto* dict = result.Get_if<Dict>();
  ASSERT_NE(dict, nullptr);
  EXPECT_EQ(dict->map.at(to_value("first")), "oneone");
  EXPECT_EQ(dict->map.at(to_value("second")), 2);
}

TEST_F(VMTest, DictItemOpsSupportMixedKeyTypes) {
  auto* dict = gc->Allocate<Dict>();
  auto* fiber = gc->Allocate<Fiber>();

  fiber->op_stack = value_vector(Value(dict), 1, 10);
  dict->SetItem(vm, *fiber);
  EXPECT_TRUE(fiber->op_stack.empty());

  fiber->op_stack = value_vector(Value(dict), true, 20);
  dict->SetItem(vm, *fiber);
  EXPECT_TRUE(fiber->op_stack.empty());

  auto* alpha1 = gc->Allocate<String>("alpha");
  auto* alpha2 = gc->Allocate<String>("alpha");
  fiber->op_stack = value_vector(Value(dict), Value(alpha1), 30);
  dict->SetItem(vm, *fiber);
  EXPECT_TRUE(fiber->op_stack.empty());

  fiber->op_stack = value_vector(Value(dict), 1);
  dict->GetItem(vm, *fiber);
  ASSERT_EQ(fiber->op_stack.size(), 1u);
  EXPECT_EQ(fiber->op_stack.back(), 10);

  fiber->op_stack = value_vector(Value(dict), true);
  dict->GetItem(vm, *fiber);
  ASSERT_EQ(fiber->op_stack.size(), 1u);
  EXPECT_EQ(fiber->op_stack.back(), 20);

  fiber->op_stack = value_vector(Value(dict), Value(alpha2));
  dict->GetItem(vm, *fiber);
  ASSERT_EQ(fiber->op_stack.size(), 1u);
  EXPECT_EQ(fiber->op_stack.back(), 30);
}

TEST_F(VMTest, MakeDictSupportsMixedKeyTypes) {
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool = value_vector(1, "one", true, 10, 20, 30);
  append_ins(chunk, {Push{0}, Push{3}, Push{1}, Push{4}, Push{2}, Push{5},
                     MakeDict{.nelms = 3}, Return{}});

  Value result = run_and_get(chunk);
  auto* dict = result.Get_if<Dict>();
  ASSERT_NE(dict, nullptr);
  EXPECT_EQ(dict->map.at(Value(1)), 10);
  String one("one");
  EXPECT_EQ(dict->map.at(Value(&one)), 20);
  EXPECT_EQ(dict->map.at(Value(true)), 30);
}

TEST_F(VMTest, StringSiglusAsciiMethods) {
  auto* text = gc->Allocate<String>("AbC xYz 123");
  EXPECT_EQ(CallMember(text, "upper"), "ABC XYZ 123");
  EXPECT_EQ(CallMember(text, "lower"), "abc xyz 123");

  auto* searchable = gc->Allocate<String>("AbcAbCA");
  EXPECT_EQ(CallMember(searchable, "find", value_vector("bca")), 1);
  EXPECT_EQ(CallMember(searchable, "find", value_vector("BCA")), 1);
  EXPECT_EQ(CallMember(searchable, "rfind", value_vector("bca")), 4);
  EXPECT_EQ(CallMember(searchable, "find", value_vector("missing")), -1);

  EXPECT_EQ(CallMember(gc->Allocate<String>("12345"), "tonum"), 12345);
  EXPECT_EQ(CallMember(gc->Allocate<String>("-42"), "tonum"), -42);
  EXPECT_EQ(CallMember(gc->Allocate<String>("+7"), "tonum"), 7);
  EXPECT_EQ(CallMember(gc->Allocate<String>("12x"), "tonum"), 0);
  EXPECT_EQ(CallMember(gc->Allocate<String>("nope"), "tonum"), 0);
}

TEST_F(VMTest, StringSiglusUtf8DisplayWidthMethods) {
  const std::string hiragana_a = "\xe3\x81\x82";
  const std::string halfwidth_ka = "\xef\xbd\xb6";
  const std::string mixed = std::string("A") + hiragana_a + halfwidth_ka + "B";
  auto* text = gc->Allocate<String>(mixed);

  EXPECT_EQ(CallMember(text, "cnt"), 4);
  EXPECT_EQ(CallMember(text, "len"), 5);
  EXPECT_EQ(
      CallMember(text, "find",
                 std::vector<Value>{Value(gc->Allocate<String>(halfwidth_ka))}),
      2);
  EXPECT_EQ(CallMember(text, "rfind", value_vector("B")), 3);

  EXPECT_EQ(CallMember(text, "left_len", value_vector(0)), "");
  EXPECT_EQ(CallMember(text, "left_len", value_vector(1)), "A");
  EXPECT_EQ(CallMember(text, "left_len", value_vector(2)), "A");
  EXPECT_EQ(CallMember(text, "left_len", value_vector(3)),
            std::string("A") + hiragana_a);
  EXPECT_EQ(CallMember(text, "left_len", value_vector(4)),
            std::string("A") + hiragana_a + halfwidth_ka);

  EXPECT_EQ(CallMember(text, "right_len", value_vector(0)), "");
  EXPECT_EQ(CallMember(text, "right_len", value_vector(3)), halfwidth_ka + "B");
  EXPECT_EQ(CallMember(text, "right_len", value_vector(4)),
            hiragana_a + halfwidth_ka + "B");

  EXPECT_EQ(CallMember(text, "mid_len", value_vector(1)),
            hiragana_a + halfwidth_ka + "B");
  EXPECT_EQ(CallMember(text, "mid_len", value_vector(1, 1)), "");
  EXPECT_EQ(CallMember(text, "mid_len", value_vector(1, 2)), hiragana_a);
  EXPECT_EQ(CallMember(text, "mid_len", value_vector(1, 3)),
            hiragana_a + halfwidth_ka);
  EXPECT_EQ(CallMember(text, "mid_len", value_vector(2, 2)),
            halfwidth_ka + "B");
}

TEST_F(VMTest, StringSiglusCharacterSubstringsClampRanges) {
  auto* text = gc->Allocate<String>("abcd");

  EXPECT_EQ(CallMember(text, "left", value_vector(-1)), "");
  EXPECT_EQ(CallMember(text, "left", value_vector(2)), "ab");
  EXPECT_EQ(CallMember(text, "left", value_vector(99)), "abcd");

  EXPECT_EQ(CallMember(text, "right", value_vector(-1)), "");
  EXPECT_EQ(CallMember(text, "right", value_vector(2)), "cd");
  EXPECT_EQ(CallMember(text, "right", value_vector(99)), "abcd");

  EXPECT_EQ(CallMember(text, "mid", value_vector(-5)), "abcd");
  EXPECT_EQ(CallMember(text, "mid", value_vector(1)), "bcd");
  EXPECT_EQ(CallMember(text, "mid", value_vector(1, 2)), "bc");
  EXPECT_EQ(CallMember(text, "mid", value_vector(2, 99)), "cd");
  EXPECT_EQ(CallMember(text, "mid", value_vector(2, -3)), "");
  EXPECT_EQ(CallMember(text, "mid", value_vector(99)), "");

  EXPECT_EQ(CallMember(text, "mid_len", value_vector(-5)), "abcd");
  EXPECT_EQ(CallMember(text, "mid_len", value_vector(-5, 2)), "ab");
  EXPECT_EQ(CallMember(text, "mid_len", value_vector(99)), "");
}

TEST_F(VMTest, StringSiglusCharAtReturnsCodepointOrMinusOne) {
  const std::string hiragana_a = "\xe3\x81\x82";
  const std::string halfwidth_ka = "\xef\xbd\xb6";
  auto* text =
      gc->Allocate<String>(std::string("A") + hiragana_a + halfwidth_ka);

  EXPECT_EQ(CallMember(text, "charat", value_vector(0)), 'A');
  EXPECT_EQ(CallMember(text, "charat", value_vector(1)), 0x3042);
  EXPECT_EQ(CallMember(text, "charat", value_vector(2)), 0xff76);
  EXPECT_EQ(CallMember(text, "charat", value_vector(-1)), -1);
  EXPECT_EQ(CallMember(text, "charat", value_vector(3)), -1);
}

TEST_F(VMTest, DictMethods) {
  auto* dict = gc->Allocate<Dict>();

  EXPECT_EQ(CallMember(dict, "update", {},
                       {std::pair<std::string, Value>{"a", Value(1)},
                        std::pair<std::string, Value>{"b", Value(2)}}),
            nil);
  EXPECT_EQ(CallMember(dict, "get", value_vector("a")), 1);
  EXPECT_EQ(CallMember(dict, "get", value_vector("missing", 42)), 42);

  EXPECT_EQ(CallMember(dict, "setdefault", value_vector("c", 3)), 3);
  EXPECT_EQ(CallMember(dict, "setdefault", value_vector("c", 99)), 3);

  auto* keys = CallMember(dict, "keys").Get_if<List>();
  ASSERT_NE(keys, nullptr);
  ASSERT_EQ(keys->items.size(), 3u);
  EXPECT_EQ(keys->items[0], "a");
  EXPECT_EQ(keys->items[1], "b");
  EXPECT_EQ(keys->items[2], "c");

  auto* values = CallMember(dict, "values").Get_if<List>();
  ASSERT_NE(values, nullptr);
  ASSERT_EQ(values->items.size(), 3u);
  EXPECT_EQ(values->items[0], 1);
  EXPECT_EQ(values->items[1], 2);
  EXPECT_EQ(values->items[2], 3);

  auto* items = CallMember(dict, "items").Get_if<List>();
  ASSERT_NE(items, nullptr);
  ASSERT_EQ(items->items.size(), 3u);
  auto* item0 = items->items[0].Get_if<List>();
  ASSERT_NE(item0, nullptr);
  EXPECT_EQ(item0->items[0], "a");
  EXPECT_EQ(item0->items[1], 1);

  auto* copied = CallMember(dict, "copy").Get_if<Dict>();
  ASSERT_NE(copied, nullptr);
  ASSERT_NE(copied, dict);
  EXPECT_EQ(copied->map.at(to_value("a")), 1);
  EXPECT_EQ(copied->map.at(to_value("b")), 2);
  EXPECT_EQ(copied->map.at(to_value("c")), 3);

  EXPECT_EQ(CallMember(dict, "pop", value_vector("b")), 2);

  auto* pair_list = gc->Allocate<List>(
      std::vector<Value>{Value(gc->Allocate<List>(value_vector("d", 4))),
                         Value(gc->Allocate<List>(value_vector("e", 5)))});
  EXPECT_EQ(CallMember(dict, "update", std::vector<Value>{Value(pair_list)}),
            nil);

  auto* popped = CallMember(dict, "popitem").Get_if<List>();
  ASSERT_NE(popped, nullptr);
  ASSERT_EQ(popped->items.size(), 2u);
  EXPECT_EQ(popped->items[0], "e");
  EXPECT_EQ(popped->items[1], 5);

  auto* seed_keys = gc->Allocate<List>(value_vector("x", "y"));
  auto* from_keys = CallMember(dict, "fromkeys",
                               std::vector<Value>{Value(seed_keys), Value(9)})
                        .Get_if<Dict>();
  ASSERT_NE(from_keys, nullptr);
  EXPECT_EQ(from_keys->map.at(to_value("x")), 9);
  EXPECT_EQ(from_keys->map.at(to_value("y")), 9);

  EXPECT_EQ(CallMember(dict, "clear"), nil);
  EXPECT_TRUE(dict->map.empty());
  EXPECT_TRUE(dict->order.empty());
}

TEST_F(VMTest, ListMethods) {
  auto* xs = gc->Allocate<List>(value_vector(2, 1, 2));

  EXPECT_EQ(CallMember(xs, "len"), 3);
  EXPECT_EQ(CallMember(xs, "append", std::vector<Value>{Value(3)}), nil);
  EXPECT_EQ(CallMember(xs, "len"), 4);
  EXPECT_EQ(CallMember(xs, "count", std::vector<Value>{Value(2)}), 2);
  EXPECT_EQ(CallMember(xs, "index", std::vector<Value>{Value(3)}), 3);

  auto* copied = CallMember(xs, "copy").Get_if<serilang::List>();
  ASSERT_NE(copied, nullptr);
  ASSERT_NE(copied, xs);
  ASSERT_EQ(copied->items.size(), 4u);
  EXPECT_EQ(copied->items[0], 2);
  EXPECT_EQ(copied->items[3], 3);

  auto* extension = gc->Allocate<List>(value_vector(4, 5));
  EXPECT_EQ(CallMember(xs, "extend", std::vector<Value>{Value(extension)}),
            nil);
  EXPECT_EQ(CallMember(xs, "insert", std::vector<Value>{Value(1), Value(9)}),
            nil);

  EXPECT_EQ(CallMember(xs, "pop"), 5);
  EXPECT_EQ(CallMember(xs, "pop", std::vector<Value>{Value(1)}), 9);
  EXPECT_EQ(CallMember(xs, "remove", std::vector<Value>{Value(2)}), nil);
  EXPECT_EQ(CallMember(xs, "reverse"), nil);
  EXPECT_EQ(CallMember(xs, "sort"), nil);

  ASSERT_EQ(xs->items.size(), 4u);
  EXPECT_EQ(xs->items[0], 1);
  EXPECT_EQ(xs->items[1], 2);
  EXPECT_EQ(xs->items[2], 3);
  EXPECT_EQ(xs->items[3], 4);

  EXPECT_EQ(CallMember(xs, "clear"), nil);
  EXPECT_EQ(CallMember(xs, "len"), 0);
  EXPECT_TRUE(xs->items.empty());
}

TEST_F(VMTest, Exception) {
  Code* code = gc->Allocate<Code>();
  code->const_pool = value_vector(1, 2);
  code->fast_locals = {"unused", "error_handle"};
  append_ins(code, {Push(1),
                    // 5: try
                    TryBegin(19), Push(1), Push(0), Throw(), TryEnd(), Jump(2),
                    // 29: handle
                    StoreFast(1),
                    // 31: return local #1
                    LoadFast(1), Return()});

  Value result = run_and_get(code);
  EXPECT_EQ(result.Desc(), "<int: 1>");
}

}  // namespace serilang_test
