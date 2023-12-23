// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    Class for handling "views" of a buffer. Views presume the memory for the
    buffer is managed elsewhere and allow efficient access to segments of the
    buffer without copies. Views are read only as the view doesn't own the
    memory. Along with generic buffer methods are specialized methods to support
    better string parsing, particularly token based parsing.
*/

#include "swoc/TextView.h"
#include <cctype>
#include <sstream>

using namespace swoc::literals;

namespace swoc { inline namespace SWOC_VERSION_NS {

/// @cond INTERNAL_DETAIL
const int8_t svtoi_convert[256] = {
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
/// @endcond

intmax_t
svtoi(TextView src, TextView *out, int base) {
  static constexpr uintmax_t ABS_MAX = std::numeric_limits<intmax_t>::max();
  static constexpr uintmax_t ABS_MIN = uintmax_t(std::numeric_limits<intmax_t>::min());
  intmax_t zret = 0;

  if (src.ltrim_if(&isspace)) {
    TextView parsed;
    const char *start = src.data();
    bool neg          = false;
    if ('-' == *src) {
      ++src;
      neg = true;
    } else if ('+' == *src) {
      ++src;
    }
    auto n = svtou(src, &parsed, base);
    if (!parsed.empty()) {
      if (out) {
        out->assign(start, parsed.data_end());
      }
      if (neg) {
        zret = -intmax_t(std::min<uintmax_t>(n, ABS_MIN));
      } else {
        zret = std::min(n, ABS_MAX);
      }
    }
  }
  return zret;
}

uintmax_t
svtou(TextView src, TextView *out, int base) {
  uintmax_t zret = 0;

  if (out) {
    out->clear();
  }

  if (src.ltrim_if(&isspace).size()) {
    auto origin = src.data(); // cache to handle prefix skipping.
    // If base is 0, it wasn't specified - check for standard base prefixes
    if (0 == base) {
      base = 10;
      if ('0' == *src) {
        ++src;
        base = 8;
        if (src) {
          switch (*src) {
          case 'x':
          case 'X':
            ++src;
            base = 16;
            break;
          case 'b':
          case 'B':
            ++src;
            base = 2;
            break;
          }
        }
      }
    }
    if (!(1 <= base && base <= 36)) {
      return 0;
    }

    // For performance in common cases, use the templated conversion.
    switch (base) {
    case 2:
      zret = svto_radix<2>(src);
      break;
    case 8:
      zret = svto_radix<8>(src);
      break;
    case 10:
      zret = svto_radix<10>(src);
      break;
    case 16:
      zret = svto_radix<16>(src);
      break;
    default: {
      static constexpr auto MAX            = std::numeric_limits<uintmax_t>::max();
      const auto OVERFLOW_LIMIT = MAX / base;
      intmax_t v    = 0;
      while (src.size() && (0 <= (v = svtoi_convert[static_cast<unsigned char>(*src)])) && v < base) {
        ++src;
        if (zret <= OVERFLOW_LIMIT && uintmax_t(v) <= (MAX - (zret *= base))) {
          zret += v;
        } else {
          zret = MAX;
        }
      }
      break;
      }
    }

    if (out) {
      out->assign(origin, src.data());
    }
  }
  return zret;
}

double
svtod(swoc::TextView text, swoc::TextView *parsed) {
  // @return 10^e
  auto pow10 = [](int e) -> double {
    double zret  = 1.0;
    double scale = 10.0;
    if (e < 0) { // flip the scale and make @a e positive.
      e     = -e;
      scale = 0.1;
    }

    // Walk the bits in the exponent @a e and multiply the scale for set bits.
    while (e) {
      if (e & 1) {
        zret *= scale;
      }
      scale  *= scale;
      e     >>= 1;
    }
    return zret;
  };

  if (text.empty()) {
    return 0;
  }

  auto org_text = text; // save this to update @a parsed.
  // Check just once and dump to a local copy if needed.
  TextView local_parsed;
  if (!parsed) {
    parsed = &local_parsed;
  }

  // Handle leading sign.
  int sign = 1;
  if (*text == '-') {
    ++text;
    sign = -1;
  } else if (*text == '+') {
    ++text;
  }
  // Parse the leading whole part as an integer.
  intmax_t whole = svto_radix<10>(text);
  parsed->assign(org_text.data(), text.data());

  if (text.empty()) {
    return whole;
  }

  double frac = 0.0;
  if (*text == '.') { // fractional part.
    ++text;
    double scale = 0.1;
    while (text && isdigit(*text)) {
      frac  += scale * (*text++ - '0');
      scale /= 10.0;
    }
  }

  double exp = 1.0;
  if (text.starts_with_nocase("e")) {
    int exp_sign = 1;
    ++text;
    if (text) {
      if (*text == '+') {
        ++text;
      } else if (*text == '-') {
        ++text;
        exp_sign = -1;
      }
    }
    auto exp_part = svto_radix<10>(text);
    exp           = pow10(exp_part * exp_sign);
  }

  parsed->assign(org_text.data(), text.data());
  return sign * (whole + frac) * exp;
}

// Do the template instantiations.
template std::ostream &TextView::stream_write(std::ostream &, const TextView &) const;

}} // namespace swoc::SWOC_VERSION_NS

namespace std {
ostream &
operator<<(ostream &os, const swoc::TextView &b) {
  if (os.good()) {
    b.stream_write<ostream>(os, b);
    os.width(0);
  }
  return os;
}
} // namespace std
