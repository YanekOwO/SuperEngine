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

#pragma once

#include "core/frame_counter.hpp"
#include "core/kidoku_table.hpp"
#include "machine/call_stack.hpp"
#include "machine/instruction.hpp"
#include "machine/iscriptor.hpp"
#include "machine/module_manager.hpp"
#include "utilities/stopwatch.hpp"

#include <boost/serialization/split_member.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace libreallive {
class IntMemRef;
};  // namespace libreallive
class LongOperation;
class Memory;
class RLModule;
class RealLiveDLL;
class System;
class Stage;
class Haikei;
struct StackFrame;
class EventListener;
class Debugger;
struct LongopListenerAdapter;
class rlSceneRenderer;

// Generic values
//
// "RealLive provides two generic settings to permit games using the standard
// system command menu to include custom options in it. The meaning of each
// generic flag is left up to the programmer. Valid values are 0 to 4."
struct Generic {
  int val1 = 0;
  int val2 = 0;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, const unsigned int ver) { ar & val1 & val2; }
};

// The RealLive virtual machine implementation. This class is the main user
// facing class which contains all state regarding integer/string memory, flow
// control, and other execution issues.
class RLMachine {
 public:
  RLMachine(std::shared_ptr<System> system,
            std::shared_ptr<IScriptor> scriptor,
            std::unique_ptr<Memory> memory = nullptr);
  virtual ~RLMachine();

  void Update();

  // Returns whether the machine is halted. When the machine is
  // halted, no more instruction may be executed, either because it
  // ran off the end of a scenario, or because the end() or halt()
  // instruction was called explicitly in the code.
  bool IsHalted() const { return halted_; }

  void set_store_register(int new_value) { store_register_ = new_value; }
  int store_register() const { return store_register_; }

  // Returns the integer that is the store register. (This is exposed for the
  // memory iterators to work properly.)
  int* store_register_address() { return &store_register_; }

  // Returns the value of the most recent line MetadataElement, which
  // should correspond with the line in the source file.
  int LineNumber() const { return line_; }

  // Returns the current scene number for the Scenario on the top of
  // the call stack.
  int SceneNumber() const;

  // Returns the current script location, including scenario number, line number
  // and bytecode offset
  ScriptLocation Location() const;

  void set_replaying_graphics_stack(bool in) { replaying_graphics_stack_ = in; }
  bool replaying_graphics_stack() { return replaying_graphics_stack_; }

  // Returns the current System that this RLMachine outputs to.
  System& GetSystem() { return system_; }

  Stage& stage();
  const Stage& stage() const;
  Haikei& haikei();
  const Haikei& haikei() const;

  std::shared_ptr<IScriptor> GetScriptor();

  Gameexe& GetGameexe();

  // Returns the internal memory object for raw access to the machine object's
  // memory.
  Memory& GetMemory() { return *memory_; }

  KidokuTable& GetKidokus() { return kidoku_table_; }

  CallStack& GetCallStack();

  ScenarioConfig GetScenarioConfig() const;

  // Where the current scenario was compiled with RLdev, returns the text
  // encoding used:
  //   0 -> CP932
  //   1 -> CP936 within CP932 codespace
  //   2 -> CP1252 within CP932 codespace
  //   3 -> CP949 within CP932 codespace
  // Where a scenario was not compiled with RLdev, always returns 0.
  int GetTextEncoding() const;

  // ---------------------------------------------------- [ DLL Management ]
  // RealLive has an extension system where a DLL can be loaded, and can be
  // called from bytecode through a ridiculously underpowered interface. We
  // couldn't support this even if we linked in winelib because the only way to
  // get things done is for the DLL to poke around in the memory of the
  // RealLive process. Haeleth (and insani?) have abused this for their own
  // purposes.
  //
  // So instead, we present the interface, and have our own versions of popular
  // DLLs compiled in. For now, that's just rlBabel.

  // Returns true if a DLL with |name| loaded.
  bool DllLoaded(const std::string& name);

  // Loads a "DLL" into the specified slot.
  void LoadDLL(int slot, const std::string& name);

  // Unloads the "DLL"
  void UnloadDLL(int slot);

  // Calls a DLL through the RealLive provided interface.
  int CallDLL(int slot, int one, int two, int three, int four, int five);

  // Adds a programmatic action triggered by a line marker in a specific SEEN
  // file. This is used both by lua_rlvm to trigger actions specified in lua to
  // drive rlvm's playing certain games, but is also used for game specific
  // hacks.
  void AddLineAction(const int seen, const int line, std::function<void(void)>);

  // ---------------------------------------------------------------------
  // State control

  // Force the machine to halt. This should terminate the execution of
  // bytecode, and theoretically, the program.
  void Halt();

  // Clears all call stacks and other data. Does not clear any local memory, as
  // this should only be called right before a load.
  void Reset();

  // Resets pieces of local memory. Correspondingly does NOT clear the call
  // stack, though it does clear the shadow save stack.
  void LocalReset();

  // Pushes a long operation onto the function stack. Control will be passed to
  // this LongOperation instead of normal bytecode passing until the
  // LongOperation gives control up.
  virtual void PushLongOperation(std::shared_ptr<LongOperation> long_operation);

  // Returns a pointer to the currently running LongOperation when the top of
  // the call stack is a LongOperation. NULL otherwise.
  std::shared_ptr<LongOperation> CurrentLongOperation() const;

  // Increments the stack pointer in the current frame. If we have run
  // off the end of the current scenario, set the halted bit.
  void AdvanceIP();
  void RevertIP();

  // RealLive will save the latest savepoint for the topmost stack
  // frame. Savepoints can be manually set (with the "Savepoint" command), but
  // are usually implicit.
  // Mark a savepoint on the top of the stack. Used by both the
  // explicit Savepoint command, and most actions that would trigger
  // an implicit savepoint.
  void MarkSavepoint();

  // Whether the DisableAutoSavepoints override is on. This is
  // triggered purely from bytecode.
  void SetMarkSavepoints(const int in);

  // -----------------------------------------------------------------------
  // machine execution wrapper functions
  std::shared_ptr<Instruction> ReadInstruction() const;
  bool ExecuteLongop(std::shared_ptr<LongOperation> long_op);
  void ExecuteInstruction(std::shared_ptr<Instruction> instruction);

  // -----------------------------------------------------------------------
  // Actual implementation for each type of byte code
  void PerformTextout(std::string text);
  void operator()(std::monostate);
  void operator()(Kidoku);
  void operator()(Line);
  void operator()(rlCommand);
  void operator()(rlExpression);
  void operator()(Textout);
  void operator()(End);

  // -----------------------------------------------------------------------
  // Replays the new Graphics stack, string representations of reallive
  // bytecode.
  void ReplayGraphicsStackCommand();

  Generic& GetGenerics();

  Stopwatch& GetTimer(int layer, int idx);

  FrameCounter* GetFrameCounter(int layer, int idx);
  void SetFrameCounter(int layer, int idx, FrameCounter counter);
  void ClearFrameCounter(int layer, int idx);

 private:
  // States whether the RLMachine is in the halted state (and thus won't
  // execute more instructions)
  bool halted_ = false;

  // The Reallive VM's integer and string memory
  std::unique_ptr<Memory> memory_;
  Memory savepoint_memory_;

  KidokuTable kidoku_table_;

  // The RealLive machine's single result register
  int store_register_ = 0;

  ModuleManager module_manager_;

  std::shared_ptr<IScriptor> scriptor_;

  // The most recent line marker we've come across
  int line_ = 0;

  // The RLMachine carried around a reference to the local system, to keep it
  // from being a Singleton so we can do proper unit testing.
  System& system_;

  std::unique_ptr<Stage> stage_;
  std::unique_ptr<Haikei> haikei_;

  std::shared_ptr<rlSceneRenderer> renderer_;

  Generic generic_;
  std::map<std::pair<int, int>, Stopwatch> rltimer_;
  std::map<std::pair<int, int>, FrameCounter> frame_counter_;

  // Override defaults
  bool mark_savepoints_ = true;

  // Whether we are currently replaying the graphics stack. While replaying the
  // graphics stack, we shouldn't advance the instruction pointer and do other
  // stuff.
  bool replaying_graphics_stack_ = false;

  // Call Stack, manages local variables, call metadata(long operation), return
  // address.
  CallStack call_stack_, savepoint_call_stack_;

  // An optional set of game specific hacks that run at certain SEEN/line
  // pairs. These run during setLineNumer().
  typedef std::map<std::pair<int, int>, std::function<void(void)>> ActionMap;
  std::unique_ptr<ActionMap> on_line_actions_;

  typedef std::unordered_map<int, std::unique_ptr<RealLiveDLL>> DLLMap;
  // Currently loaded "DLLs".
  DLLMap loaded_dlls_;

  std::shared_ptr<EventListener> longop_listener_adapter_;
  std::shared_ptr<EventListener> system_listener_;

 private:
  // boost::serialization support
  friend class boost::serialization::access;
  BOOST_SERIALIZATION_SPLIT_MEMBER();

  void save(auto& ar, const unsigned int file_version) const {
    ar& LineNumber() & savepoint_call_stack_ & generic_;
  }

  void load(auto& ar, const unsigned int file_version) {
    ar & line_ & call_stack_ & generic_;
  }
};
