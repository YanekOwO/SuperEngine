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

#include "srbind/function.hpp"
#include "srbind/method.hpp"
#include "vm/gc.hpp"
#include "vm/object.hpp"
#include "vm/value.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace srbind {

template <class T>
class instance_;

// -------------------------------------------------------------
// module: where we register functions/classes/native instances
// -------------------------------------------------------------
class module_ {
  serilang::GarbageCollector* gc_;
  std::unordered_map<std::string, Value>* dict_;

 public:
  module_(serilang::GarbageCollector* gc,
          std::unordered_map<std::string, Value>* dict)
      : gc_(gc), dict_(dict) {}
  module_(serilang::VM& vm, char const* name) : gc_(vm.gc_.get()) {
    serilang::Module* mod = gc_->Allocate<serilang::Module>(name);
    dict_ = mod->globals.get();
    (*vm.builtins_)[std::string(name)] = Value(mod);
  }

  template <class T, class F, class... A>
    requires std::constructible_from<std::string, T>
  module_& def(T&& name, F&& f, A&&... a) {
    std::string n(std::forward<T>(name));
    (*dict_)[n] =
        Value(make_function(gc_, n, std::forward<F>(f), std::forward<A>(a)...));
    return *this;
  }

  template <class T>
  instance_<T> bind_instance(const char* name, T* obj);

  template <class T>
  instance_<T> bind_instance(const char* name, T& obj);

  template <class T>
  instance_<T> bind_instance(const char* name, std::unique_ptr<T> obj);

  inline std::unordered_map<std::string, Value>* dict() const { return dict_; }
  inline serilang::GarbageCollector* gc() const { return gc_; }
};

// -------------------------------------------------------------
// init helpers -> binds __init__ to construct T inside NativeInstance
// -------------------------------------------------------------
template <class... Args>
struct init_t {};

// usage: class_<T>(...).def(init<Args...>(), arg("x")=..., ...)
template <class... Args>
constexpr init_t<Args...> init() {
  return {};
}

template <class F>
struct init_factory_t {
  F factory;
};

// usage: class_<T>(...).def(init([](Args...){...}), arg("x")=..., ...)
template <class F>
constexpr init_factory_t<std::decay_t<F>> init(F&& f) {
  return {std::forward<F>(f)};
}

namespace detail {
// pointer conversion helper
template <class T, class R>
static T* convert_factory_return(R&& r) {
  using RD = std::decay_t<R>;
  if constexpr (std::is_pointer_v<RD>) {
    using Pointee = std::remove_pointer_t<RD>;
    static_assert(
        std::is_base_of_v<T, Pointee>,
        "Factory must return (derived) T* or std::unique_ptr<derived T>");
    return r;
  } else if constexpr (is_unique_ptr_v<RD>) {
    using E = typename RD::element_type;
    static_assert(std::is_base_of_v<T, E>,
                  "unique_ptr element must be T or derived from T");
    static_assert(std::is_same_v<typename is_unique_ptr<RD>::deleter_type,
                                 std::default_delete<E>>,
                  "unique_ptr deleter must be std::default_delete");

    return r.release();  // transfer ownership to engine (deleted by finalize)
  } else {
    static_assert(always_false<RD>,
                  "Unsupported factory return type. Use T* or "
                  "std::unique_ptr<T>.");
    return nullptr;
  }
}

}  // namespace detail

// -------------------------------------------------------------
// instance_<T>: binds a single pre-existing native object as a
// NativeInstance in the module dict
// -------------------------------------------------------------
template <class T>
class instance_ {
  serilang::GarbageCollector* gc_;
  serilang::NativeClass* cls_;
  serilang::NativeInstance* inst_;

 public:
  instance_(serilang::GarbageCollector* gc,
            serilang::NativeClass* cls,
            serilang::NativeInstance* inst)
      : gc_(gc), cls_(cls), inst_(inst) {}

  instance_& no_delete() {
    cls_->finalize = nullptr;
    return *this;
  }

  template <class M, class... A>
  instance_& def(const char* name, M pmf, A&&... a) {
    arglist_spec spec = detail::parse_method_spec<T, M>(std::forward<A>(a)...);

    cls_->methods[name] =
        Value(make_method<T>(gc_, name, pmf, std::move(spec)));
    return *this;
  }
};

// -------------------------------------------------------------
// class_<T>: binds a NativeClass container + methods + __init__
// -------------------------------------------------------------
template <class T>
class class_ {
  template <class>
  friend class class_;

  module_& m_;
  serilang::GarbageCollector* gc_;
  serilang::NativeClass* cls_;
  static void finalize_T(void* p) { delete static_cast<T*>(p); }

 public:
  class_(module_& m, const char* name, bool should_register = true)
      : m_(m), gc_(m.gc()) {
    cls_ = gc_->Allocate<serilang::NativeClass>();
    cls_->name = name;
    cls_->finalize = &finalize_T;
    if (should_register)
      (*m.dict())[std::string(name)] = Value(cls_);
  }

  class_& no_delete() {
    cls_->finalize = nullptr;
    return *this;
  }

  inline serilang::NativeClass* native_class() const { return cls_; }

  template <class Child>
  class_& add_gc_root(class_<Child>& child_class) {
    if (child_class.gc_ != gc_)
      throw type_error(
          "gc root class belongs to a different garbage collector");

    serilang::NativeClass* child_cls = child_class.cls_;
    if (std::ranges::find(cls_->gc_roots, child_cls) == cls_->gc_roots.cend())
      cls_->gc_roots.emplace_back(child_cls);
    return *this;
  }

  // __init__ via operator new
  template <class... Args, class... A>
  class_& def(init_t<Args...>, A&&... a) {
    arglist_spec spec = parse_spec<T*(Args...)>(std::forward<A>(a)...);

    auto* nf = gc_->Allocate<serilang::NativeFunction>(
        "__init__",
        [spec = std::move(spec)](serilang::VM& vm, serilang::Fiber& f,
                                 uint8_t nargs,
                                 uint8_t nkwargs) -> serilang::TempValue {
          try {
            if (nargs < 1)
              throw type_error("missing 'self'");
            Value selfv = std::move(f.op_stack.end()[-nargs - 2 * nkwargs]);
            auto* self = selfv.Get_if<serilang::NativeInstance>();
            if (!self)
              throw type_error("self is not a native instance");
            auto tup = load_args<Args...>(f.op_stack, nargs - 1, nkwargs, spec);
            f.op_stack.pop_back();  // self

            if (self->foreign)
              throw type_error("__init__ called twice");
            T* obj = std::apply(
                [](auto&&... a) {
                  return new T(std::forward<decltype(a)>(a)...);
                },
                std::move(tup));
            self->SetForeign<T>(obj);
            return serilang::nil;
          } catch (const type_error& e) {
            throw serilang::RuntimeError(e.what());
          } catch (const std::exception& e) {
            throw serilang::RuntimeError(e.what());
          }
        });
    cls_->methods["__init__"] = Value(nf);
    return *this;
  }

  // __init__ via native factory
  // Factory signature:  R(Args...) where R is T* or std::unique_ptr<T/Derived>
  template <class F, class... A>
  class_& def(init_factory_t<F> tag, A&&... a) {
    arglist_spec spec = parse_spec<F>(std::forward<A>(a)...);

    auto* nf = gc_->Allocate<serilang::NativeFunction>(
        "__init__",
        [factory = std::move(tag.factory), spec = std::move(spec)](
            serilang::VM& vm, serilang::Fiber& fib, uint8_t nargs,
            uint8_t nkwargs) -> serilang::TempValue {
          try {
            if (nargs < 1)
              throw type_error("missing 'self'");
            Value selfv = std::move(fib.op_stack.end()[-nargs - 2 * nkwargs]);
            auto* self = selfv.Get_if<serilang::NativeInstance>();
            if (!self)
              throw type_error("self is not a native instance");
            if (self->foreign)
              throw type_error("__init__ called twice");

            auto r =
                detail::invoke_free(vm, fib, nargs - 1, nkwargs, factory, spec);
            fib.op_stack.pop_back();  // self
            T* raw = detail::convert_factory_return<T>(std::move(r));
            if (!raw)
              throw type_error("factory returned null");
            self->SetForeign<T>(raw);

            return serilang::nil;
          } catch (const type_error& e) {
            throw serilang::RuntimeError(e.what());
          } catch (const std::exception& e) {
            throw serilang::RuntimeError(e.what());
          }
        });
    cls_->methods["__init__"] = Value(nf);
    return *this;
  }

  template <class M, class... A>
  class_& def(const char* name, M pmf, A&&... a) {
    arglist_spec spec = detail::parse_method_spec<T, M>(std::forward<A>(a)...);

    cls_->methods[name] =
        Value(make_method<T>(gc_, name, pmf, std::move(spec)));
    return *this;
  }

  template <class F, class... A>
  class_& def_static(const char* name, F&& f, A&&... a) {
    cls_->methods[name] = Value(
        make_function(gc_, name, std::forward<F>(f), std::forward<A>(a)...));
    return *this;
  }

  template <class... A>
  auto make_inst(A&&... a) -> serilang::NativeInstance* {
    serilang::NativeInstance* inst_ =
        gc_->Allocate<serilang::NativeInstance>(cls_);
    T* obj = new T(std::forward<A>(a)...);
    inst_->SetForeign<T>(obj);
    return inst_;
  }
  template <class... A>
  auto inst(std::string_view name, A&&... a) -> instance_<T> {
    auto inst_ = make_inst(std::forward<A>(a)...);
    (*m_.dict())[std::string(name)] = Value(inst_);
    return instance_<T>(gc_, cls_, inst_);
  }
};

template <typename T>
static void finalize_T(void* p) {
  delete static_cast<T*>(p);
}

template <class T>
std::pair<serilang::NativeClass*, serilang::NativeInstance*>
create_instance(std::string name, T* obj, serilang::GarbageCollector* gc) {
  serilang::NativeClass* cls = gc->Allocate<serilang::NativeClass>();
  serilang::NativeInstance* inst = gc->Allocate<serilang::NativeInstance>(cls);
  cls->name = std::move(name);
  inst->SetForeign<T>(obj);
  cls->finalize = &finalize_T<T>;
  return std::make_pair(cls, inst);
}

template <class T>
instance_<T> module_::bind_instance(const char* name, T* obj) {
  auto [cls, inst] = create_instance(name, obj, gc_);
  cls->finalize = nullptr;
  (*dict_)[std::string(name)] = Value(inst);
  return instance_<T>(gc_, cls, inst);
}

template <class T>
instance_<T> module_::bind_instance(const char* name, T& obj) {
  return bind_instance(name, std::addressof(obj));
}

template <class T>
instance_<T> module_::bind_instance(const char* name, std::unique_ptr<T> obj) {
  T* ptr = obj.get();
  auto [cls, inst] = create_instance(name, ptr, gc_);
  (*dict_)[std::string(name)] = Value(inst);
  obj.release();
  return instance_<T>(gc_, cls, inst);
}

}  // namespace srbind
