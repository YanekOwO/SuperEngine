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

#pragma once

#include "core/colour.hpp"
#include "core/rect.hpp"
#include "glm/mat4x4.hpp"

#include <array>
#include <memory>
#include <optional>

class glTexture;
class glslProgram;
class glFrameBuffer;

struct glRenderable {
  std::shared_ptr<glTexture> texture_;
  Rect region;
};

struct glDestination {
  std::shared_ptr<glFrameBuffer> framebuf_;
  Rect region;
};

struct AlphaMaskConfig {
  std::shared_ptr<glTexture> texture;
  Rect screen_rect;
};

struct RenderingConfig {
  std::optional<glm::mat4> model;
  std::optional<RGBAColour> color;
  std::optional<RGBAColour> mask_color;
  std::optional<float> mono;
  std::optional<float> invert;
  std::optional<float> bright;
  std::optional<float> dark;
  std::optional<RGBColour> tint;
  std::optional<float> alpha;
  std::optional<std::array<float, 4>> vertex_alpha;
  std::optional<int> blend_type;
  std::optional<AlphaMaskConfig> alpha_mask;
  bool sample_texture_in_screen_space = false;
  // Framebuffer composites already contain the destination's translation.
  bool apply_render_offset = true;
};

class glRenderer {
 public:
  glRenderer();
  ~glRenderer();

  static void SetUp();

  void ClearBuffer(std::shared_ptr<glFrameBuffer> canvas_, RGBAColour color);

  void RenderColormask(glRenderable src,
                       glDestination dst,
                       const RGBAColour mask);
  void Render(glRenderable src, glDestination dst);
  void Render(glRenderable src, RenderingConfig config, glDestination dst);

 private:
  struct glBuffer;
};
