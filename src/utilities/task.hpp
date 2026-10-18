// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace task_detail {

struct PromiseBase {
  inline std::suspend_always initial_suspend() const noexcept { return {}; }

  struct FinalAwaiter {
    inline bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<Promise> handle) const noexcept {
      return handle.promise().continuation;
    }

    inline void await_resume() const noexcept {}
  };

  inline FinalAwaiter final_suspend() const noexcept { return {}; }

  inline void unhandled_exception() noexcept {
    exception = std::current_exception();
  }

  inline void ConsumeResult() {
    if (std::exchange(result_taken, true))
      throw std::logic_error("Task result has already been consumed");
    if (exception)
      std::rethrow_exception(exception);
  }

  std::exception_ptr exception;
  std::coroutine_handle<> continuation = std::noop_coroutine();
  bool started = false;
  bool result_taken = false;
};

template <typename T>
struct PromiseResult : PromiseBase {
  template <typename U>
    requires std::constructible_from<T, U&&>
  void return_value(U&& value) {
    result.emplace(std::forward<U>(value));
  }

  T TakeResult() {
    ConsumeResult();
    return std::move(result.value());
  }

  std::optional<T> result;
};

template <>
struct PromiseResult<void> : PromiseBase {
  void return_void() const noexcept {}
  void TakeResult() { ConsumeResult(); }
};

}  // namespace task_detail

// A lazy, single-consumer native coroutine. Start() starts an externally driven
// root once; outstanding awaitables are responsible for resuming it afterward.
// co_await consumes a cold or completed task and transfers ownership to the
// awaiting frame. Started, suspended tasks cannot change their driver this way.
// All operations run on one thread. Destroying a task destroys its frame;
// awaitables must unregister or invalidate their external callbacks on
// teardown.
template <typename T = void>
class [[nodiscard]] Task {
  static_assert(std::is_void_v<T> ||
                    (std::is_object_v<T> && std::is_move_constructible_v<T>),
                "Task<T> requires void or a move-constructible object type");

 public:
  struct promise_type;

 private:
  using Handle = std::coroutine_handle<promise_type>;

 public:
  struct promise_type : task_detail::PromiseResult<T> {
    Task get_return_object() noexcept {
      return Task(Handle::from_promise(*this));
    }
  };

  Task() noexcept = default;
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  Task& operator=(Task&& other) noexcept {
    if (this != &other) {
      Destroy();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~Task() { Destroy(); }

  explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

  bool Done() const noexcept { return !handle_ || handle_.done(); }

  // Never resumes a task through a wait that has not completed.
  void Start() {
    RequireHandle();
    if (handle_.promise().started)
      return;
    handle_.promise().started = true;
    handle_.resume();
  }

  // Values and exceptions are consumed exactly once after final suspension.
  T TakeResult() {
    RequireHandle();
    if (!handle_.done())
      throw std::logic_error("Task result is not ready");
    return handle_.promise().TakeResult();
  }

  class Awaiter {
   public:
    Awaiter(const Awaiter&) = delete;
    Awaiter& operator=(const Awaiter&) = delete;
    Awaiter(Awaiter&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}
    Awaiter& operator=(Awaiter&&) = delete;

    ~Awaiter() {
      if (handle_)
        handle_.destroy();
    }

    bool await_ready() const noexcept { return handle_.done(); }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> continuation) noexcept {
      handle_.promise().continuation = continuation;
      handle_.promise().started = true;
      return handle_;
    }

    T await_resume() { return handle_.promise().TakeResult(); }

   private:
    friend class Task;
    explicit Awaiter(Handle handle) noexcept : handle_(handle) {}

    Handle handle_;
  };

  Awaiter operator co_await() && {
    RequireHandle();
    if (handle_.promise().result_taken)
      throw std::logic_error("Task result has already been consumed");
    if (handle_.promise().started && !handle_.done())
      throw std::logic_error("Cannot await a started suspended Task");
    return Awaiter(std::exchange(handle_, {}));
  }

  Awaiter operator co_await() & = delete;
  Awaiter operator co_await() const& = delete;

 private:
  explicit Task(Handle handle) noexcept : handle_(handle) {}

  inline void RequireHandle() const {
    if (!handle_)
      throw std::logic_error("Task is empty");
  }

  inline void Destroy() noexcept {
    if (handle_)
      handle_.destroy();
  }

  Handle handle_;
};
