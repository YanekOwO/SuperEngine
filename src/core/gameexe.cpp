// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of libreallive, a dependency of RLVM.
//
// -----------------------------------------------------------------------
//
// Copyright (c) 2006, 2007 Peter Jolly
// Copyright (c) 2007 Elliot Glaysher
// Copyright (c) 2024 Serina Sakurai
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------

#include "core/gameexe.hpp"

#include "utilities/string_utilities.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

std::string GexeErr::GetDebugString() const {
  std::ostringstream oss;
  if (!key.empty())
    oss << "Gameexe[" << key << "]: ";
  oss << message;
  if (line)
    oss << " (line " << *line << ")";
  return oss.str();
}

namespace {
[[nodiscard]] static inline GexeErr MakeError(std::string_view key,
                                              std::string message,
                                              std::optional<std::size_t> line) {
  return GexeErr{std::string(key), std::move(message), line};
}

[[noreturn]] static void ThrowGameexeError(const GexeErr& error) {
  throw std::runtime_error(error.GetDebugString());
}
}  // namespace

// -----------------------------------------------------------------------

Gameexe::Gameexe() = default;

GexeExpected<Gameexe> Gameexe::FromFile(const std::filesystem::path& path) {
  Gameexe gameexe;
  auto result = gameexe.LoadFromFile(path);
  if (!result)
    return make_unexpected(result.error());
  return GexeExpected<Gameexe>(std::move(gameexe));
}

GexeExpected<void> Gameexe::LoadFromFile(const std::filesystem::path& path) {
  std::ifstream ifs(path);
  if (!ifs)
    return make_unexpected(MakeError(
        {}, std::string("Failed to open Gameexe file: ") + path.string(), {}));

  entries_.clear();
  next_string_id_ = 0;

  std::string line;
  std::size_t line_number = 0;
  while (std::getline(ifs, line)) {
    ++line_number;
    auto result = ParseLine(line, line_number);
    if (!result)
      return result;
  }

  return {};
}

GexeExpected<void> Gameexe::ParseLine(std::string_view line,
                                      std::size_t line_number) {
  std::string_view trimmed = trim_sv(line);
  if (trimmed.empty() || trimmed.front() == ';')
    return {};

  auto equal_pos = trimmed.find('=');
  if (equal_pos == std::string_view::npos)
    return make_unexpected(
        MakeError({}, "Missing '=' delimiter in Gameexe line", line_number));

  std::string_view lhs = trim_sv(trimmed.substr(0, equal_pos));
  std::string_view rhs = trim_sv(trimmed.substr(equal_pos + 1));

  if (lhs.empty())
    return make_unexpected(
        MakeError({}, "Empty key in Gameexe line", line_number));

  std::string_view key;
  GexeExpected<std::vector<GexeVal>> values;

  key = lhs;
  if (key.front() == '#')
    key = trim_sv(key.substr(1));
  values = ParseValues(key, rhs, line_number);

  if (!values)
    return make_unexpected(values.error());

  auto parsed_values = std::move(values.value());
  entries_.emplace(std::move(key), std::move(parsed_values));
  return {};
}

GexeExpected<std::vector<GexeVal>> Gameexe::ParseValues(
    std::string_view key,
    std::string_view value,
    std::size_t line_number) {
  std::vector<GexeVal> parsed;
  std::string current;
  std::size_t pos = 0;
  bool expect_value_for_comma = true;

  auto push_string = [&](std::string str) {
    parsed.push_back(MakeStringValue(std::move(str)));
    expect_value_for_comma = false;
  };

  auto flush_current = [&]() -> bool {
    std::string_view view(current);
    std::string_view trimmed = trim_sv(view);
    if (trimmed.empty() || trimmed == "-") {
      current.clear();
      return false;
    }

    int as_int;
    if (parse_int(trimmed, as_int)) {
      parsed.push_back(MakeIntValue(as_int));
      expect_value_for_comma = false;
    } else {
      push_string(std::string(trimmed));
    }
    current.clear();
    return true;
  };

  auto is_numeric_token = [&](std::string_view token) {
    if (token.empty())
      return false;
    if (token.front() == '+' || token.front() == '-') {
      token.remove_prefix(1);
      if (token.empty())
        return false;
    }
    return std::all_of(token.begin(), token.end(), [](char c) {
      return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
  };

  auto find_next_non_space =
      [&](std::size_t from) -> std::optional<std::size_t> {
    for (std::size_t i = from + 1; i < value.size(); ++i) {
      if (!std::isspace(static_cast<unsigned char>(value[i])))
        return i;
    }
    return std::nullopt;
  };

  auto next_sequence_starts_number = [&](std::size_t from) {
    auto idx = find_next_non_space(from);
    if (!idx)
      return false;
    char c = value[*idx];
    if (std::isdigit(static_cast<unsigned char>(c)))
      return true;
    if (c == '+' || c == '-') {
      if (*idx + 1 >= value.size() ||
          !std::isdigit(static_cast<unsigned char>(value[*idx + 1])))
        return false;
      return true;
    }
    return false;
  };

  auto next_non_space_is_digit = [&](std::size_t from) {
    auto idx = find_next_non_space(from);
    if (!idx)
      return false;
    return std::isdigit(static_cast<unsigned char>(value[*idx])) != 0;
  };

  auto should_split_numeric = [&](char delimiter) {
    std::string_view trimmed = trim_sv(std::string_view(current));
    if (!is_numeric_token(trimmed))
      return false;
    if (delimiter == '-' && !trimmed.empty() && trimmed.front() == '-')
      return false;
    if (delimiter == '-' || delimiter == ':')
      return next_non_space_is_digit(pos);
    return false;
  };

  while (pos <= value.size()) {
    if (pos == value.size()) {
      flush_current();
      break;
    }

    char ch = value[pos];

    if (ch == '"') {
      ++pos;
      std::string quoted;
      bool closed = false;
      while (pos < value.size()) {
        char qc = value[pos];
        if (qc == '"') {
          closed = true;
          ++pos;
          break;
        }
        if (qc == '\\' && pos + 1 < value.size()) {
          char next = value[pos + 1];
          if (next == '"' || next == '\\') {
            quoted.push_back(next);
            pos += 2;
            continue;
          }
        }
        quoted.push_back(qc);
        ++pos;
      }

      if (!closed) {
        return make_unexpected(
            MakeError(key, "Unterminated quoted string", line_number));
      }

      flush_current();
      push_string(std::move(quoted));
      continue;
    }

    if (ch == ',') {
      bool emitted = flush_current();
      if (!emitted && expect_value_for_comma)
        push_string(std::string());
      expect_value_for_comma = true;
      ++pos;
      continue;
    }

    if (ch == '=') {
      flush_current();
      expect_value_for_comma = true;
      ++pos;
      continue;
    }

    if (ch == ';') {
      flush_current();
      break;
    }

    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (current.empty()) {
        ++pos;
        continue;
      }

      std::string_view trimmed = trim_sv(std::string_view(current));
      if (is_numeric_token(trimmed) && next_sequence_starts_number(pos)) {
        flush_current();
      } else {
        current.push_back(ch);
      }
      ++pos;
      continue;
    }

    if ((ch == '-' || ch == ':') && should_split_numeric(ch)) {
      flush_current();
      expect_value_for_comma = true;
      ++pos;
      continue;
    }

    current.push_back(ch);
    ++pos;
  }

  return GexeExpected<std::vector<GexeVal>>(std::move(parsed));
}

const std::vector<GexeVal>* Gameexe::Find(std::string_view key) const {
  auto it = entries_.find(std::string(key));
  if (it == entries_.end())
    return nullptr;
  return &it->second;
}

std::vector<GexeVal>* Gameexe::Find(std::string_view key) {
  auto it = entries_.find(std::string(key));
  if (it == entries_.end())
    return nullptr;
  return &it->second;
}

std::vector<GexeVal>& Gameexe::Ensure(std::string key) {
  entries_.erase(key);
  auto inserted = entries_.emplace(std::move(key), std::vector<GexeVal>());
  return inserted->second;
}

GexeVal Gameexe::MakeIntValue(int value) const { return GexeVal(value); }

GexeVal Gameexe::MakeStringValue(std::string value) {
  return GexeVal(GexeStr{std::move(value), next_string_id_++});
}

bool Gameexe::Exists(std::string_view key) const {
  return entries_.find(std::string(key)) != entries_.end();
}

Gameexe::FilterRange Gameexe::Filter(std::string prefix) {
  return FilterRange(this, std::move(prefix));
}

void Gameexe::SetStringAt(std::string_view key, std::string value) {
  std::vector<GexeVal>& slot = Ensure(std::string(key));
  slot.clear();
  slot.push_back(MakeStringValue(std::move(value)));
}

void Gameexe::SetIntAt(std::string_view key, int value) {
  std::vector<GexeVal>& slot = Ensure(std::string(key));
  slot.clear();
  slot.push_back(MakeIntValue(value));
}

void Gameexe::SetAt(std::string_view key, std::vector<GexeVal> value) {
  std::vector<GexeVal>& slot = Ensure(std::string(key));
  slot.swap(value);
}

// -----------------------------------------------------------------------
// GameexeInterpretObject
// -----------------------------------------------------------------------

GameexeInterpretObject::GameexeInterpretObject(Gameexe* owner, std::string key)
    : owner_(owner), key_(std::move(key)), direct_values_(nullptr) {}

GameexeInterpretObject::GameexeInterpretObject(
    Gameexe* owner,
    std::string key,
    std::vector<GexeVal>* direct_values)
    : owner_(owner), key_(std::move(key)), direct_values_(direct_values) {}

GexeExpected<const std::vector<GexeVal>*> GameexeInterpretObject::FetchValues()
    const {
  if (!owner_)
    return make_unexpected(MakeError(key_, "Invalid Gameexe owner", {}));
  if (direct_values_)
    return GexeExpected<const std::vector<GexeVal>*>(direct_values_);

  const auto* values = owner_->Find(key_);
  if (!values)
    return make_unexpected(MakeError(key_, "Unknown Gameexe key", {}));
  return GexeExpected<const std::vector<GexeVal>*>(values);
}

std::vector<GexeVal>* GameexeInterpretObject::EnsureMutableValues() {
  if (direct_values_)
    return direct_values_;
  return &owner_->Ensure(key_);
}

GexeVal& GameexeInterpretObject::operator[](std::size_t idx) {
  const auto* values = owner_->Find(key_);
  return const_cast<GexeVal&>((*values)[idx]);
}

GexeExpected<int> GameexeInterpretObject::Int() const { return IntAt(0); }

int GameexeInterpretObject::ReadInt(int fallback) const {
  return Int().value_or(fallback);
}

bool GameexeInterpretObject::ReadBool(bool fallback) const {
  return !!ReadInt(fallback);
}

GexeExpected<int> GameexeInterpretObject::IntAt(std::size_t index) const {
  auto values = FetchValues();
  if (!values)
    return make_unexpected(values.error());

  const auto& vec = *values.value();
  if (index >= vec.size())
    return make_unexpected(MakeError(key_, "Integer index out of range", {}));

  const GexeVal& value = vec[index];
  if (auto int_ptr = std::get_if<int>(&value))
    return GexeExpected<int>(*int_ptr);

  return make_unexpected(MakeError(key_, "Value is not an integer", {}));
}

GexeExpected<std::string> GameexeInterpretObject::Str() const {
  return StrAt(0);
}

GexeExpected<std::string> GameexeInterpretObject::StrAt(
    std::size_t index) const {
  auto values = FetchValues();
  if (!values)
    return make_unexpected(values.error());

  const auto& vec = *values.value();
  if (index >= vec.size())
    return make_unexpected(MakeError(key_, "String index out of range", {}));

  const GexeVal& value = vec[index];
  if (auto str_ptr = std::get_if<GexeStr>(&value))
    return GexeExpected<std::string>(str_ptr->value);

  return make_unexpected(MakeError(key_, "Value is not a string", {}));
}

GexeExpected<std::vector<int>> GameexeInterpretObject::IntVec() const {
  auto values = FetchValues();
  if (!values)
    return make_unexpected(values.error());

  std::vector<int> ints;
  ints.reserve(values.value()->size());
  for (std::size_t i = 0; i < values.value()->size(); ++i) {
    auto element = IntAt(i);
    if (!element)
      return make_unexpected(element.error());
    ints.push_back(element.value());
  }
  return GexeExpected<std::vector<int>>(std::move(ints));
}

GexeExpected<std::vector<std::string>> GameexeInterpretObject::StrVec() const {
  auto values = FetchValues();
  if (!values)
    return make_unexpected(values.error());

  std::vector<std::string> strings;
  strings.reserve(values.value()->size());
  for (std::size_t i = 0; i < values.value()->size(); ++i) {
    auto element = StrAt(i);
    if (!element)
      return make_unexpected(element.error());
    strings.push_back(std::move(element.value()));
  }
  return GexeExpected<std::vector<std::string>>(std::move(strings));
}

bool GameexeInterpretObject::Exists() const {
  return owner_ && owner_->Exists(key_);
}

std::vector<std::string> GameexeInterpretObject::GetKeyParts() const {
  std::vector<std::string> keyparts;
  std::size_t start = 0;
  while (start <= key_.size()) {
    std::size_t pos = key_.find('.', start);
    if (pos == std::string::npos) {
      keyparts.push_back(key_.substr(start));
      break;
    }
    keyparts.push_back(key_.substr(start, pos - start));
    start = pos + 1;
  }
  return keyparts;
}

GameexeInterpretObject& GameexeInterpretObject::operator=(std::string value) {
  auto* slot = EnsureMutableValues();
  slot->clear();
  slot->push_back(owner_->MakeStringValue(std::move(value)));
  return *this;
}

GameexeInterpretObject& GameexeInterpretObject::operator=(int value) {
  auto* slot = EnsureMutableValues();
  slot->clear();
  slot->push_back(owner_->MakeIntValue(value));
  return *this;
}

int GameexeInterpretObject::ToInt() const {
  auto result = Int();
  if (!result)
    ThrowGameexeError(result.error());
  return result.value();
}

std::string GameexeInterpretObject::ToStr() const {
  auto values = FetchValues();
  if (!values)
    ThrowGameexeError(values.error());

  const auto& vec = *values.value();
  if (vec.empty())
    ThrowGameexeError(MakeError(key_, "No string data present", {}));

  const GexeVal& value = vec.front();
  if (auto str_ptr = std::get_if<GexeStr>(&value))
    return str_ptr->value;

  return std::to_string(std::get<int>(value));
}

std::vector<std::string> GameexeInterpretObject::ToStrVec() const {
  auto values = FetchValues();
  if (!values)
    ThrowGameexeError(values.error());

  std::vector<std::string> strings;
  strings.reserve(values.value()->size());
  for (const GexeVal& value : *values.value()) {
    if (auto str_ptr = std::get_if<GexeStr>(&value))
      strings.emplace_back(str_ptr->value);
    else
      strings.emplace_back(std::to_string(std::get<int>(value)));
  }
  return strings;
}

std::vector<int> GameexeInterpretObject::ToIntVec() const {
  auto values = FetchValues();
  if (!values)
    ThrowGameexeError(values.error());

  std::vector<int> ints;
  ints.reserve(values.value()->size());
  for (const GexeVal& value : *values.value()) {
    if (auto int_ptr = std::get_if<int>(&value))
      ints.push_back(*int_ptr);
    else
      ints.push_back(std::get<GexeStr>(value).id);
  }
  return ints;
}

std::ostream& operator<<(std::ostream& os, GameexeInterpretObject const& io) {
  return os << Join(",", io.ToStrVec());
}

// -----------------------------------------------------------------------
// FilterRange
// -----------------------------------------------------------------------

Gameexe::FilterRange::iterator::iterator(Gameexe* owner,
                                         Storage::iterator current,
                                         Storage::iterator end,
                                         std::string prefix)
    : owner_(owner), current_(current), end_(end), prefix_(std::move(prefix)) {
  AdvanceToMatch();
}

GameexeInterpretObject Gameexe::FilterRange::iterator::operator*() const {
  return GameexeInterpretObject(owner_, current_->first,
                                current_ != end_ ? &current_->second : nullptr);
}

Gameexe::FilterRange::iterator& Gameexe::FilterRange::iterator::operator++() {
  if (current_ != end_)
    ++current_;
  AdvanceToMatch();
  return *this;
}

bool Gameexe::FilterRange::iterator::operator==(const iterator& other) const {
  return current_ == other.current_ && owner_ == other.owner_ &&
         prefix_ == other.prefix_;
}

void Gameexe::FilterRange::iterator::AdvanceToMatch() {
  while (current_ != end_ && !current_->first.starts_with(prefix_)) {
    ++current_;
  }
}

Gameexe::FilterRange::FilterRange(Gameexe* owner, std::string prefix)
    : owner_(owner), prefix_(std::move(prefix)) {}

Gameexe::FilterRange::iterator Gameexe::FilterRange::begin() {
  auto it = owner_->entries_.lower_bound(prefix_);
  return iterator(owner_, it, owner_->entries_.end(), prefix_);
}

Gameexe::FilterRange::iterator Gameexe::FilterRange::end() {
  return iterator(owner_, owner_->entries_.end(), owner_->entries_.end(),
                  prefix_);
}
