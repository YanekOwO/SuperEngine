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

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <functional>
#include <memory>
#include <utility>

namespace lop {

enum class WaitOutcome { Timeout, InterruptedByInput };

class IWaitRegistration {
 public:
  virtual ~IWaitRegistration() = default;
};

class IWaitBackend {
 public:
  using Completion = std::function<void(WaitOutcome)>;
  virtual ~IWaitBackend() = default;
  // Complete at most once, on the driving thread, after Register returns.
  // Destroying the registration makes outstanding completions inert.
  virtual std::unique_ptr<IWaitRegistration> Register(
      std::chrono::milliseconds delay,
      bool interrupt_on_input,
      Completion completion) = 0;
};

class WaitContext {
 public:
  using Resume = std::function<void(std::coroutine_handle<>)>;

  explicit WaitContext(
      IWaitBackend& backend,
      Resume resume = [](std::coroutine_handle<> h) { h.resume(); })
      : backend_(backend), resume_(std::move(resume)) {}

  WaitContext(const WaitContext&) = delete;
  WaitContext& operator=(const WaitContext&) = delete;
  WaitContext(WaitContext&&) = delete;
  WaitContext& operator=(WaitContext&&) = delete;

  class Awaiter {
   public:
    Awaiter(WaitContext& context,
            std::chrono::milliseconds delay,
            bool interrupt_on_input)
        : context_(context),
          delay_(std::max(delay, std::chrono::milliseconds::zero())),
          interrupt_on_input_(interrupt_on_input) {}

    Awaiter(const Awaiter&) = delete;
    Awaiter& operator=(const Awaiter&) = delete;
    Awaiter(Awaiter&&) = delete;
    Awaiter& operator=(Awaiter&&) = delete;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) {
      registration_ = context_.backend_.Register(
          delay_, interrupt_on_input_, [this, h](WaitOutcome outcome) {
            result_ = outcome;
            // Resumption may destroy the awaiter and its registration, so the
            // resumer must remain alive independently of this callback.
            auto notify = context_.resume_;
            notify(h);
          });
    }
    WaitOutcome await_resume() const noexcept { return result_; }

   private:
    WaitContext& context_;
    std::chrono::milliseconds delay_;
    bool interrupt_on_input_;
    WaitOutcome result_ = WaitOutcome::Timeout;
    std::unique_ptr<IWaitRegistration> registration_;
  };

  inline Awaiter WaitFor(std::chrono::milliseconds delay,
                         bool interrupt_on_input = false) {
    return Awaiter(*this, delay, interrupt_on_input);
  }

  inline Awaiter Schedule(bool interrupt_on_input = true) {
    constexpr auto kPollInterval = std::chrono::milliseconds(5);
    return WaitFor(kPollInterval, interrupt_on_input);
  }

 private:
  IWaitBackend& backend_;
  Resume resume_;
};

}  // namespace lop
