// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
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
// -----------------------------------------------------------------------

#include "systems/sound_system.hpp"
#include "headless_resource_host.hpp"
#include "recording_audio_test_support.hpp"
#include "serialization_test_helpers.hpp"
#include "utilities/file.hpp"

#include <fstream>

namespace {
using Status = SoundSystem::PlaybackStatus;

class SoundArchiveHost {
 public:
  SoundArchiveHost() {
    std::ofstream(directory.dir() / "tone.wav", std::ios::binary)
        << ResourceWave();
    host.scanner->IndexDirectory(directory.dir());
    host.gameexe.parseLine("BGM.000 = \"track\",\"tone\",0,1024,0");
    auto implementation = std::make_unique<ResourceAudioBackend>();
    backend = implementation.get();
    sound =
        std::make_unique<SoundSystem>(*host.system, std::move(implementation));
  }
  std::string Save(serialization_test::Format format) {
    return serialization_test::Save(*sound, format);
  }
  void Load(const std::string& bytes,
            serialization_test::Format format,
            SerializationContext context = {}) {
    serialization_test::Load(bytes, *sound, format, std::move(context));
  }
  void RoundTrip(serialization_test::Format format) {
    Load(Save(format), format);
  }

  HeadlessResourceHost host;
  ScopedTemporary directory;
  ResourceAudioBackend* backend;
  std::unique_ptr<SoundSystem> sound;
};

class SoundSystemArchiveTest : public ::testing::Test {
 protected:
  void Each(auto run) {
    serialization_test::ForEachFormat([&](auto format) {
      SoundArchiveHost audio;
      run(audio, format);
    });
  }
};

TEST_F(SoundSystemArchiveTest, LoopingBgmRestartsFromBeginning) {
  // Save a loop after advancing its decoder; loading restarts the named track.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    auto original = audio.backend->GetBgm();
    original->LoadPCM(400);
    ASSERT_GT(original->GetCurrentTime(), 0);
    audio.RoundTrip(format);
    auto restored = audio.backend->GetBgm();
    ASSERT_NE(restored, nullptr);
    EXPECT_NE(restored, original);
    EXPECT_EQ(restored->GetCurrentTime(), 0);
    EXPECT_TRUE(restored->IsLoopingEnabled());
    EXPECT_TRUE(audio.sound->BgmPlaying());
  });
}

TEST_F(SoundSystemArchiveTest, PreparedBgmRemainsPausedUntilResumed) {
  // A prepared one-shot is persistent and remains silent until resumed.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPrepare("track", false);
    audio.RoundTrip(format);
    EXPECT_EQ(audio.sound->GetBgmStatus(), Status::Paused);
    EXPECT_FALSE(audio.sound->BgmLooping());
    audio.sound->BgmUnPause();
    EXPECT_TRUE(audio.sound->BgmPlaying());
    EXPECT_EQ(audio.backend->GetBgm()->GetCurrentTime(), 0);
  });
}

TEST_F(SoundSystemArchiveTest, PausedBgmRemainsPaused) {
  // Pause a playing track before saving; the reconstructed player stays paused.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    audio.sound->BgmPause();
    audio.RoundTrip(format);
    EXPECT_EQ(audio.sound->GetBgmStatus(), Status::Paused);
  });
}

TEST_F(SoundSystemArchiveTest, LoopingPcmRestartsFromBeginning) {
  // A named channel loop retains routing and loop settings but loses its
  // playhead.
  Each([](auto& audio, auto format) {
    audio.sound->WavPlay("tone", true, 2);
    auto original = audio.backend->ChannelPlayer(2);
    original->LoadPCM(400);
    ASSERT_GT(original->GetCurrentTime(), 0);
    audio.RoundTrip(format);
    auto restored = audio.backend->ChannelPlayer(2);
    ASSERT_NE(restored, nullptr);
    EXPECT_NE(restored, original);
    EXPECT_EQ(restored->GetCurrentTime(), 0);
    EXPECT_TRUE(restored->IsPlaying());
    EXPECT_TRUE(restored->IsLoopingEnabled());
  });
}

TEST_F(SoundSystemArchiveTest, PreparedPcmRemainsSilentUntilResumed) {
  // A prepared one-shot survives loading and starts only when explicitly
  // resumed.
  Each([](auto& audio, auto format) {
    audio.sound->WavPrepare("tone", false, 2);
    audio.RoundTrip(format);
    EXPECT_FALSE(audio.backend->IsPlaying(2));
    audio.sound->WavResume(2);
    ASSERT_NE(audio.backend->ChannelPlayer(2), nullptr);
    EXPECT_TRUE(audio.sound->WavPlaying(2));
    audio.backend->ChannelPlayer(2)->LoadPCM(4096);
    EXPECT_FALSE(audio.backend->ChannelPlayer(2)->IsPlaying());
  });
}

TEST_F(SoundSystemArchiveTest, PreparedLoopRetainsLoopSetting) {
  // A prepared loop remains resumable as a loop after loading.
  Each([](auto& audio, auto format) {
    audio.sound->WavPrepare("tone", true, 2);
    audio.RoundTrip(format);
    audio.sound->WavResume(2);
    ASSERT_NE(audio.backend->ChannelPlayer(2), nullptr);
    EXPECT_TRUE(audio.backend->ChannelPlayer(2)->IsLoopingEnabled());
  });
}

TEST_F(SoundSystemArchiveTest, PausedLoopingPcmRemainsPaused) {
  // A paused loop retains its live intent and can restart after loading.
  Each([](auto& audio, auto format) {
    audio.sound->WavPlay("tone", true, 2);
    audio.sound->WavPause(2);
    audio.RoundTrip(format);
    EXPECT_EQ(audio.sound->GetWavStatus(2), Status::Paused);
    EXPECT_FALSE(audio.backend->IsPlaying(2));
    audio.sound->WavResume(2);
    EXPECT_TRUE(audio.sound->WavPlaying(2));
  });
}

TEST_F(SoundSystemArchiveTest, OneShotPcmRetainsOnlyChannelVolume) {
  // Saving a playing one-shot drops its name and playback while retaining
  // volume.
  Each([](auto& audio, auto format) {
    audio.sound->SetChannelVolume(2, 93);
    const auto idle = audio.Save(format);
    audio.sound->WavPlay("tone", false, 2);
    EXPECT_EQ(audio.Save(format), idle);
    audio.RoundTrip(format);
    audio.sound->WavResume(2);
    EXPECT_EQ(audio.sound->GetWavStatus(2), Status::Stopped);
    EXPECT_EQ(audio.sound->GetChannelVolume(2), 93);
  });
}

TEST_F(SoundSystemArchiveTest, PausedOneShotPcmRetainsOnlyChannelVolume) {
  // Pausing an ordinary one-shot must not make it persistent or retain its
  // name.
  Each([](auto& audio, auto format) {
    audio.sound->SetChannelVolume(2, 93);
    const auto idle = audio.Save(format);
    audio.sound->WavPlay("tone", false, 2);
    audio.sound->WavPause(2);
    ASSERT_EQ(audio.sound->GetWavStatus(2), Status::Paused);
    EXPECT_EQ(audio.Save(format), idle);
    audio.RoundTrip(format);
    audio.sound->WavResume(2);
    EXPECT_EQ(audio.sound->GetWavStatus(2), Status::Stopped);
    EXPECT_EQ(audio.sound->GetChannelVolume(2), 93);
  });
}

TEST_F(SoundSystemArchiveTest, OneShotBgmDoesNotRestart) {
  // A playing one-shot BGM is transient, even while its decoder is active.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", false);
    audio.RoundTrip(format);
    EXPECT_FALSE(audio.sound->BgmPlaying());
    EXPECT_TRUE(audio.sound->GetBgmName().empty());
  });
}

TEST_F(SoundSystemArchiveTest, FadingBgmDoesNotRestart) {
  // Saving during a loop's fade-out omits playback without changing live state.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    audio.sound->BgmFadeOut(100);
    const auto bytes = audio.Save(format);
    EXPECT_TRUE(audio.sound->BgmFadingOut());
    audio.Load(bytes, format);
    EXPECT_FALSE(audio.sound->BgmPlaying());
  });
}

TEST_F(SoundSystemArchiveTest, FadingPcmDoesNotRestart) {
  // A channel being stopped by a fade is absent from the restored playback.
  Each([](auto& audio, auto format) {
    audio.sound->WavPlay("tone", true, 2);
    audio.sound->WavFadeOut(2, 100);
    audio.RoundTrip(format);
    audio.sound->WavResume(2);
    EXPECT_FALSE(audio.sound->WavPlaying(2));
  });
}

TEST_F(SoundSystemArchiveTest, FinishedPcmDoesNotRestart) {
  // Backend completion overrides stale play intent when a sound is saved.
  Each([](auto& audio, auto format) {
    audio.sound->WavPlay("tone", true, 2);
    audio.backend->ChannelPlayer(2)->Terminate();
    audio.RoundTrip(format);
    audio.sound->WavResume(2);
    EXPECT_FALSE(audio.sound->WavPlaying(2));
  });
}

TEST_F(SoundSystemArchiveTest, FinishedBgmDoesNotRestart) {
  // A completed backend player cannot be revived by its retained logical name.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    audio.backend->GetBgm()->Terminate();
    audio.RoundTrip(format);
    EXPECT_FALSE(audio.sound->BgmPlaying());
  });
}

TEST_F(SoundSystemArchiveTest, RestoresChannelVolumeTargetImmediately) {
  // An unfinished channel volume ramp loads at its target with no resumed ramp.
  Each([](auto& audio, auto format) {
    audio.sound->SetChannelVolume(2, 80, 100);
    audio.RoundTrip(format);
    EXPECT_EQ(audio.sound->GetChannelVolume(2), 80);
    EXPECT_EQ(audio.backend->ChannelVolume(2), 40);
  });
}

TEST_F(SoundSystemArchiveTest, RestoresBgmVolumeTargetImmediately) {
  // Music loads with the saved script target applied to its fresh player.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    audio.sound->SetBgmVolumeScript(80, 100);
    audio.RoundTrip(format);
    EXPECT_EQ(audio.sound->GetBgmVolumeScript(), 80);
    EXPECT_FLOAT_EQ(audio.backend->GetBgm()->GetVolume(), 80.0f / 255);
  });
}

TEST_F(SoundSystemArchiveTest, RestoresSeVolumeTargetImmediately) {
  // SE's script target is retained even though its transient playback is
  // dropped.
  Each([](auto& audio, auto format) {
    audio.sound->SetSeVolumeScript(72, 100);
    audio.RoundTrip(format);
    audio.sound->PlaySeByFileName("tone");
    EXPECT_EQ(audio.sound->GetSeVolumeScript(), 72);
    EXPECT_EQ(audio.backend->ChannelVolume(0), 36);
  });
}

TEST_F(SoundSystemArchiveTest, RestoresMovieVolumeConfiguration) {
  // Movie playback is transient but its script gain still participates in
  // saves.
  Each([](auto& audio, auto format) {
    audio.sound->PlayMovieAudio(audio.directory.dir() / "tone.wav", 63);
    const auto bytes = audio.Save(format);
    audio.sound->PlayMovieAudio(audio.directory.dir() / "tone.wav", 200);
    audio.Load(bytes, format);
    EXPECT_FALSE(audio.sound->MovieAudioPlaying());
    EXPECT_EQ(audio.Save(format), bytes);
  });
}

TEST_F(SoundSystemArchiveTest, KeepsDestinationUserPreferences) {
  // Loading script gains must keep the receiving user's volume preference.
  Each([](auto& audio, auto format) {
    audio.sound->SetChannelVolume(2, 200);
    const auto bytes = audio.Save(format);
    audio.sound->SetUserVolume(SoundCategory::Pcm, 128);
    audio.Load(bytes, format);
    EXPECT_EQ(audio.sound->GetUserVolume(SoundCategory::Pcm), 128);
    EXPECT_EQ(audio.backend->ChannelVolume(2), 50);
  });
}

TEST_F(SoundSystemArchiveTest, DropsSeparateOneShotPool) {
  // Loading stops all pool voices, including voices started after the save.
  Each([](auto& audio, auto format) {
    audio.sound->PlayPcm("tone");
    const auto bytes = audio.Save(format);
    audio.sound->PlayPcm("tone");
    audio.Load(bytes, format);
    EXPECT_FALSE(audio.backend->IsPlaying(NUM_TOTAL_CHANNELS));
    EXPECT_FALSE(audio.backend->IsPlaying(NUM_TOTAL_CHANNELS + 1));
  });
}

TEST_F(SoundSystemArchiveTest, DropsSoundEffects) {
  // Ordinary SE playback does not enter the sound archive.
  Each([](auto& audio, auto format) {
    audio.sound->PlaySeByFileName("tone");
    audio.RoundTrip(format);
    EXPECT_EQ(audio.sound->CheckSe(), SoundSystem::kSeFree);
  });
}

TEST_F(SoundSystemArchiveTest, ResolvesNamesUsingDestinationAssets) {
  // Load the same logical names in another root; no source paths enter the
  // save.
  Each([](auto& source, auto format) {
    source.sound->BgmPlay("track", true);
    source.sound->WavPlay("tone", true, 2);
    const auto bytes = source.Save(format);
    std::filesystem::remove(source.directory.dir() / "tone.wav");
    SoundArchiveHost target;
    target.Load(bytes, format);
    EXPECT_TRUE(target.sound->BgmPlaying());
    EXPECT_TRUE(target.sound->WavPlaying(2));
  });
}

TEST_F(SoundSystemArchiveTest, MissingAssetPreservesLivePlayback) {
  // Missing prepared audio rejects the whole candidate before stopping live
  // BGM.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    auto original = audio.backend->GetBgm();
    audio.sound->WavPrepare("missing", true, 2);
    const auto bytes = audio.Save(format);
    EXPECT_THROW(audio.Load(bytes, format), std::exception);
    EXPECT_EQ(audio.backend->GetBgm(), original);
    EXPECT_TRUE(original->IsPlaying());
  });
}

TEST_F(SoundSystemArchiveTest, TruncatedArchivePreservesLiveState) {
  // A partial payload must not replace live volumes or playback.
  Each([](auto& audio, auto format) {
    audio.sound->SetChannelVolume(2, 90);
    const auto bytes = audio.Save(format);
    audio.sound->SetChannelVolume(2, 150);
    audio.sound->BgmPlay("track", true);
    auto original = audio.backend->GetBgm();
    EXPECT_THROW(audio.Load(bytes.substr(0, bytes.size() / 2), format),
                 std::exception);
    EXPECT_EQ(audio.sound->GetChannelVolume(2), 150);
    EXPECT_EQ(audio.backend->GetBgm(), original);
  });
}

TEST_F(SoundSystemArchiveTest, DeferredLoadLeavesLivePlaybackUntilCommit) {
  // A caller can stage reconstruction without installing any player or gain.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    audio.sound->SetChannelVolume(2, 90);
    const auto bytes = audio.Save(format);
    audio.sound->SetChannelVolume(2, 150);
    auto original = audio.backend->GetBgm();
    SerializationContext::Commit commit;
    audio.Load(bytes, format, {.defer_commit = [&](auto install) {
                 commit = std::move(install);
               }});
    ASSERT_TRUE(commit);
    EXPECT_EQ(audio.backend->GetBgm(), original);
    EXPECT_EQ(audio.sound->GetChannelVolume(2), 150);
    commit();
    EXPECT_NE(audio.backend->GetBgm(), original);
    EXPECT_EQ(audio.sound->GetChannelVolume(2), 90);
  });
}

TEST_F(SoundSystemArchiveTest, InvalidChannelLeavesPlaybackUntouched) {
  // An out-of-range prepare fails before touching a valid playing channel.
  Each([](auto& audio, auto) {
    audio.sound->WavPlay("tone", true, 2);
    auto original = audio.backend->ChannelPlayer(2);
    EXPECT_THROW(audio.sound->WavPrepare("tone", true, NUM_TOTAL_CHANNELS),
                 std::out_of_range);
    EXPECT_EQ(audio.backend->ChannelPlayer(2), original);
  });
}

TEST_F(SoundSystemArchiveTest, SeReplacementClearsPreparedPcm) {
  // Replacing an explicit PCM channel with SE must not preserve the old loop.
  Each([](auto& audio, auto format) {
    audio.sound->WavPlay("tone", true, 0);
    audio.backend->ChannelPlayer(0)->Terminate();
    audio.sound->PlaySeByFileName("tone");
    audio.RoundTrip(format);
    audio.sound->WavResume(0);
    EXPECT_FALSE(audio.sound->WavPlaying(0));
  });
}

TEST_F(SoundSystemArchiveTest, BgmLoopSettingTracksPlaybackChanges) {
  // Replaying a logical name with a different loop setting updates the
  // serializer.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    audio.sound->BgmPlay("track", false);
    EXPECT_FALSE(audio.sound->BgmLooping());
    audio.backend->GetBgm()->LoadPCM(4096);
    EXPECT_FALSE(audio.backend->GetBgm()->IsPlaying());
    audio.RoundTrip(format);
    EXPECT_FALSE(audio.sound->BgmPlaying());
  });
}

// A raw payload permits exercising validation without exposing mutable state.
struct SavedPlayback {
  std::string name;
  bool loop = false;
  bool prepared = false;
  int status = 0;
  void serialize(auto& ar, unsigned int) {
    ar & name & loop & prepared & status;
  }
};

std::string InvalidSoundPayload(serialization_test::Format format,
                                SavedPlayback music,
                                int volume) {
  return serialization_test::Write(format, [&](auto& ar) {
    std::array<SavedPlayback, NUM_TOTAL_CHANNELS> pcm;
    std::array<int, NUM_TOTAL_CHANNELS> channels;
    channels.fill(255);
    int se = 255, movie = 255;
    ar & music & pcm & channels & volume & se & movie;
  });
}

TEST_F(SoundSystemArchiveTest, RejectsInvalidPlaybackStatus) {
  // An invalid saved status fails validation without replacing live playback.
  Each([](auto& audio, auto format) {
    audio.sound->BgmPlay("track", true);
    auto original = audio.backend->GetBgm();
    const auto bytes = InvalidSoundPayload(
        format, {.name = "track", .loop = true, .status = 99}, 255);
    EXPECT_THROW(
        serialization_test::Read(bytes, format,
                                 [&](auto& ar) {
                                   boost::serialization::access::member_load(
                                       ar, *audio.sound, 0);
                                 }),
        std::runtime_error);
    EXPECT_EQ(audio.backend->GetBgm(), original);
  });
}

TEST_F(SoundSystemArchiveTest, RejectsPlaybackWithoutAssetName) {
  // A playing record without a logical asset name cannot replace live sound.
  Each([](auto& audio, auto format) {
    audio.sound->SetChannelVolume(2, 90);
    const auto bytes =
        InvalidSoundPayload(format, {.loop = true, .status = 1}, 255);
    EXPECT_THROW(
        serialization_test::Read(bytes, format,
                                 [&](auto& ar) {
                                   boost::serialization::access::member_load(
                                       ar, *audio.sound, 0);
                                 }),
        std::runtime_error);
    EXPECT_EQ(audio.sound->GetChannelVolume(2), 90);
  });
}

TEST_F(SoundSystemArchiveTest, RejectsInvalidVolume) {
  // Out-of-range serialized gains are rejected before changing live
  // configuration.
  Each([](auto& audio, auto format) {
    audio.sound->SetBgmVolumeScript(90, 0);
    const auto bytes = InvalidSoundPayload(format, {}, 256);
    EXPECT_THROW(
        serialization_test::Read(bytes, format,
                                 [&](auto& ar) {
                                   boost::serialization::access::member_load(
                                       ar, *audio.sound, 0);
                                 }),
        std::runtime_error);
    EXPECT_EQ(audio.sound->GetBgmVolumeScript(), 90);
  });
}

}  // namespace
