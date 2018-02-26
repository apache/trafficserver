/** @file

    Formatted output for BufferWriter.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */

#include <ts/BufferWriter.h>
#include <ctype.h>
#include <ctime>

namespace
{
// Customized version of string to int. Using this instead of the general @c svtoi function
// made @c bwprint performance test run in < 30% of the time, changing it from about 2.5
// times slower than snprintf to the same speed. This version handles only positive integers
// in decimal.
inline int
tv_to_positive_decimal(ts::TextView src, ts::TextView *out)
{
  int zret = 0;

  if (out) {
    out->clear();
  }
  src.ltrim_if(&isspace);
  if (src.size()) {
    const char *start = src.data();
    const char *limit = start + src.size();
    while (start < limit && ('0' <= *start && *start <= '9')) {
      zret = zret * 10 + *start - '0';
      ++start;
    }
    if (out && (start > src.data())) {
      out->set_view(src.data(), start);
    }
  }
  return zret;
}
}

namespace ts
{
const ts::BWFSpec ts::BWFSpec::DEFAULT;

/// Parse a format specification.
BWFSpec::BWFSpec(TextView fmt)
{
  TextView num;
  intmax_t n;

  _name = fmt.take_prefix_at(':');
  // if it's parsable as a number, treat it as an index.
  n = tv_to_positive_decimal(_name, &num);
  if (num.size())
    _idx = static_cast<decltype(_idx)>(n);

  if (fmt.size()) {
    TextView sz = fmt.take_prefix_at(':'); // the format specifier.
    _ext        = fmt;                     // anything past the second ':' is the extension.
    if (sz.size()) {
      // fill and alignment
      if ('%' == *sz) { // enable URI encoding of the fill character so metasyntactic chars can be used if needed.
        if (sz.size() < 4) {
          throw std::invalid_argument("Fill URI encoding without 2 hex characters and align mark");
        }
        if (Align::NONE == (_align = align_of(sz[3]))) {
          throw std::invalid_argument("Fill URI without alignment mark");
        }
        char d1 = sz[1], d0 = sz[2];
        if (!isxdigit(d0) || !isxdigit(d1)) {
          throw std::invalid_argument("URI encoding with non-hex characters");
        }
        _fill = isdigit(d0) ? d0 - '0' : tolower(d0) - 'a' + 10;
        _fill += (isdigit(d1) ? d1 - '0' : tolower(d1) - 'a' + 10) << 4;
        sz += 4;
      } else if (sz.size() > 1 && Align::NONE != (_align = align_of(sz[1]))) {
        _fill = *sz;
        sz += 2;
      } else if (Align::NONE != (_align = align_of(*sz))) {
        ++sz;
      }
      if (!sz.size())
        return;
      // sign
      if (is_sign(*sz)) {
        _sign = *sz;
        if (!(++sz).size())
          return;
      }
      // radix prefix
      if ('#' == *sz) {
        _radix_lead_p = true;
        if (!(++sz).size())
          return;
      }
      // 0 fill for integers
      if ('0' == *sz) {
        if (Align::NONE == _align)
          _align = Align::SIGN;
        _fill    = '0';
        ++sz;
      }
      n = tv_to_positive_decimal(sz, &num);
      if (num.size()) {
        _min = static_cast<decltype(_min)>(n);
        sz.remove_prefix(num.size());
        if (!sz.size())
          return;
      }
      // precision
      if ('.' == *sz) {
        n = tv_to_positive_decimal(++sz, &num);
        if (num.size()) {
          _prec = static_cast<decltype(_prec)>(n);
          sz.remove_prefix(num.size());
          if (!sz.size())
            return;
        } else {
          throw std::invalid_argument("Precision mark without precision");
        }
      }
      // style (type). Hex, octal, etc.
      if (is_type(*sz)) {
        _type = *sz;
        if (!(++sz).size())
          return;
      }
      // maximum width
      if (',' == *sz) {
        n = tv_to_positive_decimal(++sz, &num);
        if (num.size()) {
          _max = static_cast<decltype(_max)>(n);
          sz.remove_prefix(num.size());
          if (!sz.size())
            return;
        } else {
          throw std::invalid_argument("Maximum width mark without width");
        }
        // Can only have a type indicator here if there was a max width.
        if (is_type(*sz)) {
          _type = *sz;
          if (!(++sz).size())
            return;
        }
      }
    }
  }
}

namespace bw_fmt
{
  GlobalTable BWF_GLOBAL_TABLE;

  void
  Err_Bad_Arg_Index(BufferWriter &w, int i, size_t n)
  {
    static const BWFormat fmt{"{{BAD_ARG_INDEX:{} of {}}}"_sv};
    w.print(fmt, i, n);
  }

  /** This performs generic alignment operations.

      If a formatter specialization performs this operation instead, that should result in output that
      is at least @a spec._min characters wide, which will cause this function to make no further
      adjustments.
   */
  void
  Do_Alignment(BWFSpec const &spec, BufferWriter &w, BufferWriter &lw)
  {
    size_t size = lw.size();
    size_t min  = spec._min;
    if (size < min) {
      size_t delta = min - size; // note - size <= extent -> size < min
      switch (spec._align) {
      case BWFSpec::Align::NONE: // same as LEFT for output.
      case BWFSpec::Align::LEFT:
        w.fill(size);
        while (delta--)
          w.write(spec._fill);
        break;
      case BWFSpec::Align::RIGHT:
        std::memmove(w.auxBuffer() + delta, w.auxBuffer(), size);
        while (delta--)
          w.write(spec._fill);
        w.fill(size);
        break;
      case BWFSpec::Align::CENTER:
        if (delta > 1) {
          size_t d2 = delta / 2;
          std::memmove(w.auxBuffer() + (delta / 2), w.auxBuffer(), size);
          while (d2--)
            w.write(spec._fill);
        }
        w.fill(size);
        delta = (delta + 1) / 2;
        while (delta--)
          w.write(spec._fill);
        break;
      case BWFSpec::Align::SIGN:
        w.fill(size);
        break;
      }
    } else {
      w.fill(size);
    }
  }

  // Conversions from remainder to character, in upper and lower case versions.
  // Really only useful for hexadecimal currently.
  namespace
  {
    char UPPER_DIGITS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char LOWER_DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  }

  /// Templated radix based conversions. Only a small number of radix are supported
  /// and providing a template minimizes cut and paste code while also enabling
  /// compiler optimizations (e.g. for power of 2 radix the modulo / divide become
  /// bit operations).
  template <size_t RADIX>
  size_t
  To_Radix(uintmax_t n, char *buff, size_t width, char *digits)
  {
    static_assert(1 < RADIX && RADIX <= 36, "RADIX must be in the range 2..36");
    char *out = buff + width;
    if (n) {
      while (n) {
        *--out = digits[n % RADIX];
        n /= RADIX;
      }
    } else {
      *--out = '0';
    }
    return (buff + width) - out;
  }

  BufferWriter &
  Format_Integer(BufferWriter &w, BWFSpec const &spec, uintmax_t i, bool neg_p)
  {
    size_t n  = 0;
    int width = static_cast<int>(spec._min); // amount left to fill.
    string_view prefix;
    char neg     = 0;
    char prefix1 = spec._radix_lead_p ? '0' : 0;
    char prefix2 = 0;
    char buff[std::numeric_limits<uintmax_t>::digits + 1];

    if (neg_p) {
      neg = '-';
    } else if (spec._sign != '-') {
      neg = spec._sign;
    }

    switch (spec._type) {
    case 'x':
      prefix2 = 'x';
      n       = bw_fmt::To_Radix<16>(i, buff, sizeof(buff), bw_fmt::LOWER_DIGITS);
      break;
    case 'X':
      prefix2 = 'X';
      n       = bw_fmt::To_Radix<16>(i, buff, sizeof(buff), bw_fmt::UPPER_DIGITS);
      break;
    case 'b':
      prefix2 = 'b';
      n       = bw_fmt::To_Radix<2>(i, buff, sizeof(buff), bw_fmt::LOWER_DIGITS);
      break;
    case 'B':
      prefix2 = 'B';
      n       = bw_fmt::To_Radix<2>(i, buff, sizeof(buff), bw_fmt::UPPER_DIGITS);
      break;
    case 'o':
      n = bw_fmt::To_Radix<8>(i, buff, sizeof(buff), bw_fmt::LOWER_DIGITS);
      break;
    default:
      prefix1 = 0;
      n       = bw_fmt::To_Radix<10>(i, buff, sizeof(buff), bw_fmt::LOWER_DIGITS);
      break;
    }
    // Clip fill width by stuff that's already committed to be written.
    if (neg)
      --width;
    if (prefix1) {
      --width;
      if (prefix2)
        --width;
    }
    width -= static_cast<int>(n);
    string_view digits{buff + sizeof(buff) - n, n};

    // The idea here is the various pieces have all been assembled, the only difference
    // is the order in which they are written to the output.
    switch (spec._align) {
    case BWFSpec::Align::LEFT:
      if (neg)
        w.write(neg);
      if (prefix1) {
        w.write(prefix1);
        if (prefix2)
          w.write(prefix2);
      }
      w.write(digits);
      while (width-- > 0)
        w.write(spec._fill);
      break;
    case BWFSpec::Align::RIGHT:
      while (width-- > 0)
        w.write(spec._fill);
      if (neg)
        w.write(neg);
      if (prefix1) {
        w.write(prefix1);
        if (prefix2)
          w.write(prefix2);
      }
      w.write(digits);
      break;
    case BWFSpec::Align::CENTER:
      for (int i = width / 2; i > 0; --i)
        w.write(spec._fill);
      if (neg)
        w.write(neg);
      if (prefix1) {
        w.write(prefix1);
        if (prefix2)
          w.write(prefix2);
      }
      w.write(digits);
      for (int i = (width + 1) / 2; i > 0; --i)
        w.write(spec._fill);
      break;
    case BWFSpec::Align::SIGN:
      if (neg)
        w.write(neg);
      if (prefix1) {
        w.write(prefix1);
        if (prefix2)
          w.write(prefix2);
      }
      while (width-- > 0)
        w.write(spec._fill);
      w.write(digits);
      break;
    default:
      if (neg)
        w.write(neg);
      if (prefix1) {
        w.write(prefix1);
        if (prefix2)
          w.write(prefix2);
      }
      w.write(digits);
      break;
    }
    return w;
  }

} // bw_fmt

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, string_view sv)
{
  int width = static_cast<int>(spec._min); // amount left to fill.
  if (spec._prec > 0)
    sv.remove_prefix(spec._prec);

  width -= sv.size();
  switch (spec._align) {
  case BWFSpec::Align::LEFT:
  case BWFSpec::Align::SIGN:
    w.write(sv);
    while (width-- > 0)
      w.write(spec._fill);
    break;
  case BWFSpec::Align::RIGHT:
    while (width-- > 0)
      w.write(spec._fill);
    w.write(sv);
    break;
  case BWFSpec::Align::CENTER:
    for (int i = width / 2; i > 0; --i)
      w.write(spec._fill);
    w.write(sv);
    for (int i = (width + 1) / 2; i > 0; --i)
      w.write(spec._fill);
    break;
  default:
    w.write(sv);
    break;
  }
  return w;
}

/// Preparse format string for later use.
BWFormat::BWFormat(ts::TextView fmt)
{
  BWFSpec lit_spec{BWFSpec::DEFAULT};
  int arg_idx = 0;

  while (fmt) {
    string_view lit_str;
    string_view spec_str;
    bool spec_p = this->parse(fmt, lit_str, spec_str);

    if (lit_str.size()) {
      lit_spec._ext = lit_str;
      _items.emplace_back(lit_spec, &Format_Literal);
    }
    if (spec_p) {
      bw_fmt::GlobalSignature gf = nullptr;
      BWFSpec parsed_spec{spec_str};
      if (parsed_spec._name.size() == 0) {
        parsed_spec._idx = arg_idx;
      }
      if (parsed_spec._idx < 0) {
        gf = bw_fmt::Global_Table_Find(parsed_spec._name);
      }
      _items.emplace_back(parsed_spec, gf);
      ++arg_idx;
    }
  }
}

BWFormat::~BWFormat()
{
}

bool
BWFormat::parse(ts::TextView &fmt, string_view &literal, string_view &specifier)
{
  TextView::size_type off;

  off = fmt.find_if([](char c) { return '{' == c || '}' == c; });
  if (off == TextView::npos) {
    literal = fmt;
    fmt.remove_prefix(literal.size());
    return false;
  }

  if (fmt.size() > off + 1) {
    char c1 = fmt[off];
    char c2 = fmt[off + 1];
    if (c1 == c2) {
      literal = fmt.take_prefix_at(off + 1);
      return false;
    } else if ('}' == c1) {
      throw std::invalid_argument("Unopened }");
    } else {
      literal = string_view{fmt.data(), off};
      fmt.remove_prefix(off + 1);
    }
  } else {
    throw std::invalid_argument("Invalid trailing character");
  }

  if (fmt.size()) {
    // Need to be careful, because an empty format is OK and it's hard to tell if
    // take_prefix_at failed to find the delimiter or found it as the first byte.
    off = fmt.find('}');
    if (off == TextView::npos) {
      throw std::invalid_argument("Unclosed {");
    }
    specifier = fmt.take_prefix_at(off);
    return true;
  }
  return false;
}

void
BWFormat::Format_Literal(BufferWriter &w, BWFSpec const &spec)
{
  w.write(spec._ext);
}

bw_fmt::GlobalSignature
bw_fmt::Global_Table_Find(string_view name)
{
  if (name.size()) {
    auto spot = bw_fmt::BWF_GLOBAL_TABLE.find(name);
    if (spot != bw_fmt::BWF_GLOBAL_TABLE.end())
      return spot->second;
  }
  return nullptr;
}

} // ts

namespace
{
void
BWF_Now(ts::BufferWriter &w, ts::BWFSpec const &spec)
{
  std::time_t t = std::time(nullptr);
  w.fill(std::strftime(w.auxBuffer(), w.remaining(), "%Y%b%d:%H%M%S", std::localtime(&t)));
}

static bool BW_INITIALIZED = []() -> bool {
  ts::bw_fmt::BWF_GLOBAL_TABLE.emplace("now", &BWF_Now);
  return true;
}();
}
