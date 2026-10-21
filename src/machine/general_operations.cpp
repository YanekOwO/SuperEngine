// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007, 2008 Elliot Glaysher
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

#include "machine/general_operations.hpp"

#include <string>
#include <vector>

#include "core/gameexe.hpp"
#include "core/haikei.hpp"
#include "core/stage.hpp"
#include "libreallive/parser.hpp"
#include "machine/rlmachine.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "utilities/exception.hpp"

namespace getSystemObjImpl {

template <>
System& GetSystemObj(RLMachine& machine) {
  return machine.GetSystem();
}

template <>
EventSystem& GetSystemObj(RLMachine& machine) {
  return machine.GetSystem().event();
}


template <>
Haikei& GetSystemObj(RLMachine& machine) {
  return machine.haikei();
}

template <>
Stage& GetSystemObj(RLMachine& machine) {
  return machine.stage();
}

template <>
TextSystem& GetSystemObj(RLMachine& machine) {
  return machine.GetSystem().text();
}

template <>
SoundSystem& GetSystemObj(RLMachine& machine) {
  return machine.GetSystem().sound();
}

template <>
CGMTable& GetSystemObj(RLMachine& machine) {
  return machine.cg_table();
}

template <>
TextPage& GetSystemObj(RLMachine& machine) {
  return machine.GetSystem().text().GetCurrentPage();
}

}  // namespace getSystemObjImpl

// -----------------------------------------------------------------------
// MultiDispatch
// -----------------------------------------------------------------------
MultiDispatch::MultiDispatch(RLOperation* op) : handler_(op) {}

MultiDispatch::~MultiDispatch() {}

/// TODO(erg): Port this up to the new expression handling code
void MultiDispatch::operator()(RLMachine& machine,
                               const libreallive::CommandElement& ff) {
  const libreallive::ExpressionPiecesVector& parameter_pieces =
      ff.GetParsedParameters();

  for (unsigned int i = 0; i < parameter_pieces.size(); ++i) {
    const libreallive::ExpressionPiecesVector& element =
        parameter_pieces[i]->GetContainedPieces();
    handler_->Dispatch(machine, element);
  }
}

// -----------------------------------------------------------------------
// ReturnGameexeInt
// -----------------------------------------------------------------------
ReturnGameexeInt::ReturnGameexeInt(const std::string& full_key, int en)
    : full_key_name_(full_key), entry_(en) {}

int ReturnGameexeInt::operator()(RLMachine& machine) {
  Gameexe& gexe = machine.GetSystem().gameexe();
  if (auto it = gexe(full_key_name_).IntAt(entry_)) {
    return *it;
  } else {
    std::ostringstream oss;
    oss << "Could not access piece " << entry_ << " in Gameexe key \""
        << full_key_name_ << "\"";
    throw std::runtime_error(oss.str());
  }
}

// -----------------------------------------------------------------------
// InvokeSyscomAsOp
// -----------------------------------------------------------------------
InvokeSyscomAsOp::InvokeSyscomAsOp(const int syscom) : syscom_(syscom) {}

void InvokeSyscomAsOp::operator()(RLMachine& machine) {
  machine.GetSystem().InvokeSyscom(machine, syscom_);
}

// -----------------------------------------------------------------------
// UndefinedFunction
// -----------------------------------------------------------------------
UndefinedFunction::UndefinedFunction(int modtype,
                                     int module,
                                     int opcode,
                                     int overload)
    : modtype_(modtype),
      module_(module),
      opcode_(opcode),
      overload_(overload) {}

void UndefinedFunction::Dispatch(
    RLMachine& machine,
    const libreallive::ExpressionPiecesVector& parameters) {
  throw rlvm::UnimplementedOpcode(Name(), modtype_, module_, opcode_,
                                  overload_);
}

void UndefinedFunction::DispatchFunction(RLMachine& machine,
                                         const libreallive::CommandElement& f) {
  throw rlvm::UnimplementedOpcode(Name(), f);
}

void UndefinedFunction::operator()(RLMachine& machine,
                                   const libreallive::CommandElement& f) {
  throw rlvm::UnimplementedOpcode(Name(), f);
}

// Template instantiations.
template RLOperation* CallFunction<EventSystem>(void (EventSystem::*)(int));
template RLOperation* CallFunction<RLMachine>(void (RLMachine::*)(int));
template RLOperation* CallFunction<Haikei>(void (Haikei::*)(int));
template RLOperation* CallFunction<Haikei>(void (Haikei::*)());
template RLOperation* CallFunction<Stage>(void (Stage::*)(int));
template RLOperation* CallFunction<Stage>(void (Stage::*)());
template RLOperation* CallFunction<SoundSystem>(void (SoundSystem::*)(int));
template RLOperation* CallFunction<System>(void (System::*)(int));
template RLOperation* CallFunction<TextSystem>(void (TextSystem::*)(int));
template RLOperation* ReturnIntValue<EventSystem, int>(int (EventSystem::*)()
                                                           const);
template RLOperation* ReturnIntValue<RLMachine, int>(int (RLMachine::*)()
                                                         const);
template RLOperation* ReturnIntValue<SoundSystem, int>(int (SoundSystem::*)()
                                                           const);
template RLOperation* ReturnIntValue<TextSystem, int>(int (TextSystem::*)()
                                                          const);
