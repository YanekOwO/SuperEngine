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

#include "libsiglus/save_manager.hpp"

#include "core/gameexe.hpp"
#include "core/selection_point.hpp"
#include "utilities/file.hpp"

#include <zlib.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace libsiglus {
namespace fs = std::filesystem;
namespace {

constexpr std::string_view kSignature = "RLVM-SIGLUS-SAVE\n";
constexpr std::uint64_t kMaximumBytes = 512 * 1024 * 1024;

std::string Truncate(std::string_view text, std::size_t characters) {
  std::size_t end = 0;
  while (end < text.size() && characters > 0) {
    ++end;
    while (end < text.size() &&
           (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
      ++end;
    --characters;
  }
  return std::string(text.substr(0, end));
}

SaveHeader CalendarNow() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  const std::tm calendar = *std::localtime(&time);
  return {.year = calendar.tm_year + 1900,
          .month = calendar.tm_mon + 1,
          .day = calendar.tm_mday,
          .weekday = calendar.tm_wday,
          .hour = calendar.tm_hour,
          .minute = calendar.tm_min,
          .second = calendar.tm_sec,
          .millisecond = static_cast<int>(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch())
                  .count() %
              1000)};
}

auto Timestamp(const SaveHeader& h) {
  return std::array{h.year,   h.month,  h.day,        h.hour,
                    h.minute, h.second, h.millisecond};
}

std::set<CheckpointId> Reachable(const SlotPayload& payload) {
  std::set<CheckpointId> result;
  std::function<void(CheckpointId)> visit = [&](CheckpointId id) {
    if (!id || !result.insert(id).second)
      return;
    const auto found = payload.blobs.find(id);
    if (found == payload.blobs.end())
      throw std::runtime_error("Checkpoint references missing local state");
    for (const auto& inner : found->second.inner)
      visit(inner.id);
    visit(found->second.stock.id);
  };
  visit(payload.active.id);
  for (const auto& checkpoint : payload.selection)
    visit(checkpoint.id);
  return result;
}

}  // namespace

SaveCounts SaveCounts::Clamped() const {
  return {.normal = std::clamp(normal, 0, 10000),
          .quick = std::clamp(quick, 0, 10000),
          .end = std::clamp(end, 0, 1),
          .inner = std::clamp(inner, 0, 100),
          .selection = std::clamp(selection, 1, 100)};
}

SaveCounts SaveCounts::Parse(Gameexe& gameexe) {
  return SaveCounts{.normal = gameexe("SAVE.CNT").ReadInt(10),
                    .quick = gameexe("QUICK_SAVE.CNT").ReadInt(3),
                    .end = gameexe("END_SAVE.CNT").ReadInt(0),
                    .inner = gameexe("INNER_SAVE.CNT").ReadInt(0),
                    .selection = gameexe("SEL_SAVE.CNT").ReadInt(1)}
      .Clamped();
}

SaveManager::SaveManager(fs::path directory, SaveCounts counts, Clock clock)
    : directory_(std::move(directory)),
      counts_(counts.Clamped()),
      clock_(clock ? std::move(clock) : CalendarNow),
      selection_(active_, counts_.selection),
      inner_(counts_.inner) {}

fs::path SaveManager::SlotPath(int slot) const {
  return directory_ / std::format("{:04}.sav", slot);
}

fs::path SaveManager::ThumbnailPath(int slot) const {
  return directory_ / std::format("{:04}.{}", slot, thumbnail_extension_);
}

void SaveManager::SetThumbnailExtension(std::string extension) {
  if (extension != "png" && extension != "bmp" && !extension.empty())
    throw std::invalid_argument("Save thumbnail format must be png or bmp");
  thumbnail_extension_ = std::move(extension);
}

bool SaveManager::ValidSlot(int slot) const {
  return slot >= 0 && slot < counts_.normal + counts_.quick + counts_.end;
}

bool SaveManager::ValidInner(int slot) const {
  return slot >= 0 && slot < inner_.size();
}

std::optional<int> SaveManager::NormalSlot(int slot) const {
  return ValidSlot(slot) ? std::optional(slot) : std::nullopt;
}

std::optional<int> SaveManager::QuickSlot(int slot) const {
  const auto absolute = static_cast<std::int64_t>(counts_.normal) + slot;
  return absolute >= 0 && absolute <= std::numeric_limits<int>::max() &&
                 ValidSlot(static_cast<int>(absolute))
             ? std::optional(static_cast<int>(absolute))
             : std::nullopt;
}

std::optional<int> SaveManager::EndSlot(int offset) const {
  const auto absolute =
      static_cast<std::int64_t>(counts_.normal) + counts_.quick + offset;
  return absolute >= 0 && absolute <= std::numeric_limits<int>::max() &&
                 ValidSlot(static_cast<int>(absolute))
             ? std::optional(static_cast<int>(absolute))
             : std::nullopt;
}

CheckpointId SaveManager::Capture(std::string runtime_state,
                                  std::string title,
                                  std::string append_dir,
                                  std::string append_name) {
  if (runtime_state.empty())
    throw std::invalid_argument("Cannot capture empty runtime state");
  if (next_id_ == std::numeric_limits<CheckpointId>::max())
    throw std::overflow_error("Checkpoint identity space exhausted");

  LocalStateBlob blob{.runtime_state = std::move(runtime_state),
                      .inner = inner_};
  const SelectionPoint::State selection = selection_.GetState();
  if (selection.stock)
    blob.stock = *selection.stock;
  for (const auto& point : selection.history)
    blob.selection_ids.push_back(point.id);
  const CheckpointRef checkpoint{.id = next_id_++,
                                 .title = std::move(title),
                                 .append_dir = std::move(append_dir),
                                 .append_name = std::move(append_name)};

  blobs_.emplace(checkpoint.id, std::move(blob));
  active_ = checkpoint;
  Prune();
  return active_.id;
}

void SaveManager::AppendMessage(std::string_view text) {
  AppendMessage(text, text);
}

void SaveManager::AppendMessage(std::string_view text,
                                std::string_view full_text) {
  if (!active_)
    return;
  active_.message = Truncate(active_.message + std::string(text), 256);
  active_.full_message =
      Truncate(active_.full_message + std::string(full_text), 256);
}

void SaveManager::ResetMessages(std::string_view full_message) {
  if (!active_)
    return;
  active_.message.clear();
  active_.full_message = Truncate(full_message, 256);
}

void SaveManager::ClearCheckpoint() {
  active_ = {};
  Prune();
}

const LocalStateBlob& SaveManager::Blob(CheckpointId id) const {
  return blobs_.at(id);
}

bool SaveManager::SaveInner(int slot) {
  if (!ValidInner(slot))
    return false;
  inner_[slot] = active_;
  Prune();
  return true;
}

bool SaveManager::ClearInner(int slot) {
  if (!ValidInner(slot))
    return false;
  inner_[slot] = {};
  Prune();
  return true;
}

bool SaveManager::CopyInner(int from, int to) {
  if (!ValidInner(from) || !ValidInner(to))
    return false;
  inner_[to] = inner_[from];
  Prune();
  return true;
}

bool SaveManager::HasInner(int slot) const {
  return ValidInner(slot) && static_cast<bool>(inner_[slot]);
}

CheckpointRef SaveManager::GetInner(int slot) const {
  return ValidInner(slot) ? inner_[slot] : CheckpointRef{};
}

std::optional<CheckpointRef> SaveManager::LatestSelection() const {
  if (!selection_.HasPoint())
    return std::nullopt;
  return selection_.history().back();
}

SlotPayload SaveManager::MakePayload(const SaveLocalExtra& extra) const {
  return MakePayloadFor(active_, extra);
}

SlotPayload SaveManager::MakePayloadFor(const CheckpointRef& checkpoint,
                                        const SaveLocalExtra& extra) const {
  if (!checkpoint)
    throw std::runtime_error("No savepoint is available");
  SlotPayload payload{.active = checkpoint, .extra = extra};
  const auto history = selection_.history();
  payload.selection.assign(history.begin(), history.end());
  payload.blobs = blobs_;
  const auto reachable = Reachable(payload);
  std::erase_if(payload.blobs, [&](const auto& pair) {
    return !reachable.contains(pair.first);
  });
  Validate(payload);
  return payload;
}

void SaveManager::Validate(const SlotPayload& payload) {
  if (!payload.active)
    throw std::runtime_error("Saved checkpoint is empty");
  if (payload.selection.size() > 100)
    throw std::runtime_error("Saved selection history exceeds its size limit");
  std::map<CheckpointId, int> marks;
  std::function<void(CheckpointId)> visit = [&](CheckpointId id) {
    if (!id)
      return;
    if (marks[id] == 1)
      throw std::runtime_error("Cyclic checkpoint references");
    if (marks[id] == 2)
      return;
    const auto found = payload.blobs.find(id);
    if (found == payload.blobs.end())
      throw std::runtime_error("Checkpoint references missing local state");
    const auto& blob = found->second;
    if (blob.runtime_state.empty() ||
        blob.runtime_state.size() > kMaximumBytes || blob.inner.size() > 100 ||
        blob.selection_ids.size() > 100)
      throw std::runtime_error("Invalid saved local state");
    marks[id] = 1;
    visit(blob.stock.id);
    for (const auto& inner : blob.inner)
      visit(inner.id);
    marks[id] = 2;
  };
  visit(payload.active.id);
  for (const auto& checkpoint : payload.selection) {
    if (!checkpoint)
      throw std::runtime_error("Empty checkpoint in selection history");
    visit(checkpoint.id);
  }
  for (const auto& [id, blob] : payload.blobs) {
    if (!id || id == std::numeric_limits<CheckpointId>::max())
      throw std::runtime_error("Invalid checkpoint identity");
    visit(id);
  }
}

void SaveManager::RestorePayload(const SlotPayload& payload) {
  Validate(payload);
  const auto& state = payload.blobs.at(payload.active.id);
  std::map<CheckpointId, CheckpointRef> available;
  for (const auto& checkpoint : payload.selection)
    available[checkpoint.id] = checkpoint;
  SelectionPoint::State points;
  if (state.stock)
    points.stock = state.stock;
  for (const auto id : state.selection_ids) {
    if (const auto found = available.find(id); found != available.end())
      points.history.push_back(found->second);
  }
  auto inner = state.inner;
  inner.resize(counts_.inner);
  auto blobs = payload.blobs;
  auto active = payload.active;
  CheckpointId next = next_id_;
  if (!blobs.empty())
    next = std::max(next, blobs.rbegin()->first + 1);
  selection_.RestoreState(std::move(points));
  inner_ = std::move(inner);
  blobs_ = std::move(blobs);
  active_ = std::move(active);
  next_id_ = next;
  Prune();
}

void SaveManager::Prune() {
  std::set<CheckpointId> reachable;
  auto dfs = [&](auto&& dfs, CheckpointId id) {
    if (!id || !reachable.insert(id).second)
      return;
    const auto found = blobs_.find(id);
    if (found == blobs_.end())
      throw std::runtime_error("Checkpoint references missing local state");
    dfs(dfs, found->second.stock.id);
    for (const auto& inner : found->second.inner)
      dfs(dfs, inner.id);
  };

  dfs(dfs, active_.id);
  for (const auto& inner : inner_)
    dfs(dfs, inner.id);
  const auto selection = selection_.GetState();
  if (selection.stock)
    dfs(dfs, selection.stock->id);
  for (const auto& checkpoint : selection.history)
    dfs(dfs, checkpoint.id);
  std::erase_if(blobs_, [&](const auto& pair) {
    return !reachable.contains(pair.first);
  });
}

void SaveManager::Reset() {
  selection_.Reset();
  active_ = {};
  inner_.assign(counts_.inner, {});
  blobs_.clear();
  error_.clear();
}

SaveManager::StoredFile SaveManager::ReadFile(int slot) const {
  if (!ValidSlot(slot))
    throw std::out_of_range("Invalid save slot");
  const auto bytes = LoadFileStr(SlotPath(slot));
  if (!bytes.starts_with(kSignature))
    throw std::runtime_error("Unsupported Siglus save format");
  std::istringstream in(bytes.substr(kSignature.size()), std::ios::binary);
  boost::archive::binary_iarchive archive(in);
  StoredFile file;
  archive >> file.header >> file.raw_size >> file.compressed;
  if (in.peek() != std::char_traits<char>::eof())
    throw std::runtime_error("Unexpected trailing save file data");
  return file;
}

std::string SaveManager::EncodeFile(const StoredFile& file) {
  std::ostringstream out(std::ios::binary);
  out.write(kSignature.data(), kSignature.size());
  boost::archive::binary_oarchive archive(out);
  archive << file.header << file.raw_size << file.compressed;
  return out.str();
}

bool SaveManager::SaveSlot(int slot,
                           const SaveLocalExtra& extra,
                           std::optional<std::string> thumbnail_bytes) {
  try {
    if (!ValidSlot(slot))
      throw std::out_of_range("Invalid save slot");

    // Release the copied payload and archive buffer before compression.
    auto pack = [&] {
      const SlotPayload payload = MakePayload(extra);
      std::ostringstream out(std::ios::binary);
      boost::archive::binary_oarchive archive(out);
      archive << payload;
      if (out.view().size() > kMaximumBytes)
        throw std::runtime_error("Save payload exceeds the size limit");
      return out.str();
    };
    std::string raw = pack();
    const std::size_t raw_size = raw.size();

    // Consume the raw buffer so it is released before assembling the save file.
    auto zlib_compress = [](std::string raw) {
      uLongf size = compressBound(raw.size());
      std::string compressed(size, '\0');
      if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &size,
                    reinterpret_cast<const Bytef*>(raw.data()), raw.size(),
                    Z_BEST_SPEED) != Z_OK)
        throw std::runtime_error("Could not compress save data");
      compressed.resize(size);
      return compressed;
    };
    std::string compressed = zlib_compress(std::move(raw));

    auto encode_file = [&]() {
      auto header = clock_();
      header.title = Truncate(active_.title, 255);
      header.message = Truncate(active_.message, 255);
      header.full_message = Truncate(active_.full_message, 255);
      header.append_dir = Truncate(active_.append_dir, 255);
      header.append_name = Truncate(active_.append_name, 255);
      header.comment.clear();
      header.values.fill(0);
      return EncodeFile({header, raw_size, std::move(compressed)});
    };
    FileTransaction transaction;
    transaction.Stage(SlotPath(slot), encode_file());
    if (!thumbnail_extension_.empty())
      transaction.Stage(ThumbnailPath(slot), std::move(thumbnail_bytes));
    transaction.Commit();
    error_.clear();

    return true;

  } catch (const std::exception& e) {
    error_ = e.what();
    return false;
  }
}

std::optional<SlotPayload> SaveManager::LoadSlot(int slot) const {
  try {
    const auto file = ReadFile(slot);
    if (!file.raw_size || file.raw_size > kMaximumBytes)
      throw std::runtime_error("Invalid save payload size");

    // z-lib uncompress
    std::string bytes(file.raw_size, '\0');
    uLongf size = bytes.size();
    if (uncompress(reinterpret_cast<Bytef*>(bytes.data()), &size,
                   reinterpret_cast<const Bytef*>(file.compressed.data()),
                   file.compressed.size()) != Z_OK ||
        size != file.raw_size)
      throw std::runtime_error("Invalid compressed save payload");

    // unpack
    std::istringstream in(bytes, std::ios::binary);
    boost::archive::binary_iarchive archive(in);
    SlotPayload payload;
    archive >> payload;
    if (in.peek() != std::char_traits<char>::eof())
      throw std::runtime_error("Unexpected trailing save data");

    // validate
    Validate(payload);
    error_.clear();
    return payload;

  } catch (const std::exception& e) {
    error_ = e.what();
    return std::nullopt;
  }
}

std::optional<SaveHeader> SaveManager::ReadHeader(int slot) const {
  try {
    auto header = ReadFile(slot).header;
    error_.clear();
    return header;
  } catch (const std::exception& e) {
    error_ = e.what();
    return std::nullopt;
  }
}

bool SaveManager::Exists(int slot) const {
  return ReadHeader(slot).has_value();
}

int SaveManager::Newest(bool quick) const {
  return Newest(0, quick ? counts_.quick : counts_.normal, quick);
}

int SaveManager::Newest(int start, int count, bool quick) const {
  const auto limit = quick ? counts_.quick : counts_.normal;
  const auto end = std::clamp<std::int64_t>(
      static_cast<std::int64_t>(start) + std::max(count, 0), 0, limit);
  int result = -1;
  std::optional<SaveHeader> latest;
  for (int i = std::max(0, start); i < end; ++i) {
    const auto header = ReadHeader(quick ? counts_.normal + i : i);
    if (header && (!latest || Timestamp(*header) >= Timestamp(*latest))) {
      latest = header;
      result = i;
    }
  }
  return result;
}

bool SaveManager::RewriteHeader(
    int slot,
    const std::function<void(SaveHeader&)>& update) {
  try {
    auto file = ReadFile(slot);
    update(file.header);
    FileTransaction transaction;
    transaction.Stage(SlotPath(slot), EncodeFile(file));
    transaction.Commit();
    error_.clear();
    return true;
  } catch (const std::exception& e) {
    error_ = e.what();
    return false;
  }
}

bool SaveManager::SetComment(int slot, std::string_view text) {
  return RewriteHeader(
      slot, [&](auto& header) { header.comment = Truncate(text, 255); });
}

bool SaveManager::SetValues(int slot, std::span<const int> values) {
  return RewriteHeader(slot, [&](auto& header) {
    std::copy_n(values.begin(), std::min(values.size(), header.values.size()),
                header.values.begin());
  });
}

bool SaveManager::CopySlot(int from, int to) {
  try {
    if (from == to || !ValidSlot(from) || !ValidSlot(to))
      throw std::out_of_range("Invalid save copy slots");
    ReadFile(from);
    FileTransaction transaction;
    transaction.Stage(SlotPath(to), LoadFileStr(SlotPath(from)));
    if (!thumbnail_extension_.empty())
      transaction.Stage(ThumbnailPath(to),
                        fs::exists(ThumbnailPath(from))
                            ? std::optional(LoadFileStr(ThumbnailPath(from)))
                            : std::nullopt);
    transaction.Commit();
    error_.clear();
    return true;
  } catch (const std::exception& e) {
    error_ = e.what();
    return false;
  }
}

bool SaveManager::SwapSlots(int first, int second) {
  try {
    if (first == second || !ValidSlot(first) || !ValidSlot(second))
      throw std::out_of_range("Invalid save swap slots");
    const bool a = fs::exists(SlotPath(first));
    const bool b = fs::exists(SlotPath(second));
    if (!a && !b)
      throw std::runtime_error("Neither save slot exists");
    if (a)
      ReadFile(first);
    if (b)
      ReadFile(second);
    FileTransaction transaction;
    transaction.Stage(
        SlotPath(first),
        b ? std::optional(LoadFileStr(SlotPath(second))) : std::nullopt);
    transaction.Stage(
        SlotPath(second),
        a ? std::optional(LoadFileStr(SlotPath(first))) : std::nullopt);
    if (!thumbnail_extension_.empty()) {
      const auto a_thumb =
          a && fs::exists(ThumbnailPath(first))
              ? std::optional(LoadFileStr(ThumbnailPath(first)))
              : std::nullopt;
      const auto b_thumb =
          b && fs::exists(ThumbnailPath(second))
              ? std::optional(LoadFileStr(ThumbnailPath(second)))
              : std::nullopt;
      transaction.Stage(ThumbnailPath(first), b_thumb);
      transaction.Stage(ThumbnailPath(second), a_thumb);
    }
    transaction.Commit();
    error_.clear();
    return true;
  } catch (const std::exception& e) {
    error_ = e.what();
    return false;
  }
}

bool SaveManager::DeleteSlot(int slot) {
  try {
    ReadFile(slot);
    FileTransaction transaction;
    transaction.Stage(SlotPath(slot), std::nullopt);
    if (!thumbnail_extension_.empty())
      transaction.Stage(ThumbnailPath(slot), std::nullopt);
    transaction.Commit();
    error_.clear();
    return true;
  } catch (const std::exception& e) {
    error_ = e.what();
    return false;
  }
}

}  // namespace libsiglus
