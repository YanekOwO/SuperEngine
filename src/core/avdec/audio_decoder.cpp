// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "core/avdec/audio_decoder.hpp"

#include "core/avdec/ffmpeg.hpp"
#include "core/avdec/nwa.hpp"
#include "core/avdec/ogg.hpp"
#include "core/avdec/wav.hpp"

#include <cctype>

// -----------------------------------------------------------------------
// class ADecoderFactory
// -----------------------------------------------------------------------

using decoder_constructor_t = ADecoderFactory::decoder_constructor_t;

static decoder_constructor_t ogg = [](std::string_view data) {
  return std::make_shared<OggDecoder>(data);
};

static decoder_constructor_t wav = [](std::string_view data) {
  return std::make_shared<WavDecoder>(data);
};

static decoder_constructor_t nwa = [](std::string_view data) {
  return std::make_shared<NwaDecoder>(data);
};

static decoder_constructor_t owp = [](std::string_view data) {
  static constexpr uint8_t owp_xorkey = 0x39;
  return std::make_shared<OggDecoder>(data, owp_xorkey);
};

static decoder_constructor_t ffmpeg_audio = [](std::string_view data) {
  return std::make_shared<FfmpegAudioDecoder>(data);
};

std::unordered_map<std::string, decoder_constructor_t>
    ADecoderFactory::default_decoder_map_ = {
        {"ogg", ogg},           {".ogg", ogg},          {"nwa", nwa},
        {".nwa", nwa},          {"wav", wav},           {".wav", wav},
        {"owp", owp},           {".owp", owp},          {"wmv", ffmpeg_audio},
        {".wmv", ffmpeg_audio}, {"asf", ffmpeg_audio},  {".asf", ffmpeg_audio},
        {"avi", ffmpeg_audio},  {".avi", ffmpeg_audio}, {"mpg", ffmpeg_audio},
        {".mpg", ffmpeg_audio}, {"mpeg", ffmpeg_audio}, {".mpeg", ffmpeg_audio},
        {"mp3", ffmpeg_audio},  {".mp3", ffmpeg_audio}};

ADecoderFactory::ADecoderFactory() : decoder_map_(&default_decoder_map_) {}

decoder_t ADecoderFactory::Create(std::string_view data,
                                  std::optional<std::string> format_hint) {
  using std::string_literals::operator""s;
  std::string format = format_hint.value_or("unknown"s);
  std::transform(format.begin(), format.end(), format.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  try {
    const auto& fn = decoder_map_->at(format);
    return fn(data);
  } catch (...) {
    // format wrong or unknown, fallback to a chain of responsibility factory
  }

  for (const auto& [key, fn] : *decoder_map_) {
    try {
      return fn(data);
    } catch (...) {
      // Handle cases where a specific decoder fails
    }
  }

  throw std::runtime_error("No Decoder found for format: " +
                           std::string(format));
}

// -----------------------------------------------------------------------
// class AudioDecoder
// -----------------------------------------------------------------------

ADecoderFactory AudioDecoder::factory_ = ADecoderFactory();

AudioDecoder::AudioDecoder(FilePos fp, const std::string& format)
    : dataholder_(fp), decoderimpl_(factory_.Create(fp.Read(), format)) {}

AudioDecoder::AudioDecoder(std::shared_ptr<IAudioDecoder> dec)
    : dataholder_(), decoderimpl_(dec) {}

AudioDecoder::AudioDecoder(std::filesystem::path filepath,
                           const std::string& format) {
  auto file = std::make_shared<MappedFile>(filepath);
  dataholder_ = file;
  const std::string format_hint =
      format.empty() ? filepath.extension().string() : format;
  decoderimpl_ = factory_.Create(file->Read(), format_hint);
}

AudioDecoder::AudioDecoder(std::string filestr, const std::string& format)
    : AudioDecoder(std::filesystem::path(filestr), format) {}

AudioData AudioDecoder::DecodeAll() const { return decoderimpl_->DecodeAll(); }

AudioData AudioDecoder::DecodeNext() const {
  return decoderimpl_->DecodeNext();
}

bool AudioDecoder::HasNext() const { return decoderimpl_->HasNext(); }

void AudioDecoder::Rewind() const { decoderimpl_->Seek(0, SEEKDIR::BEG); }

AVSpec AudioDecoder::GetSpec() const { return decoderimpl_->GetSpec(); }

SEEK_RESULT AudioDecoder::Seek(long long offset, SEEKDIR whence) const {
  return decoderimpl_->Seek(offset, whence);
}

long long AudioDecoder::Tell() const { return decoderimpl_->Tell(); }
