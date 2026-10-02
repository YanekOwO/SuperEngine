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

#include "systems/sdl/screen_canvas.hpp"

#include "systems/sdl/gltexture.hpp"

#include <GL/glew.h>

ScreenCanvas::ScreenCanvas(Size size)
    : glFrameBuffer(std::make_shared<glTexture>(size)) {}

std::shared_ptr<glTexture> ScreenCanvas::GetTexture() const {
  const Size size = GetSize();
  auto result = std::make_shared<glTexture>(size);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, GetID());
  glBindTexture(GL_TEXTURE_2D, result->GetID());
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, size.width(),
                      size.height());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

  return result;
}
