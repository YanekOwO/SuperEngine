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

#include "srbind/srbind.hpp"

#include "utilities/string_utilities.hpp"
#include "vm/exception.hpp"
#include "vm/gc.hpp"
#include "vm/object.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace srbind_test {

using namespace serilang;
using namespace srbind;
using serilang::helper::pop;
using serilang::helper::push;

class SrbindTest : public ::testing::Test {
 protected:
  using error_type = serilang::RuntimeError;
  std::shared_ptr<GarbageCollector> gc;

  VM vm;
  Fiber* f;

 protected:
  std::unordered_map<std::string, Value> dict;
  srbind::module_ mod;

  SrbindTest()
      : gc(std::make_shared<GarbageCollector>()),
        vm(gc),
        f(gc->Allocate<Fiber>()),
        dict(),
        mod(gc.get(), &dict) {}

  inline Value v(const char* s) {
    String* str = gc->Allocate<String>(s);
    return Value(str);
  }

  // Helper: push a callee and args on the fiber stack and invoke the callee.
  // Layout: [callee, pos0, pos1, ..., k1, v1, k2, v2, ...]
  Value CallCallee(
      Value callee,
      const std::vector<Value>& pos = {},
      const std::vector<std::pair<std::string, Value>>& kwargs = {}) {
    f->op_stack.clear();
    f->op_stack.emplace_back(std::move(callee));  // callee first
    for (auto const& v : pos)
      f->op_stack.emplace_back(v);
    for (auto const& kv : kwargs) {
      String* str = gc->Allocate<String>(kv.first);
      f->op_stack.emplace_back(str);
      f->op_stack.emplace_back(kv.second);
    }
    const uint8_t nargs = static_cast<uint8_t>(pos.size());
    const uint8_t nkwargs = static_cast<uint8_t>(kwargs.size());

    callee.Call(vm, *f, nargs, nkwargs);
    Value ret = pop(f->op_stack);  // (callee) <- (retval)
    if (!f->op_stack.empty()) {
      ADD_FAILURE() << "stack not empty after function call, leftovers are: "
                    << Join(",", std::views::all(f->op_stack) |
                                     std::views::transform(
                                         [](Value& v) { return v.Desc(); }));
      f->op_stack.clear();
    }
    return ret;
  }

  // Helper: get a member value
  Value GetMember(IObject* receiver, std::string_view item) {
    TempValue tval = receiver->Member(item);
    return gc->TrackValue(std::move(tval));
  }

  template <typename T>
  auto get_as(NativeInstance* inst, const char* item) -> std::add_pointer_t<T> {
    TempValue tval = inst->Member(item);
    Value val = gc->TrackValue(std::move(tval));
    auto* r = val.template Get_if<T>();
    if (!r)
      ADD_FAILURE() << "item not found: " << item;
    return r;
  }
  template <typename T, typename U>
  auto get_as(transparent_hashmap<U>& map, std::string_view item)
      -> std::add_pointer_t<T> {
    auto it = map.find(item);
    if (it == map.cend()) {
      ADD_FAILURE() << "item not found: " << item;
      return nullptr;
    }
    auto* r = it->second.template Get_if<T>();
    if (!r) {
      ADD_FAILURE() << "unexpected nullptr: " << item;
      return nullptr;
    }
    return r;
  }
};

TEST_F(SrbindTest, VoidReturn_YieldsNil) {
  bool touched = false;
  auto touch = [&](int x) { touched = (x == 42); };

  NativeFunction* nf = make_function(gc.get(), "touch", touch, arg("x"));

  Value r = CallCallee(Value(nf), {}, {{"x", Value(42)}});
  EXPECT_TRUE(touched);
  // nil is std::monostate in Value
  EXPECT_TRUE(r == std::monostate{});
}

TEST_F(SrbindTest, FreeFunction_PositionalOnly) {
  auto add = [](int a, int b) { return a + b; };
  NativeFunction* nf = make_function(gc.get(), "add", add);  // no arg spec
  Value r = CallCallee(Value(nf), {Value(2), Value(3)});
  EXPECT_TRUE(r == 5);

  EXPECT_THROW(
      [&]() { std::ignore = CallCallee(Value(nf), {}, {{"a", Value(1)}}); }(),
      error_type)
      << "Passing kwargs should error: function takes no keyword arguments";
}

TEST_F(SrbindTest, FreeFunction_KeywordsAndDefaults) {
  // a + 10*b + 100*c
  auto mix = [](int a, int b, int c) { return a + 10 * b + 100 * c; };

  NativeFunction* nf_ptr =
      make_function(gc.get(), "mix", mix, arg("a") = 1, arg("b") = 2,
                    arg("c")  // required
      );
  Value nf = Value(nf_ptr);

  // kwargs only (use defaults for a,b)
  Value r1 = CallCallee(nf, /*pos*/ {}, /*kw*/ {{"c", Value(7)}});
  EXPECT_EQ(r1, 1 + 10 * 2 + 100 * 7);

  // mixed: positional a=9, kw c=7 (b default=2)
  Value r2 = CallCallee(nf, {Value(9)}, {{"c", Value(7)}});
  EXPECT_EQ(r2, 9 + 10 * 2 + 100 * 7);

  // all keywords re-ordered
  Value r3 =
      CallCallee(nf, {}, {{"b", Value(3)}, {"a", Value(2)}, {"c", Value(4)}});
  EXPECT_EQ(r3, 2 + 10 * 3 + 100 * 4);

  // duplicate (positional + same kw)
  EXPECT_THROW(std::ignore = CallCallee(nf, {Value(5)},
                                        {{"a", Value(6)}, {"c", Value(1)}}),
               error_type)
      << "Expected error for duplicate arg";
  ;

  // unexpected keyword
  EXPECT_THROW(
      std::ignore = CallCallee(nf, {}, {{"z", Value(1)}, {"c", Value(2)}}),
      error_type)
      << "Expected error for unexpected keyword";

  // missing required 'c'
  EXPECT_THROW(std::ignore = CallCallee(nf), error_type)
      << "Expected error for missing required argument";

  // too many positionals
  EXPECT_THROW(
      std::ignore = CallCallee(nf, {Value(1), Value(2), Value(3), Value(4)}),
      error_type)
      << "Expected error for too many positional";
  // type error (c must be int)
  EXPECT_THROW(std::ignore = CallCallee(nf, {}, {{"c", v("oops")}}), error_type)
      << "Expected error for type mismatch";
}

struct V {
  int s{0};
  void add(int dx, int dy) { s += dx + dy; }
  int sum() const { return s; }
};

TEST_F(SrbindTest, Class_RegistersByPlainName) {
  class_<V> cv(mod, "V");
  cv.def(init<>());

  EXPECT_TRUE(dict.contains("V"));
  EXPECT_FALSE(dict.contains("<str: V>"));
  EXPECT_NE(dict.at("V").Get_if<NativeClass>(), nullptr);
}

TEST_F(SrbindTest, Class_Methods) {
  // Bind class V with __init__() (no args) and methods:
  // add(dx, dy=0), sum()
  class_<V> cv(mod, "V");
  cv.def(init<>())  // no args
      .def("add", &V::add, arg("dx"), arg("dy") = 0)
      .def("sum", &V::sum);

  // Fetch class object from dict
  Value vclass = dict["V"];

  // Construct instance by calling the class (allocates NativeInstance and
  // copies methods)
  Value inst_v = CallCallee(vclass);  // returns NativeInstance*
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);

  // Call add using kwargs to set dx=3, dy=2
  Value add_fn = GetMember(inst, "add");
  ASSERT_NO_THROW(std::ignore = CallCallee(
                      add_fn, {}, {{"dy", Value(2)}, {"dx", Value(3)}}));

  // sum() -> 5
  Value sum_fn = GetMember(inst, "sum");
  Value ret;
  ASSERT_NO_THROW(ret = CallCallee(sum_fn));
  EXPECT_EQ(ret, 5);

  // Now call add with only dx (dy default=0)
  ASSERT_NO_THROW(std::ignore = CallCallee(add_fn, {Value(4)}));
  ASSERT_NO_THROW(ret = CallCallee(sum_fn));
  EXPECT_TRUE(ret == 9);

  // Duplicate value for dx (positional + kw)
  EXPECT_THROW(std::ignore = CallCallee(add_fn, {Value(1)}, {{"dx", Value(2)}}),
               error_type)
      << "Expected error duplicate 'dx'";
}

TEST_F(SrbindTest, ClassSelfFreeFunction) {
  class_<V> cv(mod, "VSelf");
  auto add = [](V* self, int dx, int dy) { self->s += dx + dy; };
  cv.def(init<>())
      .def("add", add, arg("dx"), arg("dy") = 0)
      .def("add_infer", add)
      .def("sum", [](const V* self) { return self->s; });

  Value klass = dict["VSelf"];
  Value inst_v = CallCallee(klass);
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);

  EXPECT_NO_THROW(CallCallee(GetMember(inst, "add"), {},
                             {{"dy", Value(2)}, {"dx", Value(3)}}));
  EXPECT_EQ(CallCallee(GetMember(inst, "sum")), 5);

  EXPECT_NO_THROW(CallCallee(GetMember(inst, "add"), {Value(4)}));
  EXPECT_EQ(CallCallee(GetMember(inst, "sum")), 9);

  EXPECT_NO_THROW(
      CallCallee(GetMember(inst, "add_infer"), {Value(1), Value(2)}));
  EXPECT_EQ(CallCallee(GetMember(inst, "sum")), 12);

  EXPECT_THROW(std::ignore = CallCallee(GetMember(inst, "add_infer"), {},
                                        {{"dx", Value(1)}}),
               error_type)
      << "self should not be part of inferred positional-only method spec";
}

struct P {
  int x, y;
  P(int x_, int y_) : x(x_), y(y_) {}
  int sum() const { return x + y; }
};

TEST_F(SrbindTest, Class_Init) {
  class_<P> cp(mod, "P");
  cp.def(init<int, int>(), arg("x") = 10, arg("y")).def("sum", &P::sum);

  Value klass = dict["P"];

  // Allocate instance by calling class
  // Call __init__(self, y=5)  -> x=10 (default), y=5
  Value inst_v = CallCallee(klass, {}, {{"y", Value(5)}});
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);

  Value sum_fn = GetMember(inst, "sum");
  Value sum_ret = CallCallee(sum_fn);
  EXPECT_TRUE(sum_ret == 15);

  // Missing required argument
  EXPECT_THROW(std::ignore = CallCallee(klass, {}, {{"x", Value(5)}}),
               error_type)
      << "Expected missing required argument 'y'";
}

static int mul_fn(int a, int b) { return a * b; }
static const char* hi_fn() { return "hi"; }

struct W {
  int v{0};
  explicit W(int vv) : v(vv) {}
  int get() const { return v; }
};

struct B1 {
  int x{0};
  explicit B1(int xx) : x(xx) {}
  virtual ~B1() = default;  // ensure safe delete via base*
  int val() const { return x; }
  void set(int x_in) { x = x_in; }
};
struct D1 : B1 {
  using B1::B1;
};

struct U {
  int x{0};
  explicit U(int xx) : x(xx) {}
};

struct SingletonCounter {
  int value{0};

  void add(int delta) { value += delta; }
  int get() const { return value; }
};

struct LifetimeTracked {
  static int& aliveCount() {
    static int count = 0;
    return count;
  }

  int value{0};

  explicit LifetimeTracked(int value_in) : value(value_in) { ++aliveCount(); }
  ~LifetimeTracked() { --aliveCount(); }

  int get() const { return value; }
};

TEST_F(SrbindTest, FreeFunction_PlainFunctionPointer) {
  NativeFunction* nf =
      make_function(gc.get(), "mul", &mul_fn);  // fn ptr, no spec
  Value r = CallCallee(Value(nf), {Value(2), Value(3)});
  EXPECT_EQ(r, 6);

  EXPECT_THROW(std::ignore = CallCallee(Value(nf), {}, {{"a", Value(1)}}),
               error_type)
      << "kwargs not allowed without spec";
}

TEST_F(SrbindTest, FreeFunction_CastsCStringToString) {
  NativeFunction* nf = make_function(gc.get(), "hi", &hi_fn);
  Value r = CallCallee(Value(nf));
  EXPECT_EQ(r, "hi");
}

TEST_F(SrbindTest, FreeFunction_InferredVarargAndKwargSpec) {
  // return a + 10 * |varargs| + 100 * |kwargs|
  auto f = [](int a, std::vector<Value> varargs,
              std::unordered_map<std::string, Value> kwargs) {
    return a + 10 * static_cast<int>(varargs.size()) +
           100 * static_cast<int>(kwargs.size());
  };
  NativeFunction* nf = make_function(gc.get(), "count_all", f);  // infer spec
  // a=1, rest=[2,3], kwargs={x:4, y:5}
  Value r = CallCallee(Value(nf), {Value(1), Value(2), Value(3)},
                       {{"x", Value(4)}, {"y", Value(5)}});
  EXPECT_EQ(r, 1 + 10 * 2 + 100 * 2);

  // Too many args only if no vararg slot — we have one; but unknown kwargs are
  // fine only if kwarg slot present Drop kwarg slot by using a different
  // callable would error; here ensure extra kwargs are accepted
  Value r2 = CallCallee(Value(nf), {Value(7)},
                        {{"k1", Value(1)}, {"k2", Value(2)}, {"k3", Value(3)}});
  EXPECT_EQ(r2, 7 + 10 * 0 + 100 * 3);
}

TEST_F(SrbindTest, FreeFunction_InferredVmFib) {
  serilang::VM* vm_ptr = &vm;
  serilang::Fiber* fib_ptr = f;
  const int value = 123;

  auto vmfib_fn = [&](serilang::VM& vm, serilang::Fiber& f, int a) {
    EXPECT_EQ(&vm, vm_ptr);
    EXPECT_EQ(&f, fib_ptr);
    EXPECT_EQ(a, value);
  };
  auto vm_fn = [&](serilang::VM& vm, int a) {
    EXPECT_EQ(&vm, vm_ptr);
    EXPECT_EQ(a, value);
  };
  auto fib_fn = [&](serilang::Fiber& f, int a) {
    EXPECT_EQ(&f, fib_ptr);
    EXPECT_EQ(a, value);
  };

  NativeFunction* vmfib_nf = make_function(gc.get(), "vmfib", vmfib_fn);
  NativeFunction* vm_nf = make_function(gc.get(), "vm", vm_fn);
  NativeFunction* fib_nf = make_function(gc.get(), "fib", fib_fn);

  CallCallee(Value(vmfib_nf), {Value(value)});
  CallCallee(Value(vm_nf), {Value(value)});
  CallCallee(Value(fib_nf), {Value(value)});
}

TEST_F(SrbindTest, Class_InitFactory) {
  class_<W> cw(mod, "W");
  auto factory = [](int x) { return std::make_unique<W>(x); };
  cw.def(init(factory))  // no arg spec -> positional only
      .def("get", &W::get);

  Value klass = dict["W"];
  Value inst_v = CallCallee(klass, {Value(42)});  // W(42)
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);

  Value get_fn = GetMember(inst, "get");
  Value r = CallCallee(get_fn);
  EXPECT_EQ(r, 42);

  EXPECT_THROW(std::ignore = CallCallee(klass), error_type)
      << "Missing required arg because positional-only in inferred spec";
}

TEST_F(SrbindTest, Class_InitFactory_WithArgSpec) {
  class_<W> cw(mod, "W");
  auto factory = [](int x) { return std::make_unique<W>(x); };
  // Allow kwargs and a default
  cw.def(init(factory), arg("x") = 7).def("get", &W::get);

  Value klass = dict["W"];

  // Use default (x=7)
  Value inst1_v = CallCallee(klass);
  auto* inst1 = inst1_v.Get_if<NativeInstance>();
  ASSERT_NE(inst1, nullptr);
  Value get1 = GetMember(inst1, "get");
  EXPECT_EQ(CallCallee(get1), 7);

  // Override via kw
  Value inst2_v = CallCallee(klass, {}, {{"x", Value(123)}});
  auto* inst2 = inst2_v.Get_if<NativeInstance>();
  ASSERT_NE(inst2, nullptr);
  Value get2 = GetMember(inst2, "get");
  EXPECT_EQ(CallCallee(get2), 123);

  EXPECT_THROW(std::ignore = CallCallee(klass, {}, {{"y", Value(1)}}),
               error_type)
      << "Unexpected kw should error";
}

TEST_F(SrbindTest, Class_InitFactory_ReturnsNull) {
  class_<U> cu(mod, "U");
  auto bad_factory_ptr = [](int x) -> U* {
    (void)x;
    return nullptr;
  };
  cu.def(init(bad_factory_ptr), arg("x"));

  Value klass = dict["U"];
  EXPECT_THROW(std::ignore = CallCallee(klass, {Value(9)}), error_type);
}

TEST_F(SrbindTest, Class_InitFactory_MissingOrWrongSelf) {
  class_<W> cw(mod, "W");
  auto factory = [](int x) { return std::make_unique<W>(x); };
  cw.def(init(factory), arg("x"));

  Value klass = dict["W"];
  auto* cls = klass.Get_if<NativeClass>();
  ASSERT_NE(cls, nullptr);
  auto it = cls->methods.find("__init__");
  ASSERT_TRUE(it != cls->methods.end());
  auto* init_nf = it->second.Get_if<NativeFunction>();
  ASSERT_NE(init_nf, nullptr);

  // Missing self
  EXPECT_THROW(std::ignore = CallCallee(Value(init_nf), {Value(1)}),
               error_type);

  // Wrong self type (int instead of NativeInstance)
  EXPECT_THROW(std::ignore = CallCallee(Value(init_nf), {Value(0), Value(1)}),
               error_type);
}

TEST_F(SrbindTest, Class_DoubleInit) {
  class_<W> cw(mod, "W");
  auto factory = [](int x) { return std::make_unique<W>(x); };
  cw.def(init(factory), arg("x")).def("get", &W::get);

  Value klass = dict["W"];
  Value inst_v = CallCallee(klass, {Value(5)});
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);

  Value init_bound = GetMember(inst, "__init__");
  EXPECT_THROW(std::ignore = CallCallee(init_bound, {Value(9)}), error_type)
      << "Calling __init__ twice on same instance should error";
}

TEST_F(SrbindTest, Class_InitDerived) {
  class_<B1> cb(mod, "B1");
  auto factory = [](int x) -> B1* { return new D1(x); };  // Derived*
  cb.def(init(factory), arg("x"))
      .def("val", &B1::val)
      .def("set", &B1::set, arg("val") = 0);

  Value klass = dict["B1"];
  Value inst_v = CallCallee(klass, {Value(77)});
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);

  Value val_fn = GetMember(inst, "val");
  EXPECT_EQ(CallCallee(val_fn), 77);

  Value set_fn = GetMember(inst, "set");
  EXPECT_EQ(CallCallee(set_fn), std::monostate());
  EXPECT_EQ(CallCallee(val_fn), 0);
}

struct SubParent {
  int value{0};
  explicit SubParent(int value_in) : value(value_in) {}
};
struct FactoryParent {
  std::shared_ptr<int> value;
};
struct FactoryChild {
  std::shared_ptr<int> value;
  int get() const { return *value; }
  void set(int v) { *value = v; }
};

TEST_F(SrbindTest, FactoryCreatesHandlesLazilyWithSharedBackingState) {
  int creations = 0;
  class_<FactoryChild> child(mod, "FactoryChild", false);
  child.def("get", &FactoryChild::get).def("set", &FactoryChild::set);
  class_<FactoryParent> parent(mod, "FactoryParent");
  parent.def(
      init([](int value) {
        return std::make_unique<FactoryParent>(std::make_shared<int>(value));
      }),
      arg("value"));
  parent.add_gc_root(child);
  parent.def("child", [child, &creations](FactoryParent* h) mutable {
    ++creations;
    return Value(child.make_inst(h->value));
  });

  auto* inst =
      CallCallee(dict["FactoryParent"], {Value(41)}).Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_EQ(creations, 0);
  auto* first = CallCallee(GetMember(inst, "child")).Get_if<NativeInstance>();
  auto* second = CallCallee(GetMember(inst, "child")).Get_if<NativeInstance>();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first, second);
  EXPECT_EQ(creations, 2);
  CallCallee(GetMember(first, "set"), {Value(42)});
  EXPECT_EQ(CallCallee(GetMember(second, "get")), 42);
}

TEST_F(SrbindTest, FactoryHandleOwnsBackingStateAfterParentCollection) {
  module_ globals_mod(vm.gc_.get(), vm.globals_.get());
  class_<FactoryChild> child(globals_mod, "FactoryOwnedChild", false);
  child.def("get", &FactoryChild::get);
  class_<FactoryParent> parent(globals_mod, "FactoryOwnedParent");
  parent.add_gc_root(child);
  parent.def("child", [child](FactoryParent* h) mutable {
    return Value(child.make_inst(h->value));
  });
  auto owner = std::make_shared<int>(55);
  auto* inst = parent.make_inst(owner);
  (*vm.globals_)["saved_child"] = CallCallee(GetMember(inst, "child"));
  owner.reset();
  f->op_stack.clear();
  vm.last_ = Value();
  vm.fibres_.push_back(f);
  vm.CollectGarbage();
  auto* retained = (*vm.globals_)["saved_child"].Get_if<NativeInstance>();
  ASSERT_NE(retained, nullptr);
  EXPECT_EQ(CallCallee(GetMember(retained, "get")), 55);
}

TEST_F(SrbindTest, FactoryClassSurvivesCollectionBeforeFirstCall) {
  module_ globals_mod(vm.gc_.get(), vm.globals_.get());
  class_<FactoryChild> child(globals_mod, "FactoryRootedChild", false);
  child.def("get", &FactoryChild::get);
  class_<FactoryParent> parent(globals_mod, "FactoryRootedParent");
  parent.add_gc_root(child);
  parent.def("child", [child](FactoryParent* h) mutable {
    return Value(child.make_inst(h->value));
  });
  (*vm.globals_)["factory_parent"] =
      parent.make_inst(std::make_shared<int>(77));
  vm.fibres_.push_back(f);
  vm.CollectGarbage();
  auto* inst = (*vm.globals_)["factory_parent"].Get_if<NativeInstance>();
  auto* result = CallCallee(GetMember(inst, "child")).Get_if<NativeInstance>();
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(CallCallee(GetMember(result, "get")), 77);
}

TEST_F(SrbindTest, NativeInstanceRejectsMemberAssignment) {
  class_<SubParent> parent(mod, "NativeWithoutFields");
  parent.def("get", [](SubParent* h) { return h->value; });
  auto* inst = parent.make_inst(41);
  EXPECT_THROW(inst->SetMember("value", Value(9)), error_type);
  EXPECT_THROW(inst->SetMember("get", Value(9)), error_type);
  EXPECT_THROW(std::ignore = GetMember(inst, "value"), error_type);
  EXPECT_EQ(CallCallee(GetMember(inst, "get")), 41);
}

TEST_F(SrbindTest, AddGcRootKeepsHiddenFactoryClassAlive) {
  LifetimeTracked::aliveCount() = 0;

  module_ globals_mod(vm.gc_.get(), vm.globals_.get());
  class_<LifetimeTracked> child(globals_mod, "__FactoryChildRooted", false);
  child.def("get", &LifetimeTracked::get);

  class_<SubParent> parent(globals_mod, "FactoryParentRoot");
  parent.add_gc_root(child).def(init<int>(), arg("value"));

  ASSERT_FALSE(vm.globals_->contains("__FactoryChildRooted"));

  vm.fibres_.push_back(f);
  vm.CollectGarbage();

  Value child_v(child.make_inst(88));
  (*vm.globals_)["factory_child"] = child_v;
  auto* child_inst = child_v.Get_if<NativeInstance>();
  ASSERT_NE(child_inst, nullptr);
  ASSERT_NE(child_inst->klass, nullptr);
  EXPECT_EQ(child_inst->klass->name, "__FactoryChildRooted");
  EXPECT_EQ(CallCallee(GetMember(child_inst, "get")), 88);
  EXPECT_EQ(LifetimeTracked::aliveCount(), 1);

  vm.globals_->erase("factory_child");
  vm.last_ = Value();
  f->op_stack.clear();
  vm.CollectGarbage();
  EXPECT_EQ(LifetimeTracked::aliveCount(), 0);
}

TEST_F(SrbindTest, FreeFunction_PlainFunctionPointer_ArgSpecWithKw) {
  NativeFunction* nf =
      make_function(gc.get(), "mul_kw", &mul_fn, arg("a"), arg("b"));
  Value r = CallCallee(Value(nf), {}, {{"b", Value(8)}, {"a", Value(7)}});
  EXPECT_EQ(r, 56);
}

TEST_F(SrbindTest, Class_DeduceMemberSpec) {
  class_<B1> cb(mod, "B1");
  cb.def(init<int>()).def("get", &B1::val).def("set", &B1::set);

  Value klass = dict["B1"];
  Value inst_v = CallCallee(klass, {Value(77)});
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_THROW(std::ignore = CallCallee(klass, {}, {{"x", Value(1)}}),
               error_type)
      << "should reject unexpected kwargs when no names were provided";

  EXPECT_NO_THROW(CallCallee(GetMember(inst, "set"), {Value(88)}));
  EXPECT_EQ(CallCallee(GetMember(inst, "get")), 88);
}

TEST_F(SrbindTest, Class_MemberFnWithVmFib) {
  const int val(123);

  struct vmfib {
    serilang::VM* vm_ptr = nullptr;
    serilang::Fiber* fib_ptr = nullptr;
    int value = 0;

    void SetVm(serilang::VM& vm, int val) {
      EXPECT_EQ(&vm, vm_ptr);
      EXPECT_EQ(val, value);
    }
    void SetFib(serilang::Fiber& f, int val) {
      EXPECT_EQ(&f, fib_ptr);
      EXPECT_EQ(val, value);
    }
    void SetVmFib(serilang::VM& vm, serilang::Fiber& f, int val) {
      EXPECT_EQ(&vm, vm_ptr);
      EXPECT_EQ(&f, fib_ptr);
      EXPECT_EQ(val, value);
    }
  };

  class_<vmfib> cb(mod, "vmfib_class");
  cb.def(init([&]() {
      return new vmfib{.vm_ptr = &vm, .fib_ptr = f, .value = val};
    }))
      .def("set_vm", &vmfib::SetVm)
      .def("set_fib", &vmfib::SetFib)
      .def("set_vmfib", &vmfib::SetVmFib)
      .def("set_vm_named", &vmfib::SetVm, arg("val"))
      .def("set_fib_named", &vmfib::SetFib, arg("val"))
      .def("set_vmfib_named", &vmfib::SetVmFib, arg("val"));

  Value klass = dict["vmfib_class"];
  Value inst_v = CallCallee(klass);
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_NO_THROW(CallCallee(GetMember(inst, "set_vm"), {Value(val)}));

  inst_v = CallCallee(klass);
  inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_NO_THROW(CallCallee(GetMember(inst, "set_fib"), {Value(val)}));

  inst_v = CallCallee(klass);
  inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_NO_THROW(CallCallee(GetMember(inst, "set_vmfib"), {Value(val)}));

  inst_v = CallCallee(klass);
  inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_NO_THROW(
      CallCallee(GetMember(inst, "set_vm_named"), {}, {{"val", Value(val)}}));

  inst_v = CallCallee(klass);
  inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_NO_THROW(
      CallCallee(GetMember(inst, "set_fib_named"), {}, {{"val", Value(val)}}));

  inst_v = CallCallee(klass);
  inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_NO_THROW(CallCallee(GetMember(inst, "set_vmfib_named"), {},
                             {{"val", Value(val)}}));
}

TEST_F(SrbindTest, Class_SelfFirstFreeFunctionWithVmFib) {
  class_<SingletonCounter> cb(mod, "CounterVmFibSelf");
  auto set = [&](SingletonCounter* self, serilang::VM& got_vm,
                 serilang::Fiber& got_fib, int value) {
    EXPECT_EQ(&got_vm, &vm);
    EXPECT_EQ(&got_fib, f);
    self->value = value;
  };
  auto get = [&](const SingletonCounter* self, serilang::VM& got_vm,
                 serilang::Fiber& got_fib) {
    EXPECT_EQ(&got_vm, &vm);
    EXPECT_EQ(&got_fib, f);
    return self->value;
  };
  cb.def(init<>())
      .def("set", set)
      .def("set_named", set, arg("value"))
      .def("get", get);

  Value klass = dict["CounterVmFibSelf"];
  Value inst_v = CallCallee(klass);
  auto* inst = inst_v.Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);

  EXPECT_NO_THROW(CallCallee(GetMember(inst, "set"), {Value(123)}));
  EXPECT_EQ(CallCallee(GetMember(inst, "get")), 123);

  EXPECT_NO_THROW(
      CallCallee(GetMember(inst, "set_named"), {}, {{"value", Value(77)}}));
  EXPECT_EQ(CallCallee(GetMember(inst, "get")), 77);
}

TEST_F(SrbindTest, Module_BindInstance_ByReference) {
  SingletonCounter counter{.value = 5};

  mod.bind_instance("counter", counter)
      .def("add", &SingletonCounter::add, arg("delta"))
      .def("get", &SingletonCounter::get);

  ASSERT_TRUE(dict.contains("counter"));
  EXPECT_EQ(dict.at("counter").Get_if<NativeClass>(), nullptr);

  auto* inst = dict.at("counter").Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_EQ(inst->GetForeign<SingletonCounter>(), &counter);

  EXPECT_NO_THROW(CallCallee(GetMember(inst, "add"), {Value(7)}));
  EXPECT_EQ(counter.value, 12);
  EXPECT_EQ(CallCallee(GetMember(inst, "get")), 12);
}

TEST_F(SrbindTest, Module_BindInstance_SelfFirstFreeFunctionMethods) {
  SingletonCounter counter{.value = 5};

  mod.bind_instance("counter_self", counter)
      .def(
          "add",
          [](SingletonCounter* self, int delta) { self->value += delta; },
          arg("delta"))
      .def("get", [](const SingletonCounter* self) { return self->value; });

  auto* inst = dict.at("counter_self").Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);

  EXPECT_NO_THROW(CallCallee(GetMember(inst, "add"), {Value(7)}));
  EXPECT_EQ(counter.value, 12);
  EXPECT_EQ(CallCallee(GetMember(inst, "get")), 12);
}

TEST_F(SrbindTest, Module_BindInstance_KeepsNativeClassAliveWhileRooted) {
  SingletonCounter counter{.value = 17};

  module_ globals_mod(vm.gc_.get(), vm.globals_.get());
  globals_mod.bind_instance("counter", counter)
      .def("get", &SingletonCounter::get);

  vm.fibres_.push_back(f);

  const size_t bytes_before_gc = gc->AllocatedBytes();
  vm.CollectGarbage();
  EXPECT_EQ(gc->AllocatedBytes(), bytes_before_gc);

  auto* inst = (*vm.globals_)["counter"].Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_EQ(CallCallee(GetMember(inst, "get")), 17);
}

TEST_F(SrbindTest, Module_BindInstance_ByReferenceDoesNotDeleteForeign) {
  LifetimeTracked::aliveCount() = 0;
  module_ globals_mod(vm.gc_.get(), vm.globals_.get());

  {
    LifetimeTracked tracked(9);
    EXPECT_EQ(LifetimeTracked::aliveCount(), 1);

    globals_mod.bind_instance("tracked", tracked)
        .def("get", &LifetimeTracked::get);

    auto* inst = (*vm.globals_)["tracked"].Get_if<NativeInstance>();
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(CallCallee(GetMember(inst, "get")), 9);

    vm.globals_->clear();
    vm.last_ = Value();
    vm.CollectGarbage();
    EXPECT_EQ(LifetimeTracked::aliveCount(), 1);
  }

  EXPECT_EQ(LifetimeTracked::aliveCount(), 0);
}

TEST_F(SrbindTest, Module_BindInstance_UniquePtrTransfersOwnership) {
  LifetimeTracked::aliveCount() = 0;
  module_ globals_mod(vm.gc_.get(), vm.globals_.get());

  globals_mod.bind_instance("owned", std::make_unique<LifetimeTracked>(33))
      .def("get", &LifetimeTracked::get);

  auto* inst = (*vm.globals_)["owned"].Get_if<NativeInstance>();
  ASSERT_NE(inst, nullptr);
  EXPECT_EQ(CallCallee(GetMember(inst, "get")), 33);
  EXPECT_EQ(LifetimeTracked::aliveCount(), 1);

  vm.globals_->clear();
  vm.last_ = Value();
  vm.CollectGarbage();
  EXPECT_EQ(LifetimeTracked::aliveCount(), 0);
}

}  // namespace srbind_test
