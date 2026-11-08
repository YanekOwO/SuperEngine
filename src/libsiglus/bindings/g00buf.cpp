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

#include "libsiglus/bindings/registry.hpp"

#include "core/image_resources.hpp"
#include "srbind/srbind.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>

namespace libsiglus::binding {

void BindG00buf(SiglusMachine& runtime) {
  if (!runtime.host().images || !runtime.host().gameexe)
    throw std::runtime_error("g00buf binding requires images and Gameexe");
  const int count =
      std::max(0, (*runtime.host().gameexe)("G00BUF.CNT").Int().value_or(16));
  auto images = runtime.host().images;
  srbind::module_ m(runtime.vm->gc_.get(), runtime.vm->globals_.get());

  struct Handle {
    std::shared_ptr<ImageResources> images;
    int index;
  };
  srbind::class_<Handle> buffer(m, "G00buf");
  buffer.def("load", [](Handle* h, std::string filename) {
    h->images->PreloadImage(h->index, filename);
  });
  buffer.def("free",
             [](Handle* h) { h->images->ClearPreloadedImage(h->index); });

  struct ListHandle {
    std::shared_ptr<ImageResources> images;
    int count;
  };
  srbind::class_<ListHandle> list_class(m, "G00bufList");
  auto list = list_class.inst("g00buf", images, count);
  list.def("size", [](const ListHandle* h) { return h->count; });
  list.def("free_all",
           [](ListHandle* h) { h->images->ClearPreloadedImages(); });
  list.def("__getitem__", [buffer](ListHandle* h, int index) mutable {
    if (index < 0 || index >= h->count)
      throw std::out_of_range(
          std::format("g00buf index {} out of range", index));
    return serilang::Value(buffer.make_inst(h->images, index));
  });
  runtime.reset_local_memory.emplace_back(
      [images] { images->ClearPreloadedImages(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_g00buf", BindG00buf)

}  // namespace libsiglus::binding
