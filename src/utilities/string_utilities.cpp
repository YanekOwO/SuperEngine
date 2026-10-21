// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2008 Elliot Glaysher
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

#include "utilities/string_utilities.hpp"

#include "encodings/codepage.hpp"
#include "utf8.h"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <stdexcept>
#include <string>

using std::string;
using std::wstring;

wstring cp932toUnicode(const string& line, int transformation) {
  return Cp::instance(transformation).ConvertString(line);
}

string TransformationName(int transformation) {
  switch (transformation) {
    case 0:
      return "Japanese (Cp932)";
    case 1:
      return "Chinese (Cp936)";
    case 2:
      return "Western";
    case 3:
      return "Korean (Cp949)";
    default:
      return "Unknown";
  }
}

string UnicodeToUTF8(const std::wstring& widestring) {
  string out;
  utf8::utf16to8(widestring.begin(), widestring.end(), back_inserter(out));

  return out;
}

string cp932toUTF8(const string& line, int transformation) {
  if (line.empty())
    return line;

  std::wstring ws = cp932toUnicode(line, transformation);
  return UnicodeToUTF8(ws);
}

bool IsOpeningQuoteMark(int codepoint) {
  return codepoint == 0x300C || codepoint == 0x300E || codepoint == 0xFF08;
}

bool IsWrappingRomanCharacter(int codepoint) {
  if ((codepoint >= 'A' && codepoint <= 'Z') ||
      (codepoint >= 'a' && codepoint <= 'z') || codepoint == '\'' ||
      codepoint == '-') {
    return true;
  }

  return false;
}

bool IsKinsoku(int codepoint) {
  static constexpr int matchingCodepoints[] = {
      0x0021, 0x0022, 0x0027, 0x0029, 0x002c, 0x002e, 0x003a, 0x003b, 0x003e,
      0x003f, 0x005d, 0x007d, 0x2019, 0x201d, 0x2025, 0x2026, 0x3001, 0x3002,
      0x3009, 0x300b, 0x300d, 0x300f, 0x3011, 0x301f, 0x3041, 0x3043, 0x3045,
      0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308e, 0x30a1, 0x30a3,
      0x30a5, 0x30a7, 0x30a9, 0x30c3, 0x30e3, 0x30e5, 0x30e7, 0x30ee, 0x30f5,
      0x30f6, 0x30fb, 0x30fc, 0xff01, 0xff09, 0xff0c, 0xff0e, 0xff1a, 0xff1b,
      0xff1f, 0xff3d, 0xff5d, 0xff5e, 0xff61, 0xff63, 0xff64, 0xff65, 0xff67,
      0xff68, 0xff69, 0xff6a, 0xff6b, 0xff6c, 0xff6d, 0xff6e, 0xff6f, 0xff70,
      0xff9e, 0xff9f};

  const int* begin = std::begin(matchingCodepoints);
  const int* end = std::end(matchingCodepoints);
  return std::binary_search(begin, end, codepoint);
}

int Codepoint(const string& c) {
  if (c == "") {
    return 0;
  } else {
    string::const_iterator it = c.begin();
    return utf8::next(it, c.end());
  }
}

void AdvanceOneShiftJISChar(const char*& c) {
  if (shiftjis_lead_byte(c[0])) {
    if (c[1] == '\0') {
      throw std::runtime_error("Malformed Shift_JIS string!");
    } else {
      c += 2;
    }
  } else {
    c += 1;
  }
}

void CopyOneShiftJisCharacter(const char*& str, string& output) {
  if (shiftjis_lead_byte(str[0])) {
    if (str[1] == '\0') {
      throw std::runtime_error("Malformed Shift_JIS string!");
    } else {
      output += *str++;
      output += *str++;
    }
  } else {
    output += *str++;
  }
}

bool ReadFullwidthLatinLetter(const char*& str, string& output) {
  // The fullwidth uppercase latin characters are 0x8260 through 0x8279.
  if (str[0] == 0x82) {
    if (str[1] == 0) {
      throw std::runtime_error("Malformed Shift_JIS string!");
    } else if (str[1] >= 0x60 && str[1] <= 0x79) {
      char printable = str[1] - 0x1F;
      output += printable;
      str += 2;
      return true;
    }
  }

  return false;
}

void AddShiftJISChar(uint16_t c, string& output) {
  if (c > 0xFF)
    output += (c >> 8);
  output += (c & 0xFF);
}

void PrintTextToFunction(
    std::function<bool(const string& c, const string& nextChar)> fun,
    const string& charsToPrint,
    const std::string& nextCharForFinal) {
  // Iterate over each incoming character to display (we do this
  // instead of rendering the entire string so that we can perform
  // indentation, et cetera.)
  string::const_iterator cur = charsToPrint.begin();
  string::const_iterator tmp = cur;
  string::const_iterator end = charsToPrint.end();
  utf8::next(tmp, end);
  string curChar(cur, tmp);
  for (cur = tmp; tmp != end; cur = tmp) {
    // TODO(erg): Do we have to check the return value here?
    fun(curChar, string(cur, end));

    utf8::next(tmp, end);
    curChar = string(cur, tmp);
  }

  fun(curChar, nextCharForFinal);
}

string RemoveQuotes(const string& quotedString) {
  string output = quotedString;

  if (!output.empty() && output.front() == '"')
    output.erase(0, 1);

  if (!output.empty() && output.back() == '"')
    output.pop_back();

  return output;
}

int ConvertLetterIndexToInt(const std::string& value) {
  if (value.empty()) {
    throw std::invalid_argument(
        "ConvertLetterIndexToInt: Input string is empty.");
  }

  int total = 0;
  for (size_t i = 0; i < value.size(); ++i) {
    char current = value[i];

    if (current < 'A' || current > 'Z') {
      throw std::invalid_argument(
          "ConvertLetterIndexToInt: Invalid character '" +
          std::string(1, current) + "' in input string.");
    }
    int digit = current - 'A';

    if (total > (INT32_MAX - digit) / 26) {
      throw std::overflow_error(
          "ConvertLetterIndexToInt: Integer overflow for input value '" +
          value + "'.");
    }

    total = total * 26 + digit;
  }

  return total;
}

bool parse_int(std::string_view sv, int& out, int base) {
  sv = trim_sv(sv);
  if (!sv.empty() && sv.front() == '+')
    sv.remove_prefix(1);
  if (sv.empty())
    return false;
  const char* first = sv.data();
  const char* last = sv.data() + sv.size();
  auto [ptr, ec] = std::from_chars(first, last, out, base);
  return ec == std::errc{} && ptr == last;
}

std::string EncodeText(std::string_view text) {
  std::string result;
  result.reserve(text.size() + 2);
  for (char ch : text) {
    switch (ch) {
      case '\\':
        result += "\\\\";
        break;
      case '"':
        result += "\\\"";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        result.push_back(ch);
        break;
    }
  }
  return result;
}
