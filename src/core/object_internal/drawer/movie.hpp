// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#pragma once

#include "core/avdec/omv.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "utilities/clock.hpp"

#include <filesystem>
#include <memory>
#include <vector>

class IGraphicsBackend;

class ObjectMovieData : public GraphicsObjectData {
 public:
  ObjectMovieData(std::filesystem::path path,
                  bool loop,
                  bool auto_free,
                  bool real_time,
                  bool ready_only,
                  std::shared_ptr<IGraphicsBackend> backend,
                  std::shared_ptr<Clock> clock);
  ~ObjectMovieData() override;

  int PixelWidth(const GraphicsObject& go) override;
  int PixelHeight(const GraphicsObject& go) override;
  RGBAColour GetPixelColor(Point point, int cut_no) const override;
  std::unique_ptr<GraphicsObjectData> Clone() const override;
  void Execute() override;

  void Pause();
  void Resume();
  void Seek(int time);
  int GetSeekTime() const;
  bool CheckMovie() const;
  void EndLoop();
  void SetAutoFree(bool value);

 protected:
  std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& go) const override;
  Point DstOrigin(const GraphicsObject& go) const override;

 private:
  void DecodeCurrentFrame(bool force);
  unsigned int FrameDurationMilliseconds() const;
  unsigned int Now() const;

  std::filesystem::path path_;
  bool loop_;
  bool auto_free_;
  bool real_time_;
  bool paused_;
  std::shared_ptr<IGraphicsBackend> backend_;
  std::shared_ptr<Clock> clock_;
  std::unique_ptr<OmvDecoder> decoder_;
  std::shared_ptr<SDLSurface> surface_;
  std::vector<char> frame_;
  int current_time_ = 0;
  unsigned int last_tick_ = 0;
};
