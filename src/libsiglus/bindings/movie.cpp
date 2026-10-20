// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/asset_scanner.hpp"
#include "core/avdec/ffmpeg.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "long_operations/siglus_movie.hpp"
#include "long_operations/siglus_wait.hpp"
#include "srbind/module.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "vm/exception.hpp"
#include "vm/future.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;
namespace fs = std::filesystem;
namespace chr = std::chrono;

namespace {

const std::set<std::string> kMovieExtensions{"wmv", "asf", "avi", "mpg",
                                             "mpeg"};

class SiglusMovie {
 public:
  explicit SiglusMovie(SiglusMachine& runtime)
      : runtime_(runtime),
        vm_(runtime.vm.get()),
        system_(runtime.system.get()),
        asset_scanner_(runtime.asset_scanner) {}

  lop::MovieRect DefaultRect() const {
    lop::MovieRect rect;
    if (system_) {
      Size size = system_->graphics().screen_size();
      rect.width = size.width();
      rect.height = size.height();
    }
    return rect;
  }

  void Play(std::string file_name,
            std::optional<lop::MovieRect> maybe_rect,
            bool key_skip = false) {
    lop::MovieRect rect = DefaultRect();
    rect = maybe_rect.value_or(rect);

    Stop();
    if (!vm_ || !system_ || !asset_scanner_)
      return;

    fs::path path = FindMovie(file_name);

    if (system_->ShouldFastForward()) {
      FfmpegVideoDecoder validate(path);
      return;
    }

    std::unique_ptr<lop::SiglusMovieOperation> player =
        std::make_unique<lop::SiglusMovieOperation>(system_, std::move(path),
                                                    rect, 255, key_skip);
    player_ = player.get();
    runtime_.movie_frame = player->frame();
    pending_ = lop::FutureBackedCoroutineTask(*vm_, std::move(player),
                                              system_->event_ptr().get());
    pending_->Start();
  }

  bool IsPlaying() const {
    if (!player_)
      return false;
    return player_->IsPlaying();
  }

  sr::Value Wait(sr::VM& vm, bool key_skip) {
    if (!player_ || !pending_)
      return lop::MakeResolvedFuture(*vm.gc_);
    player_->SetWaitKey(key_skip);
    sr::Future* fut = pending_->MakeFuture(*vm.gc_);
    return sr::Value(fut);
  }

  void Stop() {
    if (!player_)
      return;
    player_->Stop();
    player_ = nullptr;
    pending_.reset();
  }

  std::filesystem::path FindMovie(const std::string& file_name) const {
    if (!asset_scanner_)
      throw std::runtime_error("mov.play requires an asset scanner");

    auto path = asset_scanner_->FindFile(file_name, kMovieExtensions);
    if (!path) {
      throw std::runtime_error("mov.play could not find " + file_name + ": " +
                               path.error().what());
    }
    return path.value();
  }

  SiglusMachine& runtime_;
  sr::VM* vm_ = nullptr;
  System* system_ = nullptr;
  std::shared_ptr<AssetScanner> asset_scanner_;
  lop::SiglusMovieOperation* player_ = nullptr;
  std::optional<lop::FutureBackedCoroutineTask> pending_;
};

}  // namespace

static std::pair<std::string, std::optional<lop::MovieRect>> ParsePlayArgs(
    std::vector<sr::Value> args) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(args));
  if (packet.args.empty())
    throw std::runtime_error("mov.play expects a movie name");

  std::optional<lop::MovieRect> rect;

  std::string file_name = AsString(packet.args[0]);
  if (file_name.empty())
    throw std::runtime_error("mov.play movie name is empty");

  if (packet.args.size() >= 5) {
    rect = {};
    rect->x = RequireInt(packet.args[1], "mov.play x");
    rect->y = RequireInt(packet.args[2], "mov.play y");
    rect->width = RequireInt(packet.args[3], "mov.play width");
    rect->height = RequireInt(packet.args[4], "mov.play height");
  }

  return {std::move(file_name), rect};
}

void BindMovie(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<SiglusMovie> movie_cls(m, "Movie", false);
  auto movie = movie_cls.inst("mov", runtime);

  movie.def(
      "play",
      [](SiglusMovie* movie, std::vector<sr::Value> args) {
        auto [file_name, rect] = ParsePlayArgs(std::move(args));
        movie->Play(std::move(file_name), rect);
      },
      sb::vararg);
  movie.def(
      "play_wait",
      [](SiglusMovie* movie, sr::VM& vm, std::vector<sr::Value> args) {
        auto [file_name, rect] = ParsePlayArgs(std::move(args));
        movie->Play(std::move(file_name), rect);
        return movie->Wait(vm, false);
      },
      sb::vararg);
  movie.def(
      "play_waitkey",
      [](SiglusMovie* movie, sr::VM& vm, std::vector<sr::Value> args) {
        auto [file_name, rect] = ParsePlayArgs(std::move(args));
        movie->Play(std::move(file_name), rect, true);
        return movie->Wait(vm, true);
      },
      sb::vararg);
  movie.def("stop", &SiglusMovie::Stop);
}

RLVM_REGISTER(SiglusBindingRegistry, "movie", BindMovie)

}  // namespace libsiglus::binding
