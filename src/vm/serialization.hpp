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

#include "utilities/serialization.hpp"
#include "vm/object.hpp"
#include "vm/value.hpp"

#include <algorithm>
#include <any>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>

namespace serilang {
class ObjectWriter;
class ObjectReader;
using Namespace = std::unordered_map<std::string, Value>;

// Host data and executable references are reconstructed by the embedding
// engine.
struct SerializationHooks {
  std::function<std::optional<std::string>(const IObject&)> external_name;
  std::function<Value(std::string_view)> resolve_external;
  std::function<void(ObjectWriter&, const std::any&)> save_userdata;
  std::function<std::any(ObjectReader&)> load_userdata;
};

struct StackValueOverride {
  const Fiber* fiber = nullptr;
  std::size_t index = 0;
  Value value = nil;
};

// A checkpoint shares promises across its machines. Object IDs are VM-local.
class SerializationSession {
 public:
  uint32_t PromiseId(const std::shared_ptr<serilang::Promise>& promise);
  std::shared_ptr<serilang::Promise> Promise(uint32_t id);
  void FinishRestore();

 private:
  friend class ObjectWriter;
  friend class ObjectReader;
  std::unordered_map<const serilang::Promise*, uint32_t> promise_ids_;
  std::vector<std::shared_ptr<serilang::Promise>> retained_;
  std::unordered_set<const serilang::Promise*> written_;
  std::unordered_map<uint32_t, std::shared_ptr<serilang::Promise>> promises_;
  std::unordered_set<uint32_t> loaded_;
  std::vector<std::pair<VM*, Fiber*>> waiting_;
};

class ObjectTypes {
 public:
  using Factory = std::function<IObject*(GarbageCollector&)>;
  template <class T, class F>
  void Register(std::string name, F factory) {
    names_.emplace(typeid(T), name);
    factories_.emplace(std::move(name), std::move(factory));
  }
  std::string Name(const IObject& object) const;
  IObject* Allocate(const std::string& name, GarbageCollector& gc) const;
  static const ObjectTypes& Builtins();

 private:
  std::unordered_map<std::type_index, std::string> names_;
  std::unordered_map<std::string, Factory> factories_;
};

class ObjectWriter {
 public:
  ObjectWriter(boost::archive::binary_oarchive& archive,
               VM& vm,
               SerializationSession& session,
               SerializationHooks hooks = {});
  boost::archive::binary_oarchive& Archive() { return archive_; }
  template <class T>
  void Put(const T& value) {
    archive_ << value;
  }
  void Text(std::string_view text) { Put(std::string(text)); }
  template <class Range, class F>
  void Sequence(const Range& values, F write) {
    if (values.size() > 1000000)
      throw std::runtime_error("saved collection exceeds limit");
    Put<uint32_t>(values.size());
    for (const auto& value : values)
      write(value);
  }
  template <class Map>
  void MapValue(const Map& map) {
    std::vector<std::string> keys;
    for (const auto& [key, value] : map)
      keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    Sequence(keys, [&](const auto& key) {
      Text(key);
      Write(map.at(key));
    });
  }
  void Write(const Value& value);
  void WriteNamespace(const std::shared_ptr<Namespace>& values);
  void WritePromise(const std::shared_ptr<Promise>& promise);
  // Complete this graph before committing it or resuming restored execution.
  void Finish();
  VM& vm;
  SerializationHooks hooks;
  StackValueOverride stack_override;

 private:
  boost::archive::binary_oarchive& archive_;
  SerializationSession& session_;
  std::unordered_map<const IObject*, uint32_t> objects_;
  std::unordered_map<const Namespace*, uint32_t> namespaces_;
  std::unordered_map<const IObject*, std::string> symbols_;
};

class ObjectReader {
 public:
  ObjectReader(boost::archive::binary_iarchive& archive,
               VM& vm,
               SerializationSession& session,
               SerializationHooks hooks = {});
  boost::archive::binary_iarchive& Archive() { return archive_; }
  template <class T>
  T Get() {
    T value{};
    archive_ >> value;
    return value;
  }
  std::string Text() { return Get<std::string>(); }
  uint32_t Count();
  template <class Map>
  void MapValue(Map& map) {
    for (auto n = Count(); n; --n) {
      auto key = Text();
      auto value = Read();
      if (!map.emplace(std::move(key), value).second)
        throw std::runtime_error("duplicate saved field");
    }
  }
  Value Read();
  std::shared_ptr<Namespace> ReadNamespace(
      std::shared_ptr<Namespace> target = {});
  std::shared_ptr<Promise> ReadPromise();
  // Complete this graph before committing it or resuming restored execution.
  void Finish();
  void AwaitLater(Fiber& fiber);

  VM& vm;
  SerializationHooks hooks;

 private:
  void Validate();
  bool Boundary(const Code* code, uint32_t offset);
  boost::archive::binary_iarchive& archive_;
  SerializationSession& session_;
  GarbageCollector::CollectionGuard collection_guard_;
  std::unordered_map<uint32_t, Value> objects_;
  std::unordered_map<uint32_t, std::shared_ptr<Namespace>> namespaces_;
  std::unordered_map<const Code*, std::unordered_set<uint32_t>> positions_;
};

void RememberBindingSymbols(VM& vm);
uint64_t CodeFingerprint(const Code& code);
void RegisterListSerialization(ObjectTypes& types);
void RegisterDictSerialization(ObjectTypes& types);
void RegisterStringSerialization(ObjectTypes& types);
}  // namespace serilang
