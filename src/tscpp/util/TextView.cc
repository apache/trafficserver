/** @file

    Class for handling "views" of a buffer. Views presume the memory for the
    buffer is managed elsewhere and allow efficient access to segments of the
    buffer without copies. Views are read only as the view doesn't own the
    memory. Along with generic buffer methods are specialized methods to support
    better string parsing, particularly token based parsing.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more
    contributor license agreements.  See the NOTICE file distributed with this
    work for additional information regarding copyright ownership.  The ASF
    licenses this file to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
    License for the specific language governing permissions and limitations under
    the License.
*/
#include "tscpp/util/TextView.h"
#include <cctype>
#include <sstream>

int
ts::memcmp(TextView const &lhs, TextView const &rhs)
{
  int zret;
  size_t n;

  // Seems a bit ugly but size comparisons must be done anyway to get the memcmp args.
  if (lhs.size() < rhs.size()) {
    zret = 1, n = lhs.size();
  } else {
    n    = rhs.size();
    zret = rhs.size() < lhs.size() ? -1 : 0;
  }

  int r = ::memcmp(lhs.data(), rhs.data(), n);
  if (0 != r) { // If we got a not-equal, override the size based result.
    zret = r;
  }

  return zret;
}

int
strcasecmp(const std::string_view &lhs, const std::string_view &rhs)
{
  size_t len = std::min(lhs.size(), rhs.size());
  int zret   = strncasecmp(lhs.data(), rhs.data(), len);
  if (0 == zret) {
    if (lhs.size() < rhs.size()) {
      zret = -1;
    } else if (lhs.size() > rhs.size()) {
      zret = 1;
    }
  }
  return zret;
}

const int8_t ts::svtoi_convert[256] = {
  /* [can't do this nicely because clang format won't allow exdented comments]
   0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 00
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 10
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 20
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -1, -1, -1, // 30
  -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, // 40
  25, 26, 27, 28, 20, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1, // 50
  -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, // 60
  25, 26, 27, 28, 20, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1, // 70
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 80
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 90
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // A0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // B0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // C0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // D0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // E0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // F0
};

intmax_t
ts::svtoi(TextView src, TextView *out, int base)
{
  intmax_t zret = 0;

  if (out) {
    out->clear();
  }
  if (!(0 <= base && base <= 36)) {
    return 0;
  }
  if (src.ltrim_if(&isspace) && src) {
    const char *start = src.data();
    int8_t v;
    bool neg = false;
    if ('-' == *src) {
      ++src;
      neg = true;
    }
    // If base is 0, it wasn't specified - check for standard base prefixes
    if (0 == base) {
      base = 10;
      if ('0' == *src) {
        ++src;
        base = 8;
        if (src && ('x' == *src || 'X' == *src)) {
          ++src;
          base = 16;
        }
      }
    }

    // For performance in common cases, use the templated conversion.
    switch (base) {
    case 8:
      zret = svto_radix<8>(src);
      break;
    case 10:
      zret = svto_radix<10>(src);
      break;
    case 16:
      zret = svto_radix<16>(src);
      break;
    default:
      while (src.size() && (0 <= (v = svtoi_convert[static_cast<unsigned char>(*src)])) && v < base) {
        auto n = zret * base + v;
        if (n < zret) {
          zret = std::numeric_limits<uintmax_t>::max();
          break; // overflow, stop parsing.
        }
        zret = n;
        ++src;
      }
      break;
    }

    if (out && (src.data() > (neg ? start + 1 : start))) {
      out->assign(start, src.data());
    }

    if (neg) {
      zret = -zret;
    }
  }
  return zret;
}

// Do the template instantions.
template std::ostream &ts::TextView::stream_write(std::ostream &, const ts::TextView &) const;

namespace std
{
ostream &
operator<<(ostream &os, const ts::TextView &b)
{
  if (os.good()) {
    b.stream_write<ostream>(os, b);
    os.width(0);
  }
  return os;
}
} // namespace std
