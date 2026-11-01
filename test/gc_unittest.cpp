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

#include "vm/call_frame.hpp"
#include "vm/dict.hpp"
#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/instruction.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace serilang_test {
using namespace serilang;

// A tiny IObject subclass that counts how many instances are alive.
class DummyObject : public IObject {
 public:
  static int& aliveCount() {
    static int count = 0;
    return count;
  }

  DummyObject() { ++aliveCount(); }
  ~DummyObject() override { --aliveCount(); }

  static constexpr inline ObjType objtype = ObjType::Dummy;
  constexpr ObjType Type() const noexcept override { return objtype; }
  constexpr size_t Size() const noexcept final { return sizeof(*this); }
  void MarkRoots(GCVisitor&) override {}

  std::size_t Hash() const override { return 1; }
};

// Report large sizes without making large allocations when testing thresholds.
class SizedObject : public IObject {
 public:
  explicit SizedObject(size_t size) : size_(size) {}
  constexpr ObjType Type() const noexcept final { return ObjType::Dummy; }
  constexpr size_t Size() const noexcept final { return size_; }
  void MarkRoots(GCVisitor&) final {}

 private:
  size_t size_;
};

// Exercise alignment requirements beyond the default allocation alignment.
class alignas(64) AlignedObject final : public SizedObject {
 public:
  AlignedObject() : SizedObject(sizeof(AlignedObject)) {}
};

// Count storage allocation and cleanup when construction fails before tracking.
class ThrowingObject final : public DummyObject {
 public:
  static inline int allocations = 0;
  static inline int deallocations = 0;

  static void* operator new(size_t size) {
    void* object = ::operator new(size);
    ++allocations;
    return object;
  }
  static void* operator new(size_t, void* object) noexcept { return object; }
  static void operator delete(void* object) noexcept {
    ++deallocations;
    ::operator delete(object);
  }

  ThrowingObject() { throw std::runtime_error("construction failed"); }
};

// Each test gets a fresh collector and VM; VM values provide explicit GC roots.
class GCTest : public ::testing::Test {
 protected:
  std::shared_ptr<GarbageCollector> gc;
  VM vm;
  GCTest() : gc(std::make_shared<GarbageCollector>()), vm(gc) {}

  template <typename T, typename... Args>
  T* Alloc(Args... args) {
    auto* ptr = gc->Allocate<T>(std::forward<Args>(args)...);
    return ptr;
  }

  Value strval(const char* str) {
    auto* s = Alloc<serilang::String>(str);
    return Value(s);
  }
};

TEST_F(GCTest, AllocatedBytes) {
  // Allocate two unrooted objects and check their combined byte count.
  // Collection must restore the original count.
  size_t before = gc->AllocatedBytes();
  Alloc<DummyObject>();
  Alloc<DummyObject>();

  EXPECT_EQ(gc->AllocatedBytes(), before + 2 * sizeof(DummyObject));
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), before);
}

TEST_F(GCTest, DirectRegistrationAccountsForObject) {
  // Transfer an externally constructed object directly to GC and root it.
  // Count it once and retain it until unrooting allows its bytes to be removed.
  const size_t before = gc->AllocatedBytes();
  auto object = std::make_unique<DummyObject>();
  vm.last_ = Value(object.get());
  gc->TrackObject(object.release());

  EXPECT_EQ(gc->AllocatedBytes(), before + sizeof(DummyObject));
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), before + sizeof(DummyObject));

  vm.last_ = nil;
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), before);
}

TEST_F(GCTest, AdoptedValueAccountsForObject) {
  // Adopt an owned temporary through TrackValue and keep its returned Value.
  // Its bytes must remain counted until the Value is unrooted and collected.
  const size_t before = gc->AllocatedBytes();
  vm.last_ = gc->TrackValue(TempValue(std::make_unique<DummyObject>()));

  EXPECT_EQ(gc->AllocatedBytes(), before + sizeof(DummyObject));
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), before + sizeof(DummyObject));

  vm.last_ = nil;
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), before);
}

TEST_F(GCTest, ExistingValueIsNotCountedAgain) {
  // Pass an already tracked object and an integer through TrackValue.
  // Neither should add registered bytes; the rooted object must survive GC.
  auto* object = Alloc<DummyObject>();
  const size_t before = gc->AllocatedBytes();

  vm.last_ = gc->TrackValue(TempValue(Value(object)));
  EXPECT_EQ(vm.last_.Get_if<DummyObject>(), object);
  EXPECT_EQ(gc->TrackValue(TempValue(Value(42))), 42);
  EXPECT_EQ(gc->AllocatedBytes(), before);
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), before);
}

TEST_F(GCTest, ThrowingConstructorDoesNotChangeAccounting) {
  // Make construction throw after allocating storage.
  // Storage must be freed, with no live object or additional registered bytes.
  const size_t before = gc->AllocatedBytes();
  const int alive_before = DummyObject::aliveCount();
  ThrowingObject::allocations = ThrowingObject::deallocations = 0;

  EXPECT_THROW(Alloc<ThrowingObject>(), std::runtime_error);
  EXPECT_EQ(ThrowingObject::allocations, 1);
  EXPECT_EQ(ThrowingObject::deallocations, 1);
  EXPECT_EQ(gc->AllocatedBytes(), before);
  EXPECT_EQ(DummyObject::aliveCount(), alive_before);
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), before);
}

TEST_F(GCTest, AllocationRespectsObjectAlignment) {
  // Allocate a 64-byte-aligned object through GC and then collect it.
  // Expect an aligned address and no remaining registered bytes after GC.
  auto* object = Alloc<AlignedObject>();

  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(object) % alignof(AlignedObject),
            0u);
  EXPECT_EQ(gc->AllocatedBytes(), object->Size());
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), 0u);
}

TEST_F(GCTest, CollectionThresholdAdaptsToSurvivingObjects) {
  // Exercise an empty heap, a rooted 2 MiB object, and removal of that root.
  // Expect thresholds of 1 MiB, then a stable 4 MiB, then 1 MiB again.
  constexpr size_t minimum = 1024 * 1024;
  vm.gc_threshold_ = minimum * 8;

  vm.CollectGarbage();
  EXPECT_EQ(vm.gc_threshold_, minimum);
  vm.CollectGarbage();
  EXPECT_EQ(vm.gc_threshold_, minimum);

  vm.last_ = Value(Alloc<SizedObject>(minimum * 2));
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), minimum * 2);
  EXPECT_EQ(vm.gc_threshold_, minimum * 4);
  vm.CollectGarbage();
  EXPECT_EQ(vm.gc_threshold_, minimum * 4);

  vm.last_ = nil;
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), 0u);
  EXPECT_EQ(vm.gc_threshold_, minimum);
}

TEST_F(GCTest, CollectionThresholdSaturatesWithoutDisablingGc) {
  // Report sizes at the doubling boundary without allocating enormous objects.
  // Double exactly when safe, saturate on overflow, and return to the GC floor.
  constexpr size_t maximum = std::numeric_limits<size_t>::max();
  for (const auto& [size, expected] :
       {std::pair{maximum / 2, maximum - 1},
        std::pair{maximum / 2 + 1, maximum}, std::pair{maximum, maximum}}) {
    SCOPED_TRACE(size);
    vm.last_ = Value(Alloc<SizedObject>(size));
    vm.CollectGarbage();
    EXPECT_EQ(gc->AllocatedBytes(), size);
    EXPECT_EQ(vm.gc_threshold_, expected);

    vm.last_ = nil;
    vm.CollectGarbage();
    EXPECT_EQ(gc->AllocatedBytes(), 0u);
    EXPECT_EQ(vm.gc_threshold_, 1024u * 1024);
  }
}

TEST_F(GCTest, ExplicitCollectionPreservesDisabledThreshold) {
  // Disable automatic GC and leave one allocated object unreachable.
  // Explicit collection must reclaim it without re-enabling automatic GC.
  vm.gc_threshold_ = 0;
  const int alive_before = DummyObject::aliveCount();
  Alloc<DummyObject>();

  vm.CollectGarbage();

  EXPECT_EQ(DummyObject::aliveCount(), alive_before);
  EXPECT_EQ(gc->AllocatedBytes(), 0u);
  EXPECT_EQ(vm.gc_threshold_, 0u);
}

TEST_F(GCTest, DisabledThresholdSuppressesAutomaticCollection) {
  // Execute a trivial script with automatic GC disabled and an unrooted object.
  // Execution must leave it alive; explicit collection must still reclaim it.
  vm.gc_threshold_ = 0;
  const int alive_before = DummyObject::aliveCount();
  Alloc<DummyObject>();
  auto* code = Alloc<Code>();
  code->const_pool = {Value(42)};
  code->Append(Push{0});
  code->Append(Return{});

  EXPECT_EQ(vm.Evaluate(code), 42);
  EXPECT_EQ(DummyObject::aliveCount(), alive_before + 1);
  EXPECT_EQ(vm.gc_threshold_, 0u);

  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), alive_before);
  EXPECT_EQ(vm.gc_threshold_, 0u);
}

TEST_F(GCTest, AutomaticCollectionContinuesThroughMixedAllocationChurn) {
  // Repeatedly allocate and adopt unreachable temporaries through native calls.
  // Automatic GC must keep object counts bounded and never disable itself.
  constexpr size_t minimum = 1024 * 1024;
  const int alive_before = DummyObject::aliveCount();
  auto* step = Alloc<NativeFunction>(
      "allocate_temporaries",
      [](VM& current, Fiber&, uint8_t, uint8_t) -> TempValue {
        for (int i = 0; i < 1000; ++i) {
          std::ignore = current.gc_->Allocate<DummyObject>();
          current.AddTrack(TempValue(std::make_unique<DummyObject>()));
        }
        return Value();
      });
  auto* code = Alloc<Code>();
  code->const_pool = {Value(step)};
  code->Append(Push{0});
  code->Append(Call{0, 0});
  code->Append(Return{});
  // Keep the program reachable while each evaluation creates a fresh fiber.
  (*vm.globals_)["churn"] = Value(code);

  for (int i = 0; i < 500; ++i)
    vm.Evaluate(code);

  EXPECT_EQ(vm.gc_threshold_, minimum);
  EXPECT_LT(gc->AllocatedBytes(), minimum * 2);
  EXPECT_LT(DummyObject::aliveCount() - alive_before, 100000);

  // Only the rooted program and its native callee should remain after cleanup.
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), alive_before);
  EXPECT_EQ(gc->AllocatedBytes(), sizeof(Code) + sizeof(NativeFunction));
}

TEST_F(GCTest, Sweep) {
  // An unreachable allocated object must be deleted by the next collection.
  DummyObject::aliveCount() = 0;
  Alloc<DummyObject>();
  EXPECT_EQ(DummyObject::aliveCount(), 1);

  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

TEST_F(GCTest, MarkValue) {
  // Keep an object through the VM's last-value root, then clear it to allow GC.
  DummyObject::aliveCount() = 0;
  auto* d = Alloc<DummyObject>();
  EXPECT_EQ(DummyObject::aliveCount(), 1);

  // Mark by wrapping in Value
  vm.last_ = Value(d);
  vm.CollectGarbage();
  // Still alive because it was marked
  EXPECT_EQ(DummyObject::aliveCount(), 1);

  vm.last_ = Value();
  vm.CollectGarbage();
  // Next sweep without marking should collect it
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

TEST_F(GCTest, MarkList) {
  // Tracing a rooted list must retain both items until the list is unrooted.
  DummyObject::aliveCount() = 0;
  auto* d1 = Alloc<DummyObject>();
  auto* d2 = Alloc<DummyObject>();
  EXPECT_EQ(DummyObject::aliveCount(), 2);

  // Build a List containing both
  std::vector<Value> items = {Value(d1), Value(d2)};
  auto* listObj = Alloc<List>(std::move(items));

  vm.last_ = Value(listObj);
  vm.CollectGarbage();
  // Both DummyObjects survive because listVal was marked, which recursed into
  // items
  EXPECT_EQ(DummyObject::aliveCount(), 2);

  // If we sweep again without re-marking the list, they all die:
  vm.last_ = Value(1);
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

TEST_F(GCTest, MarkDict) {
  // Both dictionary values must survive until their dictionary is unrooted.
  DummyObject::aliveCount() = 0;
  auto* d1 = Alloc<DummyObject>();
  auto* d2 = Alloc<DummyObject>();
  EXPECT_EQ(DummyObject::aliveCount(), 2);

  // Build a Dict containing both
  std::unordered_map<Value, Value> mp;
  mp[strval("one")] = Value(d1);
  mp[strval("two")] = Value(d2);
  auto* dictObj = Alloc<Dict>(std::move(mp));
  Value dictVal(dictObj);

  vm.last_ = Value(dictObj);
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 2);

  vm.last_ = Value();
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

TEST_F(GCTest, MarkDictObjectKeys) {
  // Trace object keys as well as values; unrooting the dictionary frees both.
  DummyObject::aliveCount() = 0;
  auto* key = Alloc<DummyObject>();
  auto* value = Alloc<DummyObject>();
  EXPECT_EQ(DummyObject::aliveCount(), 2);

  Dict::map_t mp;
  mp.emplace(key, Value(value));
  auto* dictObj = Alloc<Dict>(std::move(mp));

  vm.last_ = Value(dictObj);
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 2);

  vm.last_ = Value();
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

TEST_F(GCTest, MarkGlobalsRoot) {
  // A global entry must retain its object until the entry is removed.
  DummyObject::aliveCount() = 0;
  auto* d = Alloc<DummyObject>();
  EXPECT_EQ(DummyObject::aliveCount(), 1);

  // Place into VM globals
  (*vm.globals_)["foo"] = Value(d);
  vm.CollectGarbage();
  // Still alive
  EXPECT_EQ(DummyObject::aliveCount(), 1);

  // Remove global and recollect
  vm.globals_->clear();
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

TEST_F(GCTest, MarkFibresAndClosures) {
  // A rooted fiber reaches five objects through its stack, promise, code, and
  // fast locals. All must survive until the fiber is removed from the VM roots.
  DummyObject::aliveCount() = 0;
  // Create a one-shot closure and fiber
  auto* chunk = gc->Allocate<Code>();
  auto* fn = Alloc<Function>(chunk);
  auto* f = Alloc<Fiber>();
  f->frames.emplace_back(CallFrame(fn));

  auto* d1 = Alloc<DummyObject>();
  auto* d2 = Alloc<DummyObject>();
  auto* d3 = Alloc<DummyObject>();
  f->op_stack.emplace_back(d1);
  f->completion_promise->roots.emplace_back(d2);
  fn->chunk->const_pool.emplace_back(d3);

  auto* d4 = Alloc<DummyObject>();
  auto* d5 = Alloc<DummyObject>();
  f->frames[0].fast_locals = {Value(d4), std::nullopt, Value(d5)};

  // Register fiber in VM
  vm.fibres_ = {f};

  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 5);

  // Removing the fiber root should make all five objects collectible.
  vm.fibres_.clear();
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

TEST_F(GCTest, CircularReferences) {
  // Trace a rooted cycle without looping forever; reclaim it when unrooted.
  DummyObject::aliveCount() = 0;
  // Create two DummyObjects
  auto* d1 = Alloc<DummyObject>();
  auto* d2 = Alloc<DummyObject>();
  EXPECT_EQ(DummyObject::aliveCount(), 2);

  // Create two lists that reference each other and also hold the DummyObjects
  auto* list1 = Alloc<List>(std::vector<Value>{Value(d1)});
  auto* list2 = Alloc<List>(std::vector<Value>{Value(d2)});
  // circular link
  list1->items.emplace_back(list2);
  list2->items.emplace_back(list1);

  // Root only list1
  vm.last_ = Value(list1);
  vm.CollectGarbage();
  // Both DummyObjects survive because they are reachable through the cycle
  EXPECT_EQ(DummyObject::aliveCount(), 2);

  // Clear root and collect again: the cycle is now unreachable
  vm.last_ = Value();
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

TEST_F(GCTest, MixedValueGraph) {
  // Connect objects through an instance, class, dictionary, list, and function.
  // Rooting the instance must preserve the whole graph; unrooting must free it.
  DummyObject::aliveCount() = 0;
  // Create three DummyObjects
  auto* d1 = Alloc<DummyObject>();
  auto* d2 = Alloc<DummyObject>();
  auto* d3 = Alloc<DummyObject>();
  EXPECT_EQ(DummyObject::aliveCount(), 3);

  // Closure holding d3 in its constant pool
  auto* chunk = gc->Allocate<Code>();
  chunk->const_pool.emplace_back(d3);
  auto* fn = Alloc<Function>(chunk);

  // List holding d2
  auto* list = Alloc<List>(std::vector<Value>{Value(d2)});

  // Dict holding d1 and the list
  std::unordered_map<Value, Value> mp;
  mp[strval("one")] = Value(d1);
  mp[strval("lst")] = Value(list);
  auto* dict = Alloc<Dict>(mp);

  // Class+Instance: instance.field → dict, class.method → closure
  auto* klass = Alloc<Class>();
  klass->name = "Mixed";
  klass->memfns["fn"] = fn;
  auto* inst = Alloc<Instance>(klass);
  inst->fields["data"] = Value(dict);

  // Root only the instance
  vm.last_ = Value(inst);
  vm.CollectGarbage();
  // All three DummyObjects survive via the mixed graph
  EXPECT_EQ(DummyObject::aliveCount(), 3);

  // Drop the root and collect: everything should be freed
  vm.last_ = Value();
  vm.CollectGarbage();
  EXPECT_EQ(DummyObject::aliveCount(), 0);
}

}  // namespace serilang_test
