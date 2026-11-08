// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of libreallive, a dependency of RLVM.
//
// -----------------------------------------------------------------------
//
// Copyright (c) 2006, 2007 Peter Jolly
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "libreallive/alldefs.hpp"
#include "libreallive/scenario.hpp"

namespace libreallive {

struct XorKey;

namespace fs = std::filesystem;

// Interface to an archived or loose collection of RealLive scenarios.
class Archive {
 public:
  Archive() = default;
  virtual ~Archive();

  // Accepts a scenario archive file or a directory containing loose scenes.
  // Uses |regname| to look up per-game xor keys for newer games.
  Archive(const fs::path& filename, const std::string& regname);

  // Returns a specific scenario by |index| number or NULL if none exist.
  virtual Scenario* GetScenario(int index);

  int GetFirstScenarioID() const;

  // Does a quick pass through all scenarios in the archive, looking for any
  // with non-default encoding. This short circuits when it finds one.
  int GetProbableEncodingType() const;

  std::string regname_;

 private:
  typedef std::map<int, FilePos> toc_t;

  void ReadTOC(const fs::path& filepath);

  void ReadOverrides(const fs::path& directory);

  void FindXorKey();

  toc_t toc_;
  std::map<int, std::unique_ptr<Scenario>> scenario_;

  // Mappings to unarchived SEEN\d{4}.TXT files on disk.
  // std::vector<std::unique_ptr<Mapping>> maps_to_delete_;

  // Now that VisualArts is using per game xor keys, this is equivalent to the
  // game's second level xor key.
  const XorKey* second_level_xor_key_;
};

}  // namespace libreallive
