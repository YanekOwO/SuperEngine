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

#include "vm/serialization.hpp"
#include <array>
#include <cstring>
#include <utility>
#include "vm/dict.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

namespace serilang {
template <std::size_t... I>
constexpr auto InstructionSizes(std::index_sequence<I...>) {
  std::array<std::size_t, sizeof...(I)> sizes{};
  ((sizes[static_cast<std::size_t>(
        GetOpcode<std::variant_alternative_t<I, Instruction>>())] =
        sizeof(std::variant_alternative_t<I, Instruction>)),
   ...);
  return sizes;
}

uint64_t CodeFingerprint(const Code& code) {
  uint64_t hash = 1469598103934665603ull;
  auto append = [&](std::string_view bytes) {
    for (unsigned char byte : bytes) {
      hash ^= byte;
      hash *= 1099511628211ull;
    }
  };
  // Native instruction padding is not part of executable identity.
  static constexpr auto sizes = InstructionSizes(
      std::make_index_sequence<std::variant_size_v<Instruction>>{});
  for (std::size_t offset = 0; offset < code.code.size();) {
    const auto opcode = std::to_integer<uint8_t>(code.code[offset]);
    if (opcode >= sizes.size() || sizes[opcode] > code.code.size() - offset - 1)
      throw std::runtime_error("invalid scene instruction encoding");
    std::size_t payload = sizes[opcode];
    if (opcode == static_cast<uint8_t>(OpCode::MakeFunction))
      payload = offsetof(MakeFunction, has_kwarg) + sizeof(bool);
    else if (opcode == static_cast<uint8_t>(OpCode::MakeClass))
      payload = offsetof(MakeClass, nproperty) + sizeof(uint16_t);
    append({reinterpret_cast<const char*>(code.code.data() + offset),
            1 + payload});
    offset += 1 + sizes[opcode];
  }
  for (const auto& value : code.const_pool) {
    const auto type = static_cast<uint8_t>(value.Type());
    append({reinterpret_cast<const char*>(&type), sizeof(type)});
    if (const auto* string = value.Get_if<String>())
      append(string->str_);
    else if (!value.Get_if<IObject>())
      append(value.Str());
  }
  for (const auto& local : code.fast_locals)
    append(local);
  return hash;
}

std::unordered_set<uint32_t> ValidateCode(const Code& code) {
  static constexpr auto sizes = InstructionSizes(
      std::make_index_sequence<std::variant_size_v<Instruction>>{});
  std::unordered_set<uint32_t> positions;
  std::vector<std::int64_t> destinations;
  for (std::size_t offset = 0; offset < code.code.size();) {
    positions.insert(offset);
    const auto opcode = std::to_integer<uint8_t>(code.code[offset++]);
    if (opcode >= sizes.size() || !sizes[opcode] ||
        sizes[opcode] > code.code.size() - offset)
      throw std::runtime_error("invalid saved instruction encoding");
    auto operand = [&]<class T>() {
      T value;
      std::memcpy(&value, code.code.data() + offset, sizeof(value));
      return value;
    };
    const auto next = offset + sizes[opcode];
    auto constant = [&](uint32_t index, bool name) {
      if (index >= code.const_pool.size() ||
          (name && !code.const_pool[index].Get_if<String>()))
        throw std::runtime_error("invalid saved instruction constant");
    };
    switch (static_cast<OpCode>(opcode)) {
      case OpCode::Push:
        constant(operand.template operator()<Push>().const_index, false);
        break;
      case OpCode::LoadGlobal:
        constant(operand.template operator()<LoadGlobal>().name_index, true);
        break;
      case OpCode::StoreGlobal:
        constant(operand.template operator()<StoreGlobal>().name_index, true);
        break;
      case OpCode::GetField:
        constant(operand.template operator()<GetField>().name_index, true);
        break;
      case OpCode::SetField:
        constant(operand.template operator()<SetField>().name_index, true);
        break;
      case OpCode::LoadFast:
        if (operand.template operator()<LoadFast>().slot >=
            code.fast_locals.size())
          throw std::runtime_error("invalid saved local slot");
        break;
      case OpCode::StoreFast:
        if (operand.template operator()<StoreFast>().slot >=
            code.fast_locals.size())
          throw std::runtime_error("invalid saved local slot");
        break;
      case OpCode::Jump:
        destinations.push_back(static_cast<std::int64_t>(next) +
                               operand.template operator()<Jump>().offset);
        break;
      case OpCode::JumpIfTrue:
        destinations.push_back(
            static_cast<std::int64_t>(next) +
            operand.template operator()<JumpIfTrue>().offset);
        break;
      case OpCode::JumpIfFalse:
        destinations.push_back(
            static_cast<std::int64_t>(next) +
            operand.template operator()<JumpIfFalse>().offset);
        break;
      case OpCode::TryBegin:
        destinations.push_back(
            static_cast<std::int64_t>(next) +
            operand.template operator()<TryBegin>().handler_rel_ofs);
        break;
      case OpCode::MakeFunction:
        destinations.push_back(
            operand.template operator()<MakeFunction>().entry);
        break;
      default:
        break;
    }
    offset = next;
  }
  positions.insert(code.code.size());
  for (auto destination : destinations)
    if (destination < 0 || destination > code.code.size() ||
        !positions.contains(destination))
      throw std::runtime_error("invalid saved instruction destination");
  for (const auto& [line, offset] : code.line_offset)
    if (!positions.contains(offset))
      throw std::runtime_error("invalid saved line offset");
  return positions;
}

std::string ObjectTypes::Name(const IObject& object) const {
  auto found = names_.find(typeid(object));
  if (found == names_.end())
    throw std::runtime_error("unsupported saved object: " + object.Desc());
  return found->second;
}
IObject* ObjectTypes::Allocate(const std::string& name,
                               GarbageCollector& gc) const {
  auto found = factories_.find(name);
  if (found == factories_.end())
    throw std::runtime_error("unknown saved object type: " + name);
  return found->second(gc);
}
const ObjectTypes& ObjectTypes::Builtins() {
  static const auto types = [] {
    ObjectTypes result;
#define OBJECT(T, ...) \
  result.Register<T>(  \
      #T, [](GarbageCollector& gc) { return gc.Allocate<T>(__VA_ARGS__); })
    OBJECT(Code);
    OBJECT(Function, nullptr);
    OBJECT(Class);
    OBJECT(Instance, nullptr);
    OBJECT(Module, "");
    OBJECT(NativeInstance, nullptr);
    OBJECT(BoundMethod, nil, std::vector<Value>{});
    OBJECT(Future);
    OBJECT(Fiber);
#undef OBJECT
    RegisterListSerialization(result);
    RegisterDictSerialization(result);
    RegisterStringSerialization(result);
    return result;
  }();
  return types;
}

void RememberBindingSymbols(VM& vm) {
  std::unordered_set<const IObject*> visited;
  auto sorted = [](const auto& map) {
    std::vector<std::string> keys;
    for (const auto& [key, value] : map)
      keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    return keys;
  };
  auto add = [&](auto&& self, std::string name, Value value) -> void {
    auto* object = value.Get_if<IObject>();
    if (!object || !visited.insert(object).second)
      return;
    vm.binding_symbols_.insert_or_assign(name, value);
    auto properties = [&](const auto& props) {
      for (const auto& key : sorted(props)) {
        self(self, name + "/get/" + key, props.at(key).getter);
        self(self, name + "/set/" + key, props.at(key).setter);
      }
    };
    if (auto* klass = value.Get_if<NativeClass>()) {
      for (const auto& key : sorted(klass->methods))
        self(self, name + "/method/" + key, klass->methods.at(key));
      properties(klass->properties);
    } else if (auto* instance = value.Get_if<NativeInstance>()) {
      self(self, name + "/class", instance->klass);
    } else if (auto* klass = value.Get_if<Class>()) {
      for (const auto& key : sorted(klass->memfns))
        self(self, name + "/method/" + key, klass->memfns.at(key));
      properties(klass->properties);
    } else if (auto* module = value.Get_if<Module>()) {
      for (const auto& key : sorted(*module->globals))
        self(self, name + "/global/" + key, module->globals->at(key));
    }
  };
  for (const auto& key : sorted(*vm.globals_)) {
    const auto& value = vm.globals_->at(key);
    if (value.Get_if<NativeFunction>() || value.Get_if<NativeClass>() ||
        value.Get_if<Class>() || value.Get_if<Function>() ||
        value.Get_if<Module>() || value.Get_if<NativeInstance>())
      add(add, "global/" + key, value);
  }
  for (const auto& key : sorted(*vm.builtins_))
    add(add, "builtin/" + key, vm.builtins_->at(key));
}

ObjectWriter::ObjectWriter(boost::archive::binary_oarchive& archive,
                           VM& machine,
                           SerializationSession& session,
                           SerializationHooks callbacks)
    : vm(machine),
      hooks(std::move(callbacks)),
      archive_(archive),
      session_(session) {
  for (const auto& [name, value] : vm.binding_symbols_) {
    const auto* object = value.Get_if<IObject>();
    const auto* native = value.Get_if<NativeInstance>();
    if (object && !(native && native->klass->save))
      symbols_.emplace(object, name);
  }
}
ObjectReader::ObjectReader(boost::archive::binary_iarchive& archive,
                           VM& machine,
                           SerializationSession& session,
                           SerializationHooks callbacks)
    : vm(machine),
      hooks(std::move(callbacks)),
      archive_(archive),
      session_(session),
      collection_guard_(*vm.gc_) {}

void ObjectWriter::Write(const Value& value) {
  if (value.Get_if<std::monostate>()) {
    Put<uint8_t>(0);
    return;
  }
  if (auto* v = value.Get_if<bool>()) {
    Put<uint8_t>(1);
    Put(*v);
    return;
  }
  if (auto* v = value.Get_if<int>()) {
    Put<uint8_t>(2);
    Put(*v);
    return;
  }
  if (auto* v = value.Get_if<double>()) {
    Put<uint8_t>(3);
    Put(*v);
    return;
  }
  auto* object = value.Get_if<IObject>();
  if (!object)
    throw std::runtime_error("null saved object");
  auto [it, fresh] = objects_.emplace(object, objects_.size() + 1);
  Put<uint8_t>(fresh ? 5 : 4);
  Put(it->second);
  if (!fresh)
    return;
  if (hooks.external_name) {
    if (auto name = hooks.external_name(*object)) {
      Put<uint8_t>(2);
      Text(*name);
      return;
    }
  }
  if (auto symbol = symbols_.find(object); symbol != symbols_.end()) {
    Put<uint8_t>(1);
    Text(symbol->second);
    return;
  }
  Put<uint8_t>(0);
  Text(ObjectTypes::Builtins().Name(*object));
  object->Save(*this);
}
Value ObjectReader::Read() {
  auto tag = Get<uint8_t>();
  switch (tag) {
    case 0:
      return nil;
    case 1:
      return Get<bool>();
    case 2:
      return Get<int>();
    case 3:
      return Get<double>();
    case 4:
      return objects_.at(Get<uint32_t>());
    case 5:
      break;
    default:
      throw std::runtime_error("invalid saved value tag");
  }
  auto id = Get<uint32_t>();
  if (!id || objects_.contains(id) || objects_.size() >= 1000000)
    throw std::runtime_error("invalid saved object ID");
  auto kind = Get<uint8_t>();
  auto name = Text();
  if (kind == 1 || kind == 2) {
    Value value;
    if (kind == 1)
      value = vm.binding_symbols_.at(name);
    else if (hooks.resolve_external)
      value = hooks.resolve_external(name);
    else
      throw std::runtime_error("saved external reference has no resolver");
    if (!value.Get_if<IObject>())
      throw std::runtime_error("saved external reference is not an object");
    objects_.emplace(id, value);
    return value;
  }
  if (kind != 0)
    throw std::runtime_error("invalid saved object reference kind");
  auto* object = ObjectTypes::Builtins().Allocate(name, *vm.gc_);
  objects_.emplace(id, object);
  object->Load(*this);
  return object;
}
uint32_t ObjectReader::Count() {
  auto count = Get<uint32_t>();
  if (count > 1000000)
    throw std::runtime_error("saved collection exceeds limit");
  return count;
}
void ObjectWriter::WriteNamespace(const std::shared_ptr<Namespace>& values) {
  if (!values) {
    Put<uint32_t>(0);
    return;
  }
  auto [it, fresh] = namespaces_.emplace(values.get(), namespaces_.size() + 1);
  Put(it->second);
  Put(fresh);
  if (fresh)
    MapValue(*values);
}
std::shared_ptr<Namespace> ObjectReader::ReadNamespace(
    std::shared_ptr<Namespace> target) {
  auto id = Get<uint32_t>();
  if (!id)
    return nullptr;
  if (!Get<bool>())
    return namespaces_.at(id);
  if (namespaces_.contains(id) || namespaces_.size() >= 1000000)
    throw std::runtime_error("invalid saved namespace ID");
  auto values = target ? std::move(target) : std::make_shared<Namespace>();
  namespaces_.emplace(id, values);
  // External resolvers can bootstrap code using the destination's bindings.
  // Keep those bindings available until the replacement namespace is complete.
  Namespace fields;
  MapValue(fields);
  *values = std::move(fields);
  return values;
}

uint32_t SerializationSession::PromiseId(
    const std::shared_ptr<serilang::Promise>& promise) {
  if (!promise)
    return 0;
  auto [it, fresh] =
      promise_ids_.emplace(promise.get(), promise_ids_.size() + 1);
  if (fresh)
    retained_.push_back(promise);
  return it->second;
}
std::shared_ptr<Promise> SerializationSession::Promise(uint32_t id) {
  if (!id)
    return nullptr;
  auto& promise = promises_[id];
  if (!promise)
    promise = std::make_shared<serilang::Promise>();
  return promise;
}
void ObjectWriter::WritePromise(const std::shared_ptr<Promise>& promise) {
  Put(session_.PromiseId(promise));
  if (!promise)
    return;
  bool fresh = session_.written_.insert(promise.get()).second;
  Put(fresh);
  if (!fresh)
    return;
  Put(promise->status);
  Put(promise->GetWakeCount() != 0);
  if (promise->HasResult()) {
    Put(promise->result->has_value());
    if (promise->result->has_value())
      Write(promise->result->value());
    else
      Text(promise->result->error());
  }
}
std::shared_ptr<Promise> ObjectReader::ReadPromise() {
  auto id = Get<uint32_t>();
  auto promise = session_.Promise(id);
  if (!promise || !Get<bool>())
    return promise;
  if (!session_.loaded_.insert(id).second)
    throw std::runtime_error("duplicate saved promise");
  promise->status = Get<Promise::Status>();
  if (promise->status < Promise::Status::New ||
      promise->status > Promise::Status::Rejected)
    throw std::runtime_error("invalid saved promise status");
  bool observed = Get<bool>();
  if (promise->HasResult()) {
    if (Get<bool>()) {
      auto value = Read();
      promise->result = value;
      promise->AddRoot(value);
    } else
      promise->result = unexpected(Text());
    if (observed)
      promise->AddWaker([](const auto&) {});
  } else
    vm.TrackPendingPromise(promise);
  return promise;
}
void ObjectWriter::Finish() {
  for (size_t i = 0; i < session_.retained_.size(); ++i) {
    auto promise = session_.retained_[i];
    if (session_.written_.contains(promise.get()))
      continue;
    Put(true);
    WritePromise(promise);
  }
  Put(false);
}
void ObjectReader::Finish() {
  for (uint32_t count = 0; Get<bool>(); ++count) {
    if (count >= 1000000)
      throw std::runtime_error("saved promise collection exceeds limit");
    ReadPromise();
  }
  Validate();
}
void ObjectReader::Validate() {
  // References can point into objects still being loaded. Validate executable
  // positions after every node in this graph has its fields populated.
  for (const auto& [id, value] : objects_) {
    if (const auto* code = value.Get_if<Code>())
      Boundary(code, 0);
    if (const auto* function = value.Get_if<Function>())
      if (!function->chunk || !Boundary(function->chunk, function->entry))
        throw std::runtime_error("invalid saved function entry");
    if (const auto* fiber = value.Get_if<Fiber>()) {
      for (const auto& frame : fiber->frames) {
        if (!frame.fn->chunk || !Boundary(frame.fn->chunk, frame.ip) ||
            frame.bp > fiber->op_stack.size())
          throw std::runtime_error("invalid saved call frame position");
        if (frame.fast_locals.size() != frame.fn->chunk->fast_locals.size())
          throw std::runtime_error("saved frame locals do not match code");
        for (const auto& handler : frame.handlers)
          if (!Boundary(frame.fn->chunk, handler.handler_ip) ||
              handler.stack_top > fiber->op_stack.size())
            throw std::runtime_error("invalid saved exception handler");
      }
    }
  }
}
void ObjectReader::AwaitLater(Fiber& fiber) {
  session_.waiting_.emplace_back(&vm, &fiber);
}
void SerializationSession::FinishRestore() {
  for (const auto& [id, promise] : promises_)
    if (!loaded_.contains(id))
      throw std::runtime_error("saved promise has no state definition");
  for (auto [vm, fiber] : std::exchange(waiting_, {}))
    vm->RestoreAwait(*fiber);
}
bool ObjectReader::Boundary(const Code* code, uint32_t offset) {
  auto found = positions_.find(code);
  if (found == positions_.end())
    found = positions_.emplace(code, ValidateCode(*code)).first;
  return found->second.contains(offset);
}

void VM::Save(ObjectWriter& writer) const {
  writer.WriteNamespace(globals_);
  writer.Write(last_);
  writer.Write(main_fiber_ ? Value(main_fiber_) : nil);
  writer.Sequence(fibres_, [&](auto* fiber) { writer.Write(fiber); });
  writer.MapValue(module_cache_);
  const auto queues = scheduler_.GetReadyTasks();
  auto write_fiber = [&](auto* fiber) {
    if (std::find(fibres_.begin(), fibres_.end(), fiber) == fibres_.end())
      throw std::runtime_error("scheduler references an unknown fiber");
    writer.Write(fiber);
  };
  writer.Sequence(queues.tasks, write_fiber);
  writer.Sequence(queues.microtasks, write_fiber);
  std::vector<std::shared_ptr<Promise>> pending;
  for (const auto& weak : pending_promises_)
    if (auto promise = weak.lock())
      pending.push_back(promise);
  writer.Sequence(pending, [&](const auto& p) { writer.WritePromise(p); });
}
void VM::Load(ObjectReader& reader) {
  if (!fibres_.empty())
    throw std::runtime_error("execution restoration requires a fresh VM");
  globals_ = reader.ReadNamespace(globals_);
  if (!globals_)
    throw std::runtime_error("saved VM has no globals");
  last_ = reader.Read();
  auto main = reader.Read();
  main_fiber_ = main == nil ? nullptr : main.Get<Fiber*>();
  for (auto n = reader.Count(); n; --n)
    fibres_.push_back(reader.Read().Get<Fiber*>());
  for (auto n = reader.Count(); n; --n) {
    auto key = reader.Text();
    module_cache_.emplace(std::move(key), reader.Read().Get<Module*>());
  }
  Scheduler::ReadyTasks queues;
  auto read_fiber = [&] {
    auto* fiber = reader.Read().Get<Fiber*>();
    if (std::find(fibres_.begin(), fibres_.end(), fiber) == fibres_.end())
      throw std::runtime_error("saved scheduler references an unknown fiber");
    return fiber;
  };
  for (auto n = reader.Count(); n; --n)
    queues.tasks.push_back(read_fiber());
  for (auto n = reader.Count(); n; --n)
    queues.microtasks.push_back(read_fiber());
  scheduler_.RestoreReadyTasks(std::move(queues));
  for (auto n = reader.Count(); n; --n) {
    auto promise = reader.ReadPromise();
    if (promise && !promise->HasResult())
      TrackPendingPromise(promise);
  }
}
}  // namespace serilang
