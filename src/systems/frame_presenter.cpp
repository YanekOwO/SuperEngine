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

#include "systems/frame_presenter.hpp"

#include "utilities/graphics.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

FramePresenter::FramePresenter(std::shared_ptr<IGraphicsBackend> backend,
                               Size screen_size,
                               bool fullscreen)
    : backend_(std::move(backend)), screen_size_(screen_size) {
  if (!backend_ || screen_size.width() <= 0 || screen_size.height() <= 0)
    throw std::invalid_argument(
        "FramePresenter requires a backend and positive screen dimensions");
  backend_->InitSystem(screen_size, fullscreen);
  Resize(screen_size, fullscreen);
}

void FramePresenter::Resize(Size display_size, bool fullscreen) {
  display_size_ = backend_->Resize(display_size, fullscreen);
}

Point FramePresenter::DisplayToScreenPoint(Point display_pos) const {
  const Rect view = AspectFitRect(screen_size(), display_size_);
  const float scale = static_cast<float>(view.width()) / screen_size().width();
  const int x = static_cast<int>((display_pos.x() - view.x()) / scale);
  const int y = static_cast<int>((display_pos.y() - view.y()) / scale);
  return Point(std::clamp(x, 0, screen_size().width() - 1),
               std::clamp(y, 0, screen_size().height() - 1));
}

void FramePresenter::WarpMouse(Point game_pos) {
  const Rect view = AspectFitRect(screen_size(), display_size_);
  const float scale = static_cast<float>(view.width()) / screen_size().width();
  backend_->WarpMouse(Point(view.x() + static_cast<int>(game_pos.x() * scale),
                            view.y() + static_cast<int>(game_pos.y() * scale)));
}

RenderFrameConfig FramePresenter::FrameConfig(bool cache_scene,
                                              Point origin) const {
  return {.screen_size = screen_size_,
          .display_size = display_size_,
          .screen_origin = origin,
          .cache_scene = cache_scene};
}

void FramePresenter::RenderFrame(FrameRenderOptions options,
                                 const DrawCallback& scene,
                                 const DrawCallback& cursor,
                                 Point origin) {
  const auto config = FrameConfig(options.cache_scene, origin);
  if (options.redraw_scene)
    backend_->RenderFrame(config, scene, cursor);
  else
    backend_->RedrawLastFrame(config, cursor);
}

std::shared_ptr<SDLSurface> FramePresenter::RenderToSurface(
    const DrawCallback& scene,
    Point origin) {
  return backend_->RenderToSurface(FrameConfig(false, origin), scene);
}

std::shared_ptr<SDLSurface> FramePresenter::RenderToSurface(
    Size size,
    const DrawCallback& draw,
    bool transparent,
    Point offset) {
  if (size.width() <= 0 || size.height() <= 0)
    throw std::invalid_argument("Capture dimensions must be positive");
  RenderFrameConfig config{.screen_size = size,
                           .display_size = size,
                           .screen_origin = {},
                           .cache_scene = false,
                           .transparent = transparent,
                           .render_offset = offset};
  return backend_->RenderToSurface(config, draw);
}
