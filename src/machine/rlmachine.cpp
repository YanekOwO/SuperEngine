// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include "machine/rlmachine.hpp"

#include "core/event_listener.hpp"
#include "core/haikei.hpp"
#include "core/memory.hpp"
#include "core/memory_internal/serialization_local.hpp"
#include "core/stage.hpp"
#include "libreallive/parser.hpp"
#include "log/domain_logger.hpp"
#include "long_operations/pause_long_operation.hpp"
#include "long_operations/textout_long_operation.hpp"
#include "machine/long_operation.hpp"
#include "machine/reallive_dll.hpp"
#include "machine/rlmodule.hpp"
#include "machine/rloperation.hpp"
#include "machine/scene_renderer.hpp"
#include "machine/serialization.hpp"
#include "machine/stack_frame.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/system_error.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "utilities/clock.hpp"
#include "utilities/exception.hpp"
#include "utilities/string_utilities.hpp"

#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// -----------------------------------------------------------------------
// RLMachine
// -----------------------------------------------------------------------
static DomainLogger logger("RLMachine");
RLMachine::RLMachine(std::shared_ptr<System> system,
                     std::shared_ptr<IScriptor> scriptor,
                     std::unique_ptr<Memory> memory)
    : memory_(std::move(memory)),
      module_manager_(ModuleManager::CreatePrototype()),
      scriptor_(scriptor),
      system_(*system) {
  if (!memory_)
    memory_ = std::make_unique<Memory>();
  memory_->AttachCallStack(&call_stack_);

  if (system) {
    stage_ = std::make_unique<Stage>(system->graphics().GetObjectLayerSize());
    auto backend = system->graphics().GetBackend();
    haikei_ = std::make_unique<Haikei>(
        system->graphics().screen_size(),
        [backend](Size size) { return backend->CreateSurface(size); },
        [this](const std::string& name) {
          return GetSystem().graphics().GetSurfaceNamed(name);
        },
        system->event().GetClock());

    renderer_ = std::make_shared<rlSceneRenderer>(*this);
    system->graphics().BindSceneRenderer(renderer_);

    generic_.val1 =
        system->gameexe()("INIT_ORIGINALSETING1_MOD").Int().value_or(0);
    generic_.val2 =
        system->gameexe()("INIT_ORIGINALSETING2_MOD").Int().value_or(0);

    // Initial value of the savepoint
    MarkSavepoint();
  }
}

RLMachine::~RLMachine() = default;

void RLMachine::Update() {
  system_.Run();
}

Stage& RLMachine::stage() { return *stage_; }

const Stage& RLMachine::stage() const { return *stage_; }

Haikei& RLMachine::haikei() { return *haikei_; }

const Haikei& RLMachine::haikei() const { return *haikei_; }

void RLMachine::MarkSavepoint() {
  savepoint_call_stack_ = call_stack_.Clone();
  savepoint_memory_ = Memory();
  stage().TakeSavepointSnapshot();
  GetSystem().text().TakeSavepointSnapshot();
}

ScenarioConfig RLMachine::GetScenarioConfig() const {
  int current_scenario = call_stack_.Top()->pos.scenario_number;
  return scriptor_->GetScenarioConfig(current_scenario);
}

void RLMachine::SetMarkSavepoints(const int in) { mark_savepoints_ = in; }

std::shared_ptr<Instruction> RLMachine::ReadInstruction() const {
  if (IsHalted())
    return std::make_shared<Instruction>(std::monostate());

  const auto top_frame = call_stack_.Top();
  if (top_frame == nullptr) {
    logger(Severity::Error) << "Stack underflow";
    return std::make_shared<Instruction>(std::monostate());
  }

  if (top_frame->frame_type == StackFrame::TYPE_LONGOP)
    return std::make_shared<Instruction>(std::monostate());

  return scriptor_->ResolveInstruction(top_frame->pos);
}

bool RLMachine::ExecuteLongop(std::shared_ptr<LongOperation> long_op) {
  auto lock = call_stack_.GetLock();
  return (*long_op)(*this);
}

void RLMachine::ExecuteInstruction(std::shared_ptr<Instruction> instruction) {
  try {
    // write trace log
    static DomainLogger tracer("TRACER");
    tracer(Severity::None) << std::format("({:0>4d}:{:d}) ", SceneNumber(),
                                          line_)
                           << std::visit(InstructionToString(&module_manager_),
                                         *instruction);

    // execute the instruction
    std::visit(*this, *instruction);
  } catch (rlvm::UnimplementedOpcode& e) {
    static DomainLogger logger("Unimplemented");
    logger(Severity::None) << std::format("({:0>4d}:{:d}) ", SceneNumber(),
                                          line_)
                           << e.FormatCommand() << e.FormatParameters();
  } catch (rlvm::Exception& e) {
    static DomainLogger logger;
    auto rec = logger(Severity::Error);
    rec << std::format("({:0>4d}:{:d}) ", SceneNumber(), line_);

    // We specialcase rlvm::Exception because we might have the name of the
    // opcode.
    if (e.operation()) {
      rec << "[" << e.operation()->Name() << "]";
    }

    rec << ":  " << e.what();
  } catch (std::exception& e) {
    auto rec = logger(Severity::Error);
    rec << std::format("({:0>4d}:{:d}) ", SceneNumber(), line_);
    rec << e.what();
  }
}

void RLMachine::AdvanceIP() {
  if (replaying_graphics_stack())
    return;

  const auto it = call_stack_.FindTopRealFrame();
  if (it != nullptr) {
    it->pos = scriptor_->Next(it->pos);

    if (!scriptor_->HasNext(it->pos))
      halted_ = true;
  }
}

void RLMachine::RevertIP() {
  auto& pos = call_stack_.Top()->pos;
  --pos.location_offset;
}

CallStack& RLMachine::GetCallStack() { return call_stack_; }

std::shared_ptr<IScriptor> RLMachine::GetScriptor() { return scriptor_; }

ScriptLocation RLMachine::Location() const {
  auto location = call_stack_.FindTopRealFrame()->pos;
  location.line_num = LineNumber();
  return location;
}

Gameexe& RLMachine::GetGameexe() { return system_.gameexe(); }

Generic& RLMachine::GetGenerics() { return generic_; }

Stopwatch& RLMachine::GetTimer(int layer, int idx) {
  static DomainLogger logger("RLTimer");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting timer. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  const auto key = std::make_pair(layer, idx);
  static std::shared_ptr<Clock> clock = std::make_shared<Clock>();
  if (!rltimer_.contains(key)) {
    Stopwatch timer(clock);
    timer.Apply(Stopwatch::Action::Run);
    rltimer_.emplace(key, std::move(timer));
  }
  return rltimer_.find(key)->second;
}

FrameCounter* RLMachine::GetFrameCounter(int layer, int idx) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  auto it = frame_counter_.find(std::make_pair(layer, idx));
  return it == frame_counter_.end() ? nullptr : &it->second;
}

void RLMachine::SetFrameCounter(int layer, int idx, FrameCounter counter) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  frame_counter_.insert_or_assign(std::make_pair(layer, idx),
                                  std::move(counter));
}

void RLMachine::ClearFrameCounter(int layer, int idx) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when clearing frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  frame_counter_.erase(std::make_pair(layer, idx));
}

void RLMachine::PushLongOperation(
    std::shared_ptr<LongOperation> long_operation) {
  const auto top_frame = call_stack_.Top();
  auto pos = top_frame->pos;
  pos.location_offset--;  // location associated with this longop is previous
                          // location of the instruction pointer.
  call_stack_.Push(StackFrame(pos, long_operation));
}

void RLMachine::Reset() {
  call_stack_ = CallStack();
  savepoint_call_stack_ = CallStack();
  stage().Reset();
  haikei().Reset();
  GetSystem().Reset();
}

void RLMachine::LocalReset() {
  savepoint_call_stack_ = CallStack();
  memory_->PartialReset(LocalMemory());
  stage().Reset();
  haikei().Reset();
  GetSystem().Reset();
}

std::shared_ptr<LongOperation> RLMachine::CurrentLongOperation() const {
  auto top = call_stack_.Top();
  if (top && top->frame_type == StackFrame::TYPE_LONGOP)
    return top->long_op;

  return nullptr;
}

int RLMachine::SceneNumber() const {
  return call_stack_.Top()->pos.scenario_number;
}

int RLMachine::GetTextEncoding() const {
  auto scenario_number = call_stack_.Top()->pos.scenario_number;
  return scriptor_->GetScenarioConfig(scenario_number).text_encoding;
}

bool RLMachine::DllLoaded(const std::string& name) {
  for (auto const& dll : loaded_dlls_) {
    if (dll.second->GetDLLName() == name)
      return true;
  }

  return false;
}

void RLMachine::LoadDLL(int slot, const std::string& name) {
  RealLiveDLL* dll = RealLiveDLL::BuildDLLNamed(*this, name);
  if (dll) {
    loaded_dlls_.emplace(slot, std::unique_ptr<RealLiveDLL>(dll));
  } else {
    std::ostringstream oss;
    oss << "Can't load emulated DLL named '" << name << "'";
    throw std::runtime_error(oss.str());
  }
}

void RLMachine::UnloadDLL(int slot) { loaded_dlls_.erase(slot); }

int RLMachine::CallDLL(int slot,
                       int one,
                       int two,
                       int three,
                       int four,
                       int five) {
  DLLMap::iterator it = loaded_dlls_.find(slot);
  if (it != loaded_dlls_.end()) {
    return it->second->CallDLL(*this, one, two, three, four, five);
  } else {
    std::ostringstream oss;
    oss << "Attempt to callDLL(" << one << ", " << two << ", " << three << ", "
        << four << ", " << five << ") on slot " << slot
        << " when no DLL is loaded there!";
    throw std::runtime_error(oss.str());
  }
}

void RLMachine::Halt() { halted_ = true; }

void RLMachine::AddLineAction(const int seen,
                              const int line,
                              std::function<void(void)> function) {
  if (!on_line_actions_)
    on_line_actions_.reset(new ActionMap);

  (*on_line_actions_)[std::make_pair(seen, line)] = function;
}

// -----------------------------------------------------------------------

void RLMachine::PerformTextout(std::string cp932str) {
  std::string name_parsed_text;
  try {
    name_parsed_text = parseNames(GetMemory(), cp932str);
  } catch (rlvm::Exception& e) {
    // WEIRD: Sometimes rldev (and the official compiler?) will generate
    // strings that aren't valid shift_jis. Fall back while I figure out how
    // to handle this.
    name_parsed_text = cp932str;
  }

  std::string utf8str = cp932toUTF8(name_parsed_text, GetTextEncoding());
  TextSystem& ts = GetSystem().text();

  // Display UTF-8 characters
  auto ptr = std::make_shared<TextoutLongOperation>(*this, utf8str);

  if (GetSystem().ShouldFastForward() || ts.message_no_wait() ||
      ts.script_message_nowait()) {
    ptr->set_no_wait();
  }

  PushLongOperation(ptr);
}

// -----------------------------------------------------------------------

void RLMachine::operator()(std::monostate) {}

void RLMachine::operator()(Kidoku k) {
  // Check to see if we mark savepoints on textout
  if (GetScenarioConfig().enable_message_savepoint &&
      system_.text().GetCurrentPage().number_of_chars_on_page() == 0) {
    MarkSavepoint();
  }

  // Mark if we've previously read this piece of text.
  system_.text().SetKidokuRead(kidoku_table_.HasBeenRead(SceneNumber(), k.num));

  // Record the kidoku pair in global memory.
  kidoku_table_.RecordKidoku(SceneNumber(), k.num);
}

void RLMachine::operator()(Line l) {
  line_ = l.num;

  if (on_line_actions_) {
    ActionMap::iterator it =
        on_line_actions_->find(std::make_pair(SceneNumber(), line_));
    if (it != on_line_actions_->end()) {
      it->second();
    }
  }
}

void RLMachine::operator()(rlCommand cmd) {
  auto f = cmd.cmd;

  auto op = module_manager_.Dispatch(*f);
  if (op == nullptr) {  // unimplemented opcode
    throw rlvm::UnimplementedOpcode("", *f);
  }

  try {
    op->DispatchFunction(*this, *f);
  } catch (rlvm::Exception& e) {
    e.setOperation(op);
    throw e;
  }
}

void RLMachine::operator()(rlExpression e) { e.Execute(*this); }

void RLMachine::operator()(Textout t) { PerformTextout(std::move(t.text)); }

void RLMachine::operator()(End) { Halt(); }

// ------------------------------------------------------------------------------
void RLMachine::ReplayGraphicsStackCommand() {
  Stage& stage = this->stage();
  std::deque<std::string> stack;
  stack.swap(stage.graphics_stack);

  set_replaying_graphics_stack(true);
  {
    try {
      for (auto const& command : stack) {
        if (command != "") {
          // Parse the string as a chunk of Reallive bytecode.
          libreallive::Parser parser;
          auto element = parser.ParseBytecode(command.c_str(),
                                              command.c_str() + command.size());
          if (auto command =
                  std::dynamic_pointer_cast<libreallive::CommandElement>(
                      element)) {
            operator()(rlCommand(command.get()));
          }
        }
      }
    } catch (std::exception& e) {
      std::cerr << "Error while replaying graphics stack: " << e.what()
                << std::endl;
    }
  }
  set_replaying_graphics_stack(false);
}
