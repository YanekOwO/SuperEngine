// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include "core/haikei.hpp"

#include "core/colour.hpp"
#include "core/hik.hpp"
#include "core/image_resources.hpp"
#include "libreallive/alldefs.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <algorithm>
#include <cassert>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using libreallive::read_i32;

namespace {

int consume_i32(const char*& curpointer) {
  int x = read_i32(curpointer);
  curpointer += 4;
  return x;
}

std::string consume_string(const char*& curpointer) {
  int size = consume_i32(curpointer);
  std::string x(curpointer, size - 1);
  curpointer += size;
  return x;
}

class HIKScriptLoader {
 public:
  explicit HIKScriptLoader(std::shared_ptr<ImageResources> images)
      : images_(std::move(images)) {}

  std::shared_ptr<HIKScript> Load(const fs::path& file) {
    std::ifstream ifs(file, std::ios::binary);
    if (!ifs) {
      throw std::runtime_error("Could not read the contents of \"" +
                               file.string() + '"');
    }
    std::string hik_data(fs::file_size(file), '\0');
    ifs.read(hik_data.data(), hik_data.size());
    ifs.close();

    const char* curpointer = hik_data.data();
    const char* endpointer = hik_data.data() + hik_data.size();
    int a = consume_i32(curpointer), b = consume_i32(curpointer);
    if (a != 10000 || b != 10000)
      throw std::runtime_error("HIK Parse error: Invalid magic");

    while (curpointer < endpointer) {
      int property_id = consume_i32(curpointer);
      switch (property_id) {
        case 10100:
        case 10101:
        case 10102: {
          consume_i32(curpointer);
          break;
        }
        case 10103: {
          int width = consume_i32(curpointer);
          int height = consume_i32(curpointer);
          size_of_hik_ = Size(width, height);
          break;
        }
        case 20000: {
          number_of_layers_ = consume_i32(curpointer);
          break;
        }
        case 20001: {
          consume_i32(curpointer);
          layers_.emplace_back();
          break;
        }
        case 20100: {
          // String name of this layer? We can't make use of this.
          consume_string(curpointer);
          break;
        }
        case 20101: {
          int x = consume_i32(curpointer);
          int y = consume_i32(curpointer);
          CurrentLayer().top_offset = Point(x, y);
          break;
        }
        case 21000: {
          consume_i32(curpointer);
          break;
        }
        case 21001: {
          consume_i32(curpointer);
          consume_i32(curpointer);
          consume_i32(curpointer);
          consume_i32(curpointer);
          break;
        }
        case 21002: {
          consume_i32(curpointer);
          consume_i32(curpointer);
          consume_i32(curpointer);
          consume_i32(curpointer);
          consume_i32(curpointer);
          break;
        }
        case 21003: {
          consume_i32(curpointer);
          break;
        }
        case 21100: {
          consume_i32(curpointer);
          break;
        }
        case 21101: {
          consume_i32(curpointer);
          consume_i32(curpointer);
          consume_i32(curpointer);
          consume_i32(curpointer);
          break;
        }
        case 21200: {
          CurrentLayer().use_scrolling = consume_i32(curpointer);
          break;
        }
        case 21201: {
          int x = consume_i32(curpointer);
          int y = consume_i32(curpointer);
          CurrentLayer().start_point = Point(x, y);
          x = consume_i32(curpointer);
          y = consume_i32(curpointer);
          CurrentLayer().end_point = Point(x, y);
          break;
        }
        case 21202: {
          CurrentLayer().x_scroll_time_ms = consume_i32(curpointer);
          CurrentLayer().y_scroll_time_ms = consume_i32(curpointer);
          break;
        }
        case 21203: {
          consume_i32(curpointer);
          break;
        }
        case 21301: {
          CurrentLayer().use_clip_area = consume_i32(curpointer);
          break;
        }
        case 21300: {
          // GRP or REC?
          int x = consume_i32(curpointer);
          int y = consume_i32(curpointer);
          int x2 = consume_i32(curpointer);
          int y2 = consume_i32(curpointer);
          CurrentLayer().clip_area = Rect::GRP(x, y, x2, y2);
          break;
        }
        case 30000: {
          CurrentLayer().number_of_animations = consume_i32(curpointer);
          break;
        }
        case 30001: {
          consume_i32(curpointer);
          CurrentLayer().animations.emplace_back();
          break;
        }
        case 30100: {
          CurrentAnimation().use_multiframe_animation = consume_i32(curpointer);
          break;
        }
        case 30101: {
          CurrentAnimation().i_30101 = consume_i32(curpointer);
          break;
        }
        case 30102: {
          CurrentAnimation().i_30102 = consume_i32(curpointer);
          break;
        }
        case 40000: {
          CurrentAnimation().number_of_frames = consume_i32(curpointer);
          break;
        }
        case 40101: {
          for (int i = 0; i < 31; ++i) {
            consume_i32(curpointer);
          }

          CurrentAnimation().frames.emplace_back();
          break;
        }
        case 40102: {
          CurrentFrame().opacity = consume_i32(curpointer);
          break;
        }
        case 40103: {
          consume_i32(curpointer);
          consume_i32(curpointer);
          break;
        }
        case 40100: {
          HIKScript::Frame& frame = CurrentFrame();
          frame.image = consume_string(curpointer);
          frame.surface = images_->GetSurfaceNamed(frame.image);
          if (!frame.surface) {
            std::ostringstream oss;
            oss << "Could not load image " << frame.image << " for HIK";
            throw std::runtime_error(oss.str());
          }
          frame.grp_pattern = consume_i32(curpointer);
          frame.frame_length_ms = consume_i32(curpointer);
          break;
        }
        default: {
          std::ostringstream oss;
          oss << "HIK Parse exception. Unknown id: " << property_id;
          throw std::runtime_error(oss.str());
        }
      }
    }

    for (HIKScript::Layer& layer : layers_) {
      for (HIKScript::Animation& animation : layer.animations) {
        animation.total_time = 0;
        for (HIKScript::Frame& frame : animation.frames) {
          animation.total_time += frame.frame_length_ms;
        }
      }
    }

    // Records are in reverse order of what they should be.
    std::reverse(layers_.begin(), layers_.end());

    return std::make_shared<HIKScript>(std::move(layers_), number_of_layers_,
                                       size_of_hik_);
  }

 private:
  HIKScript::Layer& CurrentLayer() {
    if (layers_.size() == 0)
      throw std::runtime_error("Invalid layer reference");

    return layers_.back();
  }

  HIKScript::Animation& CurrentAnimation() {
    HIKScript::Layer& layer = CurrentLayer();
    if (layer.animations.size() == 0)
      throw std::runtime_error("Invalid unknowns reference");

    return layer.animations.back();
  }

  HIKScript::Frame& CurrentFrame() {
    HIKScript::Animation& animation = CurrentAnimation();
    if (animation.frames.size() == 0)
      throw std::runtime_error("Invalid frame reference");

    return animation.frames.back();
  }

  std::shared_ptr<ImageResources> images_;
  std::vector<HIKScript::Layer> layers_;
  int number_of_layers_ = 0;
  Size size_of_hik_;
};

}  // namespace

Haikei::Haikei(Size screen_size,
               SurfaceFactory surface_factory,
               std::shared_ptr<ImageResources> images,
               std::shared_ptr<Clock> clock)
    : screen_size_(screen_size),
      surface_factory_(std::move(surface_factory)),
      images_(std::move(images)),
      clock_(std::move(clock)),
      background_type_(BACKGROUND_DC0),
      preloaded_hik_scripts_(32) {
  haikei_ = surface_factory_(screen_size_);
  for (int i = 0; i < 16; ++i)
    display_contexts_[i] = surface_factory_(screen_size_);
}

Haikei::~Haikei() = default;

void Haikei::Reset() {
  ClearAllDCs();
  preloaded_hik_scripts_.Clear();
  hik_renderer_.reset();
  background_type_ = BACKGROUND_DC0;
}

std::shared_ptr<SDLSurface> Haikei::GetHaikei() {
  if (haikei_->RawSurface() == NULL) {
    haikei_->Allocate(screen_size_);
  }

  return haikei_;
}

void Haikei::AllocateDC(int dc, Size size) {
  if (dc < 0 || dc >= 16)
    throw std::runtime_error(
        std::format("Invalid DC number '{}' in Haikei::AllocateDC", dc));

  if (dc == 0)
    throw std::runtime_error("Attempting to reallocate DC 0!");

  if (dc == 1) {
    Size dc0_size = display_contexts_[0]->GetSize();
    if (size.width() < dc0_size.width())
      size.set_width(dc0_size.width());
    if (size.height() < dc0_size.height())
      size.set_height(dc0_size.height());
  }

  display_contexts_[dc]->Allocate(size);
}

void Haikei::SetMinimumSizeForDC(int dc, Size size) {
  if (display_contexts_[dc] == NULL || !display_contexts_[dc]->IsAllocated()) {
    AllocateDC(dc, size);
  } else {
    Size current = display_contexts_[dc]->GetSize();
    if (current.width() < size.width() || current.height() < size.height()) {
      Size maxSize = current.SizeUnion(size);

      std::shared_ptr<SDLSurface> newdc = surface_factory_(maxSize);
      display_contexts_[dc]->BlitToSurface(*newdc,
                                           display_contexts_[dc]->GetRect(),
                                           display_contexts_[dc]->GetRect());

      display_contexts_[dc] = newdc;
    }
  }
}

void Haikei::FreeDC(int dc) {
  if (dc == 0) {
    throw std::runtime_error("Attempt to deallocate DC[0]");
  } else if (dc == 1) {
    GetDC(1)->Fill(RGBAColour::Black());
  } else {
    display_contexts_[dc]->Deallocate();
  }
}

std::shared_ptr<SDLSurface> Haikei::GetDC(int dc) {
  assert(0 <= dc && dc < 16);

  if (display_contexts_[dc]->RawSurface() == NULL)
    AllocateDC(dc, display_contexts_[0]->GetSize());

  return display_contexts_[dc];
}

void Haikei::ClearAllDCs() {
  GetDC(0)->Fill(RGBAColour::Black());

  for (int i = 1; i < 16; ++i)
    FreeDC(i);
}

void Haikei::SetHikRenderer(HIKRenderer* renderer) {
  hik_renderer_.reset(renderer);
}

void Haikei::ClearHikRenderer() { hik_renderer_.reset(); }

void Haikei::LoadHikRenderer(const std::string& name,
                             const std::filesystem::path& file) {
  hik_renderer_ =
      std::make_unique<HIKRenderer>(clock_, GetHIKScript(name, file));
}

std::shared_ptr<HIKScript> Haikei::LoadHikFile(
    const std::filesystem::path& file_path) {
  HIKScriptLoader loader(images_);
  return loader.Load(file_path);
}

void Haikei::PreloadHIKScript(int slot,
                              const std::string& name,
                              const std::filesystem::path& file_path) {
  auto script = LoadHikFile(file_path);
  preloaded_hik_scripts_[slot] = std::make_pair(name, script);
}

void Haikei::ClearPreloadedHIKScript(int slot) {
  preloaded_hik_scripts_[slot] = std::make_pair("", nullptr);
}

void Haikei::ClearAllPreloadedHIKScripts() { preloaded_hik_scripts_.Clear(); }

std::shared_ptr<HIKScript> Haikei::GetHIKScript(
    const std::string& name,
    const std::filesystem::path& file_path) {
  for (HIKArrayItem& item : preloaded_hik_scripts_) {
    if (item.first == name)
      return item.second;
  }

  return LoadHikFile(file_path);
}
