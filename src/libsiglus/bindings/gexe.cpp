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

#include "core/gameexe.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "srbind/srbind.hpp"
#include "utilities/string_utilities.hpp"
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

struct GameexeHandle {
  std::shared_ptr<Gameexe> gexe;
  void Clear() { *gexe = Gameexe(); }
  void LoadFrom(std::string path) {
    if (auto result = Gameexe::FromFile(std::filesystem::path(std::move(path))))
      *gexe = result.value();
    else
      throw std::runtime_error(result.error().message);
  }
  std::string Get(std::string key) {
    return Join(",", (*gexe)(key).ToStrVec());
  }
  void AddParse(std::string file) {
    std::ifstream ifs(file);
    if (!ifs)
      throw std::runtime_error("Cannot open file: " + file);
    std::string lines((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
    for (auto part : std::views::split(lines, '\n')) {
      std::string_view line(&*part.begin(), std::ranges::distance(part));
      if (line.empty())
        continue;
      auto result = gexe->ParseLine(line, -1);
      if (!result.has_value())
        throw std::runtime_error(result.error().GetDebugString());
    }
  }
  void Set(std::string line) { gexe->parseLine(line); }
  std::string GetRange(std::string prefix) {
    std::string result;
    for (auto it : gexe->Filter(prefix))
      result += std::format("{}={}\n", it.key(), Join(",", it.ToStrVec()));
    return result;
  }
};

void BindGexe(SiglusMachine& runtime) {
  sb::module_ m(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  sb::class_<GameexeHandle> gh(m, "gexe");

  gh.def(
      sb::init([gexe = runtime.gameexe]() { return new GameexeHandle(gexe); }));

  gh.def("clear", &GameexeHandle::Clear);
  gh.def("load_from", &GameexeHandle::LoadFrom);
  gh.def("__getitem__", &GameexeHandle::Get);
  gh.def("set", &GameexeHandle::Set);
  gh.def("get_range", &GameexeHandle::GetRange);
  gh.def("add_parse", &GameexeHandle::AddParse);
}

RLVM_REGISTER(SiglusBindingRegistry, "gexe", BindGexe)

}  // namespace libsiglus::binding
