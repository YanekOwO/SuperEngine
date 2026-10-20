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

#include "systems/sdl/glrenderer.hpp"

#include "systems/sdl/gl_frame_buffer.hpp"
#include "systems/sdl/gl_utils.hpp"
#include "systems/sdl/glshaders.hpp"
#include "systems/sdl/gltexture.hpp"
#include "systems/sdl/shaders.hpp"

#include <GL/glew.h>
#include "glm/gtc/matrix_transform.hpp"

#include <format>

struct glRenderer::glBuffer {
  GLuint VAO, VBO, EBO;
};

glRenderer::glRenderer() = default;
glRenderer::~glRenderer() = default;

void glRenderer::SetUp() {
  glEnable(GL_TEXTURE_2D);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glShadeModel(GL_SMOOTH);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glDepthFunc(GL_LEQUAL);
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  ShowGLErrors();
}

void glRenderer::ClearBuffer(std::shared_ptr<glFrameBuffer> canvas,
                             RGBAColour color) {
  glBindFramebuffer(GL_FRAMEBUFFER, canvas->GetID());
  glClearColor(color.r_float(), color.g_float(), color.b_float(),
               color.a_float());
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void glRenderer::RenderColormask(glRenderable src,
                                 glDestination dst,
                                 const RGBAColour mask) {
  auto canvas = dst.framebuf_;
  const auto canvas_size = canvas->GetSize();
  const auto texture_size = src.texture_->GetSize();

  int x1 = src.region.x(), y1 = src.region.y(), x2 = src.region.x2(),
      y2 = src.region.y2();
  int fdx1 = dst.region.x(), fdy1 = dst.region.y(), fdx2 = dst.region.x2(),
      fdy2 = dst.region.y2();

  const Point offset = canvas->render_offset();
  fdx1 += offset.x();
  fdx2 += offset.x();
  fdy1 += offset.y();
  fdy2 += offset.y();

  auto toNDC = [screen_size = canvas_size](int x, int y) {
    const auto w = screen_size.width();
    const auto h = screen_size.height();
    return std::make_pair(2.0f * x / w - 1.0f, 1.0f - (2.0f * y / h));
  };
  auto [dx1, dy1] = toNDC(fdx1, fdy1);
  auto [dx2, dy2] = toNDC(fdx2, fdy2);

  float thisx1 = float(x1) / texture_size.width();
  float thisy1 = 1.0f - float(y1) / texture_size.height();
  float thisx2 = float(x2) / texture_size.width();
  float thisy2 = 1.0f - float(y2) / texture_size.height();

  std::shared_ptr<glTexture> background = canvas->GetTexture();

  float bgx1 = float(fdx1) / canvas_size.width();
  float bgy1 = 1.0f - float(fdy1) / canvas_size.height();
  float bgx2 = float(fdx2) / canvas_size.width();
  float bgy2 = 1.0f - float(fdy2) / canvas_size.height();

  static glBuffer buf = []() {
    GLuint VAO, VBO, EBO;
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), NULL, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    ShowGLErrors();
    return glBuffer{VAO, VBO, EBO};
  }();

  auto shader = _GetColorMaskShader();
  glUseProgram(shader->GetID());
  glBindFramebuffer(GL_FRAMEBUFFER, canvas->GetID());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, background->GetID());
  shader->SetUniform("texture0", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, src.texture_->GetID());
  shader->SetUniform("texture1", 1);
  shader->SetUniform("color", mask.r_float(), mask.g_float(), mask.b_float(),
                     mask.a_float());
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glBindVertexArray(buf.VAO);
  float vertices[] = {
      dx1, dy1, bgx1, bgy1, thisx1, thisy1,  // NOLINT
      dx2, dy1, bgx2, bgy1, thisx2, thisy1,  // NOLINT
      dx2, dy2, bgx2, bgy2, thisx2, thisy2,  // NOLINT
      dx1, dy2, bgx1, bgy2, thisx1, thisy2   // NOLINT
  };
  glBindBuffer(GL_ARRAY_BUFFER, buf.VBO);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

  glUseProgram(0);
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBlendFunc(GL_ONE, GL_ZERO);
  ShowGLErrors();
}

void glRenderer::Render(glRenderable src, glDestination dst) {
  this->Render(src, RenderingConfig(), dst);
}

void glRenderer::Render(glRenderable src,
                        RenderingConfig cfg,
                        glDestination dst) {
  auto canvas = dst.framebuf_;
  const auto canvas_size = canvas->GetSize();
  const auto texture_size = src.texture_->GetSize();

  static glBuffer buf = []() {
    GLuint VAO, VBO, EBO;
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, 20 * sizeof(float), NULL, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    ShowGLErrors();
    return glBuffer{VAO, VBO, EBO};
  }();

  int x1 = src.region.x(), y1 = src.region.y(), x2 = src.region.x2(),
      y2 = src.region.y2();
  int fdx1 = dst.region.x(), fdy1 = dst.region.y(), fdx2 = dst.region.x2(),
      fdy2 = dst.region.y2();

  auto model = cfg.model.value_or(
      glm::translate(glm::mat4(1.0f), glm::vec3(fdx1, fdy1, 0)));
  const Point offset =
      cfg.apply_render_offset ? canvas->render_offset() : Point{};
  model =
      glm::translate(glm::mat4(1.0f), glm::vec3(offset.x(), offset.y(), 0)) *
      model;

  auto toNDC = [w = canvas_size.width(),
                h = canvas_size.height()](glm::vec4 point) {
    const auto pt = glm::vec2(point) / point.w;
    return std::make_pair(2.0f * pt.x / w - 1.0f, 1.0f - (2.0f * pt.y / h));
  };
  const auto width = fdx2 - fdx1;
  const auto height = fdy2 - fdy1;
  const auto top_left = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  const auto top_right = glm::vec4(width, 0.0f, 0.0f, 1.0f);
  const auto bottom_right = glm::vec4(width, height, 0.0f, 1.0f);
  const auto bottom_left = glm::vec4(0.0f, height, 0.0f, 1.0f);
  auto [dx1, dy1] = toNDC(model * top_left);
  auto [dx2, dy2] = toNDC(model * top_right);
  auto [dx3, dy3] = toNDC(model * bottom_right);
  auto [dx4, dy4] = toNDC(model * bottom_left);

  float thisx1 = float(x1) / texture_size.width();
  float thisy1 = 1.0f - float(y1) / texture_size.height();
  float thisx2 = float(x2) / texture_size.width();
  float thisy2 = 1.0f - float(y2) / texture_size.height();

  auto op = cfg.vertex_alpha.value_or(std::array<float, 4>{1.0, 1.0, 1.0, 1.0});
  for (int i = 0; i < 4; ++i) {
    if (op[i] < 0.0f || op[i] > 1.0f)
      throw std::runtime_error("glRenderer: Invalid opacity " +
                               std::format("op[{}]={}", i, op[i]));
  }

  float vertices[] = {
      dx1, dy1, thisx1, thisy1, op[0],  // NOLINT
      dx2, dy2, thisx2, thisy1, op[1],  // NOLINT
      dx3, dy3, thisx2, thisy2, op[2],  // NOLINT
      dx4, dy4, thisx1, thisy2, op[3]   // NOLINT
  };
  const GLuint VAO = buf.VAO, VBO = buf.VBO;
  glBindVertexArray(VAO);

  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

  auto shader = _GetObjectShader();
  glUseProgram(shader->GetID());

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, src.texture_->GetID());
  shader->SetUniform("texture0", 0);
  shader->SetUniform("sample_texture_in_screen_space",
                     cfg.sample_texture_in_screen_space ? 1 : 0);
  shader->SetUniform("use_alpha_mask", cfg.alpha_mask.has_value() ? 1 : 0);
  shader->SetUniform("canvas_height", static_cast<float>(canvas_size.height()));
  if (cfg.alpha_mask) {
    const Rect& rect = cfg.alpha_mask->screen_rect;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, cfg.alpha_mask->texture->GetID());
    shader->SetUniform("alpha_mask", 1);
    shader->SetUniform(
        "alpha_mask_rect", static_cast<float>(rect.x() + offset.x()),
        static_cast<float>(rect.y() + offset.y()),
        static_cast<float>(rect.width()), static_cast<float>(rect.height()));
  }

  auto color = cfg.color.value_or(RGBAColour(0, 0, 0, 0));
  shader->SetUniform("color", color.r_float(), color.g_float(), color.b_float(),
                     color.a_float());

  auto mask_color = cfg.mask_color.value_or(RGBAColour(0, 0, 0, 0));
  shader->SetUniform("mask_color", mask_color.r_float(), mask_color.g_float(),
                     mask_color.b_float(), mask_color.a_float());

  switch (cfg.blend_type.value_or(0)) {
    case 0:
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                          GL_ONE_MINUS_SRC_ALPHA);
      glBlendEquation(GL_FUNC_ADD);
      break;
    case 1:
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      glBlendEquation(GL_FUNC_ADD);
      break;
    case 2:
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
      break;
    case 3:
      glBlendFunc(GL_ZERO, GL_SRC_COLOR);
      glBlendEquation(GL_FUNC_ADD);
      break;
    case 4:
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
      glBlendEquation(GL_FUNC_ADD);
      break;
    default:
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                          GL_ONE_MINUS_SRC_ALPHA);
      glBlendEquation(GL_FUNC_ADD);
      break;
  }
  shader->SetUniform("blend_type", cfg.blend_type.value_or(0));

  auto mono = cfg.mono.value_or(0.0f);
  shader->SetUniform("mono", mono);

  auto invert = cfg.invert.value_or(0.0f);
  shader->SetUniform("invert", invert);

  auto bright = cfg.bright.value_or(0.0f);
  shader->SetUniform("bright", bright);

  auto dark = cfg.dark.value_or(0.0f);
  shader->SetUniform("dark", dark);

  auto alpha = cfg.alpha.value_or(1.0f);
  shader->SetUniform("alpha", alpha);

  auto tint = cfg.tint.value_or(RGBColour(0, 0, 0));
  shader->SetUniform("tint", tint.r_float(), tint.g_float(), tint.b_float());

  glBindVertexArray(VAO);
  glBindFramebuffer(GL_FRAMEBUFFER, canvas->GetID());
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

  glUseProgram(0);
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBlendFunc(GL_ONE, GL_ZERO);
  ShowGLErrors();
}
