// -----------------------------------------------------------------------
//
// This file is part of RLVM
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
// -----------------------------------------------------------------------

#include "libsiglus/bindings/gexe.hpp"
#include "core/gameexe.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "srbind/srbind.hpp"
#include "utilities/string_utilities.hpp"
#include "vm/serialization.hpp"
#include "vm/vm.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace libsiglus::binding {
namespace sb = srbind;
using namespace serilang;

void BindGexe(SiglusMachine& runtime) {
  sb::module_ m(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  sb::class_<GameexeHandle> gh(m, "gexe");
  RegisterBindingClass(runtime, gh);
  gh.def_serialization(
      [](const GameexeHandle& h, serilang::ObjectWriter& out) {},
      [&runtime](serilang::ObjectReader& in) {
        auto& host = runtime.host();
        return std::make_unique<GameexeHandle>(host.gameexe);
      });

  gh.def(sb::init(
      [gexe = runtime.host().gameexe]() { return new GameexeHandle(gexe); }));

  gh.def("clear", [](GameexeHandle* h) { *h->gexe = Gameexe(); });
  gh.def("load_from", [](GameexeHandle* h, std::string path) {
    if (auto result = Gameexe::FromFile(std::filesystem::path(std::move(path))))
      *h->gexe = result.value();
    else
      throw std::runtime_error(result.error().message);
  });
  gh.def("__getitem__", [](GameexeHandle* h, std::string key) {
    return Join(",", (*h->gexe)(key).ToStrVec());
  });
  gh.def("set",
         [](GameexeHandle* h, std::string line) { h->gexe->parseLine(line); });
  gh.def("get_range", [](GameexeHandle* h, std::string prefix) {
    std::string result;
    for (auto it : h->gexe->Filter(prefix))
      result += std::format("{}={}\n", it.key(), Join(",", it.ToStrVec()));
    return result;
  });
  gh.def("add_parse", [](GameexeHandle* h, std::string file) {
    std::ifstream ifs(file);
    if (!ifs)
      throw std::runtime_error("Cannot open file: " + file);
    std::string lines((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
    for (auto part : std::views::split(lines, '\n')) {
      std::string_view line(&*part.begin(), std::ranges::distance(part));
      if (line.empty())
        continue;
      auto result = h->gexe->ParseLine(line, -1);
      if (!result.has_value())
        throw std::runtime_error(result.error().GetDebugString());
    }
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "gexe", BindGexe)

}  // namespace libsiglus::binding
