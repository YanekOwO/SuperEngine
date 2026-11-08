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

#include "core/object_internal/drawer/movie.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include "core/object.hpp"
#include "systems/igraphics_backend.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

ObjectMovieData::ObjectMovieData(std::filesystem::path path,
                                 bool loop,
                                 bool auto_free,
                                 bool real_time,
                                 bool ready_only,
                                 std::shared_ptr<IGraphicsBackend> backend,
                                 std::shared_ptr<Clock> clock)
    : path_(std::move(path)),
      loop_(loop),
      auto_free_(auto_free),
      real_time_(real_time),
      paused_(ready_only),
      backend_(std::move(backend)),
      clock_(std::move(clock)),
      decoder_(std::make_unique<OmvDecoder>(path_, loop_)),
      last_tick_(Now()) {
  if (!backend_)
    throw std::runtime_error("ObjectMovieData requires a graphics backend");
  if (!clock_)
    clock_ = std::make_shared<Clock>();

  const std::size_t frame_size =
      static_cast<std::size_t>(decoder_->width()) * decoder_->height() * 4;
  frame_.resize(frame_size);
  decoder_->DecodeFrame(0, frame_, true);
  surface_ = backend_->CreateSurfaceBGRA(decoder_->info().size, frame_, true);
}

ObjectMovieData::~ObjectMovieData() = default;

unsigned int ObjectMovieData::Now() const {
  if (!clock_)
    return 0;
  return static_cast<unsigned int>(clock_->GetTicks().count());
}

int ObjectMovieData::PixelWidth(const GraphicsObject& go) {
  if (!surface_)
    return 0;
  return static_cast<int>(go.Param().GetWidthScaleFactor() *
                          surface_->GetRect().width());
}

int ObjectMovieData::PixelHeight(const GraphicsObject& go) {
  if (!surface_)
    return 0;
  return static_cast<int>(go.Param().GetHeightScaleFactor() *
                          surface_->GetRect().height());
}

std::unique_ptr<GraphicsObjectData> ObjectMovieData::Clone() const {
  if (!surface_) {
    auto empty = std::unique_ptr<ObjectMovieData>(new ObjectMovieData);
    empty->path_ = path_;
    empty->loop_ = loop_;
    empty->auto_free_ = auto_free_;
    empty->real_time_ = real_time_;
    empty->paused_ = paused_;
    empty->clock_ = clock_;
    empty->backend_ = backend_;
    return empty;
  }
  auto cloned = std::make_unique<ObjectMovieData>(
      path_, loop_, auto_free_, real_time_, paused_, backend_, clock_);
  cloned->Seek(current_time_);
  if (paused_)
    cloned->Pause();
  return cloned;
}

RGBAColour ObjectMovieData::GetPixelColor(Point point, int cut_no) const {
  return surface_ ? surface_->GetPixelColor(point, cut_no)
                  : RGBAColour::Clear();
}

void ObjectMovieData::Execute() {
  if (!decoder_ || !surface_)
    return;

  const unsigned int now = Now();
  if (!paused_) {
    unsigned int delta = now >= last_tick_ ? now - last_tick_ : 0;
    if (!real_time_)
      delta = std::min(delta, FrameDurationMilliseconds());
    current_time_ += static_cast<int>(delta);
  }
  last_tick_ = now;

  if (loop_ && decoder_->total_time() > 0 &&
      current_time_ > decoder_->total_time()) {
    current_time_ %= decoder_->total_time();
  }

  int frame_no = 0;
  if (decoder_->CheckNeedUpdate(current_time_, &frame_no, false)) {
    decoder_->DecodeFrame(frame_no, frame_, false);
    surface_->UpdateBGRA(frame_, true);
  }

  if (!decoder_->IsPlaying()) {
    if (auto_free_) {
      surface_.reset();
    } else {
      paused_ = true;
    }
  }
}

void ObjectMovieData::Pause() { paused_ = true; }

void ObjectMovieData::Resume() {
  paused_ = false;
  last_tick_ = Now();
}

void ObjectMovieData::Seek(int time) {
  current_time_ = std::max(time, 0);
  last_tick_ = Now();
  if (decoder_)
    decoder_->UpdateTimeOnly(current_time_);
  DecodeCurrentFrame(true);
}

int ObjectMovieData::GetSeekTime() const {
  if (!decoder_ || decoder_->total_time() <= 0)
    return 0;
  return current_time_ % decoder_->total_time();
}

bool ObjectMovieData::CheckMovie() const {
  return decoder_ && decoder_->IsPlaying();
}

void ObjectMovieData::EndLoop() {
  loop_ = false;
  if (decoder_)
    decoder_->EndLoop();
}

void ObjectMovieData::SetAutoFree(bool value) { auto_free_ = value; }

std::shared_ptr<const SDLSurface> ObjectMovieData::CurrentSurface(
    const GraphicsObject&) const {
  return surface_;
}

Point ObjectMovieData::DstOrigin(const GraphicsObject& go) const {
  if (decoder_)
    return decoder_->info().center;
  return Point();
}

void ObjectMovieData::DecodeCurrentFrame(bool force) {
  if (!decoder_ || !surface_)
    return;

  int frame_no = 0;
  if (decoder_->CheckNeedUpdate(current_time_, &frame_no, force)) {
    decoder_->DecodeFrame(frame_no, frame_, force);
    surface_->UpdateBGRA(frame_, true);
  }
}

unsigned int ObjectMovieData::FrameDurationMilliseconds() const {
  if (!decoder_)
    return 1;

  const int frame_count = decoder_->frame_count();
  if (frame_count > 0 && decoder_->total_time() > 0) {
    return static_cast<unsigned int>(
        std::max(1, decoder_->total_time() / frame_count));
  }

  const int usec_per_frame = decoder_->info().usec_per_frame;
  if (usec_per_frame > 0)
    return static_cast<unsigned int>(std::max(1, usec_per_frame / 1000));

  return 1;
}

void ObjectMovieData::RestoreCheckpoint(const SerializationContext& context) {
  backend_ = context.backend;
  clock_ = context.GetClock();
  if (!backend_)
    throw std::runtime_error("Movie requires graphics backend");
  current_time_ = 0;
  last_tick_ = Now();
  decoder_ = std::make_unique<OmvDecoder>(path_, loop_);
  frame_.resize(static_cast<std::size_t>(decoder_->width()) *
                decoder_->height() * 4);
  decoder_->DecodeFrame(0, frame_, true);
  surface_ = backend_->CreateSurfaceBGRA(decoder_->info().size, frame_, true);
}

BOOST_CLASS_EXPORT_IMPLEMENT(ObjectMovieData)
