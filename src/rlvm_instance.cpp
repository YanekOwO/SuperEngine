// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2011 Elliot Glaysher
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

#include "rlvm_instance.hpp"

#include "core/gameexe.hpp"
#include "core/memory.hpp"
#include "libreallive/game_loader.hpp"
#include "log/domain_logger.hpp"
#include "long_operations/reallive_task.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rloperation.hpp"
#include "machine/serialization.hpp"
#include "platforms/implementor.hpp"
#include "systems/event_system.hpp"
#include "systems/system.hpp"
#include "systems/system_error.hpp"
#include "utf8.h"
#include "utilities/clock.hpp"
#include "utilities/exception.hpp"
#include "utilities/gettext.hpp"
#include "utilities/string_utilities.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

RLVMInstance::RLVMInstance() : platform_implementor_(nullptr) {
  srand(time(NULL));
}

RLVMInstance::~RLVMInstance() = default;

void RLVMInstance::SetPlatformImplementor(
    std::shared_ptr<IPlatformImplementor> impl) {
  platform_implementor_ = impl;
}

int RLVMInstance::Main(const std::filesystem::path& gameroot) {
  std::unique_ptr<libreallive::GameLoader> loader = nullptr;
  try {
    loader = std::make_unique<libreallive::GameLoader>(gameroot, start_scene_,
                                                       fast_forward_);
    machine_ = loader->machine_;
  } catch (std::exception& e) {
    static DomainLogger logger("Main");
    logger(Severity::Error) << "Failed to load game:\n" << e.what();
    ReportFatalError(_("Fatal game loading error"), e.what());
    return 1;
  }

  try {
    const auto clock = machine_->GetSystem().event().GetClock();
    constexpr auto frame_time =
        std::chrono::duration_cast<Clock::timepoint_t::duration>(
            std::chrono::seconds(1) / 144.0);
    auto next_update = clock->GetTime();

    while (!machine_->IsHalted()) {
      auto& system = machine_->GetSystem();
      if (clock->GetTime() >= next_update || system.ShouldFastForward()) {
        // Service input, rendering and audio even when an operation has a
        // distant deadline. Earlier operation wakeups need not render again.
        system.set_force_wait(false);
        machine_->Update();
        next_update = clock->GetTime() + frame_time;
      }
      if (machine_->IsHalted() || machine_->GetSystem().IsQuitRequested())
        break;

      for (;;) {
        // Starting a task or delivering one due wait is the entire operation
        // step. In particular, WaitFor(0) yields to a later iteration.
        const bool long_operation = machine_->CurrentLongOperation() != nullptr;
        Step();

        if (long_operation || machine_->IsHalted() ||
            system.IsQuitRequested() || system.force_wait() ||
            clock->GetTime() >= next_update)
          break;
      }

      if (machine_->IsHalted() || system.IsQuitRequested())
        break;
      if (!system.ShouldFastForward()) {
        auto wake_at = next_update;
        if (!system.force_wait()) {
          if (auto task = machine_->CurrentLongOperation())
            wake_at = std::min(wake_at, task->ReadyUntil());
        }
        std::this_thread::sleep_until(wake_at);
      }
    }

    Serialization::saveGlobalMemory(*machine_);
    return 0;
  } catch (rlvm::UserPresentableError& e) {
    ReportFatalError(e.message_text(), e.informative_text());
  } catch (rlvm::Exception& e) {
    ReportFatalError(_("Fatal RLVM error"), e.what());
  } catch (libreallive::Error& e) {
    ReportFatalError(_("Fatal libreallive error"), e.what());
  } catch (SystemError& e) {
    ReportFatalError(_("Fatal local system error"), e.what());
  } catch (std::exception& e) {
    ReportFatalError(_("Uncaught exception"), e.what());
  } catch (const char* e) {
    ReportFatalError(_("Uncaught exception"), e);
  }

  return 1;
}

void RLVMInstance::Step() {
  static DomainLogger logger("RLVMInstance::Step");

  try {
    if (machine_->CurrentLongOperation()) {
      machine_->ExecuteLongop();
    } else {
      std::shared_ptr<Instruction> instruction = machine_->ReadInstruction();
      machine_->AdvanceIP();
      machine_->ExecuteInstruction(instruction);
    }

  } catch (rlvm::UnimplementedOpcode& e) {
    static DomainLogger logger("Unimplemented");
    logger(Severity::Info) << DescribeCurrentIP() << ' ' << e.FormatCommand()
                           << e.FormatParameters();
  } catch (rlvm::Exception& e) {
    auto rec = logger(Severity::Error);
    rec << DescribeCurrentIP() << ' ';

    // We specialcase rlvm::Exception because we might have the name of the
    // opcode.
    if (e.operation()) {
      rec << "[" << e.operation()->Name() << "]";
    }

    rec << ":  " << e.what();
  } catch (std::exception& e) {
    auto rec = logger(Severity::Error);
    rec << DescribeCurrentIP() << ' ';
    rec << e.what();
  }
}

std::string RLVMInstance::DescribeCurrentIP() const {
  const auto scene_num = machine_->SceneNumber();
  const auto line_num = machine_->LineNumber();
  return std::format("({:0>4d}:{:d})", scene_num, line_num);
}

// -----------------------------------------------------------------------

void RLVMInstance::ReportFatalError(const std::string& message_text,
                                    const std::string& informative_text) {
  if (platform_implementor_)
    platform_implementor_->ReportFatalError(message_text, informative_text);
}

bool RLVMInstance::AskUserPrompt(const std::string& message_text,
                                 const std::string& informative_text,
                                 const std::string& true_button,
                                 const std::string& false_button) {
  if (!platform_implementor_)
    return true;
  return platform_implementor_->AskUserPrompt(message_text, informative_text,
                                              true_button, false_button);
}
