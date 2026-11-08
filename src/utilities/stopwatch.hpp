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

#include "core/serialization_context.hpp"
#include "utilities/serialization.hpp"

#include <chrono>
#include <memory>

class Clock;

/**
 * @class Stopwatch
 * @brief A class representing a stopwatch that can be started, paused, and
 * reset.
 */
class Stopwatch {
 public:
  using timepoint_t = std::chrono::time_point<std::chrono::steady_clock>;
  using duration_t = std::chrono::milliseconds;

  /**
   * @brief Constructs a Stopwatch with the provided clock.
   * @param clock A shared pointer to a Clock instance.
   * @throws std::invalid_argument if clock is nullptr.
   */
  explicit Stopwatch(std::shared_ptr<Clock> clock);

  /**
   * @enum Action
   * @brief Actions that can be applied to the stopwatch.
   */
  enum class Action { Pause, Run, Reset };

  /**
   * @brief Applies an action to the stopwatch (Pause, Run, Reset).
   * @param action The action to apply.
   * @throws std::invalid_argument if an invalid action is provided.
   */
  virtual void Apply(Action action);

  /**
   * @enum State
   * @brief Possible states of the stopwatch.
   */
  enum class State { Paused, Running, Stopped };

  /**
   * @brief Retrieves the current state of the stopwatch.
   * @return The current state.
   */
  State GetState() const;

  /**
   * @brief Gets the current elapsed time of the stopwatch.
   * @return The elapsed time as duration_t.
   */
  duration_t GetReading();

  /**
   * @brief Gets the current elapsed time since last calling of this method, or
   * since stopwatch started running if never called.
   * @return The elapsed time as duration_t.
   */
  duration_t LapTime();

 private:
  friend class boost::serialization::access;
  void save(auto& ar, unsigned int) const {
    auto snapshot = *this;
    snapshot.Update();
    ar & snapshot.state_ & snapshot.time_ & snapshot.lap_time_;
  }
  void load(auto& ar, unsigned int) {
    ar & state_ & time_ & lap_time_;
    clock_ = SerializationContext::Get(ar).GetClock();
    RebaseClock();
  }
  BOOST_SERIALIZATION_SPLIT_MEMBER()
  void RebaseClock();

  /**
   * @brief Updates the internal time based on the clock.
   * @throws std::runtime_error if the clock time moves backward.
   */
  void Update();

  std::shared_ptr<Clock> clock_;  ///< The clock used for time measurement.
  State state_;                   ///< The current state of the stopwatch.
  timepoint_t last_tick_;         ///< The last recorded time point.
  duration_t time_;               ///< The accumulated time.
  duration_t lap_time_;           ///< The accumulated lap time.
};
