// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
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

#include "machine/stack_frame.hpp"

#include "libreallive/archive.hpp"
#include "long_operations/reallive_task.hpp"

#include <iostream>
#include <typeinfo>

StackFrame::StackFrame() : pos(), frame_type() {}

StackFrame::StackFrame(ScriptLocation it, FrameType t)
    : pos(it), frame_type(t) {}

StackFrame::StackFrame(ScriptLocation it, std::shared_ptr<lop::RealliveTask> op)
    : pos(it), long_op(op), frame_type(TYPE_LONGOP) {}

StackFrame::~StackFrame() = default;
