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

#include "vm/gc.hpp"

#include "log/domain_logger.hpp"
#include "utilities/overload.hpp"
#include "vm/iobject.hpp"
#include "vm/value.hpp"

#include <cassert>
#include <memory>

namespace serilang {

void GCVisitor::MarkSub(IObject* obj) {
  if (!obj || obj->hdr_.marked)
    return;
  obj->hdr_.marked = true;

  obj->MarkRoots(*this);
}

void GCVisitor::MarkSub(Value& val) {
  if (IObject* obj = val.Get_if<IObject>())
    MarkSub(obj);
}

GarbageCollector::~GarbageCollector() {
  while (gc_list_) {
    IObject* head = gc_list_;
    gc_list_ = gc_list_->hdr_.next;
    delete head;
  }
}

size_t GarbageCollector::AllocatedBytes() const { return allocated_bytes_; }

Value GarbageCollector::TrackValue(TempValue&& t) {
  return std::visit(overload{[&](std::unique_ptr<IObject> t) {
                               IObject* ptr = t.release();
                               TrackObject(ptr);
                               return Value(ptr);
                             },
                             [](auto&& t) { return t; }},
                    std::move(t));
}
void GarbageCollector::TrackObject(IObject* obj) {
  allocated_bytes_ += obj->Size();
  GCHeader& hdr = obj->hdr_;
  hdr.next = gc_list_;
  hdr.marked = false;
  gc_list_ = obj;
}

void GarbageCollector::UnmarkAll() const {
  for (IObject* obj = gc_list_; obj; obj = obj->hdr_.next)
    obj->hdr_.marked = false;
}

void GarbageCollector::Sweep() {
  IObject** cur = &gc_list_;
  while (*cur) {
    IObject* obj = *cur;
    if (!obj->hdr_.marked) {
      *cur = obj->hdr_.next;
      const size_t size = obj->Size();
      assert(allocated_bytes_ >= size);
      allocated_bytes_ -= size;
      delete obj;
    } else {
      obj->hdr_.marked = false;
      cur = &obj->hdr_.next;
    }
  }
}

}  // namespace serilang
