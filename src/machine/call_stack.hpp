// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

/**
 * @file call_stack.hpp
 * @brief Declaration of the CallStack class for managing a stack of StackFrame
 * objects.
 */

#include "core/memory.hpp"
#include "machine/stack_frame.hpp"

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/vector.hpp>

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

/**
 * @class CallStack
 * @brief Manages a call stack of `StackFrame` objects, with support for delayed
 * modifications when locking.
 */
class CallStack {
 public:
  explicit CallStack(Memory& memory);

  /**
   * @brief Copy constructor is deleted. Use `Clone()` instead.
   */
  CallStack(const CallStack&) = delete;

  /**
   * @brief Copy assignment operator is deleted. Use `Clone()` instead.
   */
  CallStack& operator=(const CallStack&) = delete;

  CallStack(CallStack&&) = default;
  CallStack& operator=(CallStack&&) = default;

  virtual ~CallStack() = default;

  /**
   * @brief Creates a clone of the current `CallStack`.
   * @return A new `CallStack` object that is a copy of this one.
   * @throws std::runtime_error If the `CallStack` is locked.
   */
  CallStack Clone() const;

  MemoryHandle& GetMemory();
  const MemoryHandle& GetMemory() const;

  void Clear();

  /**
   * @brief Pushes a `StackFrame` onto the call stack.
   * @param frame The `StackFrame` to push.
   *
   * If the `CallStack` is locked, the push operation is delayed until the lock
   * is released. Otherwise, the frame is added to the top of the stack.
   */
  void Push(StackFrame frame);

  /**
   * @brief Pops the top `StackFrame` from the call stack.
   *
   * If the `CallStack` is locked, the pop operation is delayed until the lock
   * is released. Otherwise, the top frame is removed from the stack.
   */
  void Pop();

  /**
   * @brief Returns the number of frames in the call stack.
   */
  size_t Size() const;

  /**
   * @brief Returns a reverse iterator to the topmost frame of the stack.
   * @return A `reverse_iterator` pointing to the topmost frame.
   */
  std::vector<StackFrame>::reverse_iterator begin();

  /**
   * @brief Returns a reverse iterator past the bottom frame of the stack.
   * @return A `reverse_iterator` pointing past the bottom frame.
   *
   * This is used in conjunction with `begin()` to iterate over the entire
   * stack.
   */
  std::vector<StackFrame>::reverse_iterator end();

  /**
   * @brief Returns a pointer to the topmost `StackFrame`.
   * @return A pointer to the top `StackFrame`, or `nullptr` if the stack is
   * empty.
   *
   * Note: The method returns a non-const pointer even though it is a const
   * method. This is because `StackFrame` might need to be modified even when
   * the `CallStack` is const.
   */
  StackFrame* Top() const;

  /**
   * @brief Returns a pointer to the topmost real `StackFrame`
   * (non-`TYPE_LONGOP`).
   * @return A pointer to the topmost real `StackFrame`, or `nullptr` if none
   * exist.
   */
  virtual StackFrame* FindTopRealFrame() const;

 private:
  /**
   * @struct Lock
   * @brief RAII-style lock that prevents modifications to the `CallStack`.
   *
   * When a `Lock` object is created, it locks the `CallStack`. Upon
   * destruction, it unlocks the `CallStack` and applies any delayed
   * modifications.
   */
  struct Lock;
  friend struct Lock;

 public:
  /**
   * @brief Acquires a lock on the `CallStack`.
   * @return A `Lock` object that will release the lock upon destruction.
   * @throws std::logic_error If the `CallStack` is already locked.
   *
   * While locked, any push or pop operations are delayed and will be executed
   * when the lock is released.
   */
  Lock GetLock();

 private:
  std::vector<size_t>
      real_frames_;  ///< Indices of non-`TYPE_LONGOP` frames for quick access.
  std::vector<StackFrame>
      stack_;  ///< The actual stack of `StackFrame` objects.
  std::vector<std::function<void(void)>>
      delayed_modifications_;  ///< Queue of delayed modifications.
  bool is_locked_ =
      false;  ///< Indicates whether the `CallStack` is currently locked.
  std::unique_ptr<MemoryHandle> memory_;

  /**
   * @brief Applies all delayed modifications to the `CallStack`.
   *
   * This method is called when the `CallStack` is unlocked to execute any
   * push or pop operations that were delayed during the lock period.
   */
  void ApplyDelayedModifications();

  explicit CallStack(MemoryHandle memory);

  // Boost serialization support
  friend class boost::serialization::access;
  BOOST_SERIALIZATION_SPLIT_MEMBER();

  void save(auto& ar, const unsigned int version) const {
    ar & stack_;
    StackMemory stack_memory = memory_->GetStackMemory();
    ar & stack_memory;
  }

  void load(auto& ar, const unsigned int version) {
    real_frames_.clear();
    stack_.clear();
    delayed_modifications_.clear();
    is_locked_ = false;

    StackMemory stack_memory;
    ar & stack_ & stack_memory;

    for (std::size_t i = 0; i < stack_.size(); ++i)
      if (stack_[i].frame_type != StackFrame::TYPE_LONGOP)
        real_frames_.push_back(i);

    if (stack_memory.L.size() != real_frames_.size() ||
        stack_memory.K.size() != real_frames_.size())
      throw std::runtime_error(
          "CallStack: serialized memory depth does not match real frames.");
    memory_->ResetStackMemory(std::move(stack_memory));
  }
};

struct CallStack::Lock {
  ~Lock();
  explicit Lock(CallStack& call_stack);

 private:
  CallStack& call_stack_;  ///< Reference to the associated `CallStack`.
};
