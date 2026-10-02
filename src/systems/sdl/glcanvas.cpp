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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#include "systems/sdl/glcanvas.hpp"

#include "systems/sdl/gl_frame_buffer.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/gltexture.hpp"

#include <cstring>

#include <GL/glew.h>

glCanvas::glCanvas(Size resolution,
                   std::optional<Size> display_size,
                   std::optional<Point> origin)
    : resolution_(resolution),
      display_size_(display_size.value_or(resolution)),
      origin_(origin.value_or(Point(0, 0))),
      frame_buf_(std::make_shared<glFrameBuffer>(
          std::make_shared<glTexture>(resolution))),
      renderer_(std::make_shared<glRenderer>()) {
  renderer_->ClearBuffer(frame_buf_, RGBAColour::Black());
}

void glCanvas::Use() {
  glViewport(0, 0, resolution_.width(), resolution_.height());

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, (GLdouble)resolution_.width(), (GLdouble)resolution_.height(),
          0.0, 0.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  const auto aspect_ratio_w =
      static_cast<float>(display_size_.width()) / resolution_.width();
  const auto aspect_ratio_h =
      static_cast<float>(display_size_.height()) / resolution_.height();
  glTranslatef(origin_.x() * aspect_ratio_w, origin_.y() * aspect_ratio_h, 0);
}

std::shared_ptr<glFrameBuffer> glCanvas::GetBuffer() const {
  return frame_buf_;
}
