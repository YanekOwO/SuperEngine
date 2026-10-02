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

#include "systems/sdl/gl_utils.hpp"

#include <GL/glew.h>

#include <stdexcept>

void InitGLEW() {
  const GLenum status = glewInit();
  if (status == GLEW_OK)
    return;

  // GLEW loads the OpenGL entry points before attempting GLX initialization.
  // EGL contexts (Wayland and offscreen SDL drivers) have no GLX display.
  if (status == GLEW_ERROR_NO_GLX_DISPLAY && glGetString(GL_VERSION))
    return;

  throw std::runtime_error(
      "Failed to initialize GLEW (" + std::to_string(status) +
      "): " + reinterpret_cast<const char*>(glewGetErrorString(status)));
}

std::string GetGLErrors(void) {
  GLenum error;
  std::string msg;

  if ((error = glGetError()) != GL_NO_ERROR) {
    switch (error) {
      case GL_NO_ERROR:
        msg += "No error has been recorded.";
      case GL_INVALID_ENUM:
        msg += "An unacceptable value is specified for an enumerated argument.";
      case GL_INVALID_VALUE:
        msg += "A numeric argument is out of range.";
      case GL_INVALID_OPERATION:
        msg += "The specified operation is not allowed in the current state.";
      case GL_STACK_OVERFLOW:
        msg += "This command would cause a stack overflow.";
      case GL_STACK_UNDERFLOW:
        msg += "This command would cause a stack underflow.";
      case GL_OUT_OF_MEMORY:
        msg += "There is not enough memory left to execute the command.";
      case GL_INVALID_FRAMEBUFFER_OPERATION:
        msg += "The framebuffer object is not complete.";
      default:
        msg += "An unknown OpenGL error has occurred.";
    }
  }
  return msg;
}

void ShowGLErrors() {
  auto error = GetGLErrors();
  if (!error.empty())
    throw std::runtime_error("GL error: " + error);
}

// -----------------------------------------------------------------------

bool IsNPOTSafe() {
#ifndef GLEW_ARB_texture_non_power_of_two
  static bool is_safe = false;
#else
  static bool is_safe = GLEW_ARB_texture_non_power_of_two;
#endif
  return is_safe;
}

int GetMaxTextureSize() {
  static GLint max_texture_size = 0;
  if (max_texture_size == 0) {
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    if (max_texture_size > 4096) {
      // Little Busters tries to page in 9 images, each 1,200 x 12,000. The AMD
      // drivers do *not* like dealing with those images as one texture, even
      // if it advertises that it can. Chopping those images doesn't fix the
      // memory consumption, but helps (slightly) with the allocation pause.
      max_texture_size = 4096;
    }
  }

  return max_texture_size;
}

int SafeSize(int i) {
  const GLint max_texture_size = GetMaxTextureSize();
  if (i > max_texture_size)
    return max_texture_size;

  if (IsNPOTSafe()) {
    return i;
  }

  for (int p = 0; p < 24; p++) {
    if (i <= (1 << p))
      return 1 << p;
  }

  return max_texture_size;
}
