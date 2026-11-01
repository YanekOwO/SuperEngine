/*
 *  Copyright (C) 2000, 2007-   Kazunori Ueno(JAGARL)
 * <jagarl@creator.club.ne.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include <memory>
#include <optional>
#include <vector>

class ArchiveInfo {
 public:
  /* dest は256byte 程度の余裕があること */
  static void Extract2k(char*& dest, char*& src, char* destend, char* srcend);
};

class IConverter {
 public:
  struct PixelRegion {
    int x = 0, y = 0, width = 0, height = 0;
    int origin_x = 0, origin_y = 0;
  };

  struct Region {
    int x1, y1, x2, y2;
    int origin_x, origin_y;
    std::optional<PixelRegion> pixel_region;
    int Width() { return x2 - x1; }
    int Height() { return y2 - y1; }
    void FixVar(int& v, int& w) {
      if (v < 0)
        v = 0;
      if (v >= w)
        v = w - 1;
    }
    // This only place I've found which relies on Fix() is the Kanon English
    // patch. I suspect that vaconv and other fan tools are/were broken.
    void Fix(int w, int h) {
      FixVar(x1, w);
      FixVar(x2, w);
      FixVar(y1, h);
      FixVar(y2, h);
      if (x1 > x2)
        x2 = x1;
      if (y1 > y2)
        y2 = y1;
    }

    bool operator<(const Region& rhs) const;
  };

  std::vector<Region> region_table;

  int width;
  int height;
  bool is_mask;

  const char* data = nullptr;
  int datalen;

  int Width() { return width; }
  int Height() { return height; }
  bool IsMask() { return is_mask; }

  IConverter();
  virtual ~IConverter() = default;
  void Init(const char* data, int dlen, int width, int height, bool is_mask);

  virtual bool Read(char* image) = 0;
  static std::unique_ptr<IConverter> CreateConverter(const char* inbuf,
                                                     int inlen);

  void CopyRGBA(char* image, const char* from);
  void CopyRGB(char* image, const char* from);
  void CopyRGBA_rev(char* image, const char* from);
  void CopyRGB_rev(char* image, const char* from);
};
