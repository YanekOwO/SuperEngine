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

#include "m6/tokenizer.hpp"

#include "log/domain_logger.hpp"
#include "m6/source_buffer.hpp"
#include "m6/source_location.hpp"
#include "machine/op.hpp"

#include <cctype>
#include <charconv>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace m6 {

namespace {
// A static lookup table for single-character tokens
static const std::unordered_map<char, tok::Token_t> SINGLE_CHAR_TOKEN = {
    {'[', tok::SquareL()},      {']', tok::SquareR()},
    {'{', tok::CurlyL()},       {'}', tok::CurlyR()},
    {'(', tok::ParenthesisL()}, {')', tok::ParenthesisR()},
    {';', tok::Semicol()},      {':', tok::Colon()}};

// Sorted by descending length, so we can match the longest possible operator
// first.
static const std::vector<std::string> OPERATORS = {
    ">>>=", ">>>", ">>=", ">>", "<<=", "**=", "**", "<<", "+=",
    "-=",   "*=",  "/=",  "%=", "&=",  "|=",  "^=", "==", "!=",
    "<=",   ">=",  "||",  "&&", "=",   "+",   "-",  "*",  "/",
    "%",    "~",   "&",   "|",  "^",   "<",   ">",  ",",  "."};

static const std::unordered_map<std::string, tok::Token_t>
    RESERVED_KEYWORD_TOKEN = {{"nil", tok::Reserved(tok::Reserved::_nil)},
                              {"true", tok::Reserved(tok::Reserved::_true)},
                              {"false", tok::Reserved(tok::Reserved::_false)},
                              {"if", tok::Reserved(tok::Reserved::_if)},
                              {"else", tok::Reserved(tok::Reserved::_else)},
                              {"while", tok::Reserved(tok::Reserved::_while)},
                              {"for", tok::Reserved(tok::Reserved::_for)},
                              {"fn", tok::Reserved(tok::Reserved::_fn)},
                              {"class", tok::Reserved(tok::Reserved::_class)},
                              {"goto", tok::Reserved(tok::Reserved::_goto)},
                              {"return", tok::Reserved(tok::Reserved::_return)},
                              {"global", tok::Reserved(tok::Reserved::_global)},
                              {"yield", tok::Reserved(tok::Reserved::_yield)},
                              {"spawn", tok::Reserved(tok::Reserved::_spawn)},
                              {"await", tok::Reserved(tok::Reserved::_await)},
                              {"try", tok::Reserved(tok::Reserved::_try)},
                              {"catch", tok::Reserved(tok::Reserved::_catch)},
                              {"throw", tok::Reserved(tok::Reserved::_throw)},
                              {"import", tok::Reserved(tok::Reserved::_import)},
                              {"from", tok::Reserved(tok::Reserved::_from)},
                              {"as", tok::Reserved(tok::Reserved::_as)}};

// Attempt to match an operator from position `pos` in `input`.
// Returns the matched operator string if successful, else an empty string.
static std::string matchLongestOperator(std::string_view input, size_t pos) {
  for (auto const& op : OPERATORS) {
    if (pos + op.size() <= input.size()) {
      if (input.compare(pos, op.size(), op) == 0) {
        return op;
      }
    }
  }
  return {};
}

// Unescape a string literal (removes surrounding quotes and handles backslash
// escapes).
static std::string unescapeString(std::string_view value) {
  // e.g., if value = "\"some\\nstuff\"", we remove leading/trailing quotes,
  // then parse escapes
  if (value.size() < 2) {
    // malformed, but we'll just return empty
    return {};
  }

  // Remove leading and trailing quote (")
  std::string_view inner = value.substr(1, value.size() - 2);

  std::string result;
  result.reserve(inner.size());

  for (std::size_t i = 0; i < inner.size(); ++i) {
    char c = inner[i];
    if (c == '\\' && i + 1 < inner.size()) {
      // escaped character
      char next = inner[++i];
      switch (next) {
        case 'n':
          result.push_back('\n');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '"':
          result.push_back('"');
          break;
        // ...
        default:
          result.push_back(next);
          break;
      }
    } else {
      result.push_back(c);
    }
  }
  return result;
}

}  // namespace

void Tokenizer::Parse(std::shared_ptr<SourceBuffer> src) {
  if (!errors_.empty()) {
    DomainLogger logger("Tokenizer");
    logger(Severity::Warn) << "Unhandled errors.";
  }

  std::string_view input = src->GetView();

  size_t pos = 0;
  const size_t len = input.size();

  while (pos < len) {
    const size_t start = pos;
    char c = input[pos];

    // 1) Check whitespace
    if (std::isspace(static_cast<unsigned char>(c))) {
      // Accumulate all consecutive whitespace into one token
      while (pos < len &&
             std::isspace(static_cast<unsigned char>(input[pos]))) {
        ++pos;
      }

      if (!skip_ws_) {
        storage_.emplace_back(tok::WS(), src->GetReference(start, pos));
      }
      continue;
    }

    // 2) Check comments
    if (c == '/' && pos + 1 < len) {
      const char next = input[pos + 1];
      if (next == '/') {  // line comment
        pos += 2;
        while (pos < len && input[pos] != '\n')
          ++pos;

        if (!skip_comments_) {
          std::string comment = std::string(input.substr(start, pos - start));
          storage_.emplace_back(
              tok::Comment{tok::Comment::Kind::Line, std::move(comment)},
              src->GetReference(start, pos));
        }
        continue;
      }

      if (next == '*') {  // block comment
        pos += 2;
        bool closed = false;
        while (pos < len) {
          if (input[pos] == '*' && pos + 1 < len && input[pos + 1] == '/') {
            pos += 2;
            closed = true;
            break;
          }
          ++pos;
        }

        if (!closed) {
          errors_.emplace_back("Unterminated block comment.",
                               src->GetReference(start, pos));
          continue;
        }

        if (!skip_comments_) {
          std::string comment = std::string(input.substr(start, pos - start));
          storage_.emplace_back(
              tok::Comment{tok::Comment::Kind::Block, std::move(comment)},
              src->GetReference(start, pos));
        }
        continue;
      }
    }

    // 3) Check single character token
    if (SINGLE_CHAR_TOKEN.find(c) != SINGLE_CHAR_TOKEN.end()) {
      storage_.emplace_back(SINGLE_CHAR_TOKEN.at(c),
                            src->GetReference(start, ++pos));
      continue;
    }

    // 4) Check operator (longest match)
    {
      std::string opMatch = matchLongestOperator(input, pos);
      if (!opMatch.empty()) {
        // we matched an operator token
        pos += opMatch.size();
        storage_.emplace_back(tok::Operator(CreateOp(opMatch)),
                              src->GetReference(start, pos));

        continue;
      }
    }

    std::string idVal = [&]() -> std::string {
      if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_')
        return "";

      ++pos;
      while (pos < len &&
             (std::isalnum(static_cast<unsigned char>(input[pos])) ||
              input[pos] == '_')) {
        ++pos;
      }
      return std::string(input.substr(start, pos - start));
    }();

    // 5) Check reserved keywords
    if (RESERVED_KEYWORD_TOKEN.contains(idVal)) {
      storage_.emplace_back(RESERVED_KEYWORD_TOKEN.at(idVal),
                            src->GetReference(start, pos));
      continue;
    }

    // 6) Check identifier: [a-zA-Z_][a-zA-Z0-9_]*
    if (!idVal.empty()) {
      storage_.emplace_back(tok::ID(std::move(idVal)),
                            src->GetReference(start, pos));
      continue;
    }

    // 7) Check integer literal: hex (0x), octal (0o), binary (0b), or decimal
    if (std::isdigit(static_cast<unsigned char>(c))) {
      // determine base
      int base = 10;
      size_t prefix_len = 0;
      const char first_char = input[start];

      // only consider prefixes if the literal begins with '0'
      if (first_char == '0' && start + 1 < len) {
        char peek = input[start + 1];
        if (peek == 'x' || peek == 'X') {
          base = 16;
          prefix_len = 2;
        } else if (peek == 'b' || peek == 'B') {
          base = 2;
          prefix_len = 2;
        } else if (peek == 'o' || peek == 'O') {
          base = 8;
          prefix_len = 2;
        }
      }

      // advance past prefix (if any)
      pos = start + prefix_len;
      size_t digits_start = pos;
      auto digits_begin_it = input.cbegin() + digits_start;

      // now scan valid digits for this base
      auto [digits_end_it, first_invalid] = std::mismatch(
          digits_begin_it, input.cend(), digits_begin_it,
          [base](char ch, char) {
            const unsigned char uch = static_cast<unsigned char>(ch);
            bool isCandidate = std::isalnum(uch);
            bool isValid;
            switch (base) {
              case 16:
                isValid = std::isxdigit(uch);
                break;
              case 10:
                isValid = std::isdigit(uch);
                break;
              case 8:
                isValid = ('0' <= ch && ch <= '7');
                break;
              case 2:
                isValid = (ch == '0' || ch == '1');
                break;
              [[unlikely]] default:
                isValid = false;
            }

            return isCandidate && isValid;
          });
      if (first_invalid != digits_end_it) {
        pos = std::distance(input.cbegin(), first_invalid);
        errors_.emplace_back("Invalid digit in integer literal.",
                             src->GetReferenceAt(pos));
      } else {
        pos = std::distance(input.cbegin(), digits_end_it);
      }

      const auto location = src->GetReference(start, pos);

      // must have at least one digit after any prefix
      if (digits_begin_it == digits_end_it) {
        errors_.emplace_back("Invalid integer literal", location);
        continue;
      }

      int numValue = 0;
      auto [ptr, ec] =
          std::from_chars(digits_begin_it, digits_end_it, numValue, base);
      if (ec == std::errc::result_out_of_range)
        errors_.emplace_back("Integer literal is too large.", location);
      else if (ec != std::errc{})
        errors_.emplace_back("Invalid integer literal.", location);
      else if (ptr != digits_end_it)
        errors_.emplace_back("Invalid digit in integer literal.",
                             src->GetReferenceAt(ptr - input.data()));
      else
        storage_.emplace_back(tok::Int(numValue), location);

      continue;
    }

    // 8) Check string literal: \"([^\"\\\\]|\\\\.)*\"
    // A naive approach: detect '\"', then parse until matching '\"' or end of
    // input.
    if (c == '\"') {
      ++pos;  // consume the opening quote
      bool closed = false;
      while (pos < len) {
        if (input[pos] == '\\') {  // skip escaped character
          pos += 2;
        } else if (input[pos] == '\"') {  // found closing quote
          ++pos;
          closed = true;
          break;
        } else if (input[pos] == '\n') {  // disallow multi-line string literal
          closed = false;
          break;
        } else {
          ++pos;
        }
      }

      if (!closed) {
        storage_.emplace_back(tok::Literal(std::string(input.substr(start))),
                              src->GetReference(start, pos));
        errors_.emplace_back("Expected '\"'", src->GetReference(pos, pos));
      } else {
        // substring from start to pos is the entire literal (including quotes)
        std::string fullString = std::string(input.substr(start, pos - start));
        // unescape it
        std::string unescaped = unescapeString(fullString);
        storage_.emplace_back(tok::Literal(std::move(unescaped)),
                              src->GetReference(start, pos));
      }
      continue;
    }

    // 9) If none matched, mark it as an error. We consume one character.
    errors_.emplace_back("Unknown token", src->GetReference(start, start + 1));
    ++pos;
  }

  if (add_eof_)
    storage_.emplace_back(tok::Eof(), src->GetReference(len, len + 1));
}

Tokenizer::Tokenizer(std::vector<Token>& s)
    : errors_(),
      skip_ws_(true),
      skip_comments_(true),
      add_eof_(true),
      storage_(s) {}

bool Tokenizer::Ok() const { return errors_.empty(); }

std::span<const Error> Tokenizer::GetErrors() const { return errors_; }

void Tokenizer::ClearErrors() { errors_.clear(); }

}  // namespace m6
