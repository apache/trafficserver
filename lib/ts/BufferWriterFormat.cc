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
#include <unistd.h>
#include <sys/param.h>
#include <cctype>
#include <ctime>
#include <cmath>
#include <cmath>
#include <array>
#include <chrono>

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
      out->assign(src.data(), start);
    }
  }
  return zret;
}
} // namespace

namespace ts
{
const BWFSpec BWFSpec::DEFAULT;

const BWFSpec::Property BWFSpec::_prop;

#pragma GCC diagnostic ignored "-Wchar-subscripts"
BWFSpec::Property::Property()
{
  memset(_data, 0, sizeof(_data));
  _data['b'] = TYPE_CHAR | NUMERIC_TYPE_CHAR;
  _data['B'] = TYPE_CHAR | NUMERIC_TYPE_CHAR | UPPER_TYPE_CHAR;
  _data['d'] = TYPE_CHAR | NUMERIC_TYPE_CHAR;
  _data['g'] = TYPE_CHAR;
  _data['o'] = TYPE_CHAR | NUMERIC_TYPE_CHAR;
  _data['p'] = TYPE_CHAR;
  _data['P'] = TYPE_CHAR | UPPER_TYPE_CHAR;
  _data['s'] = TYPE_CHAR;
  _data['S'] = TYPE_CHAR | UPPER_TYPE_CHAR;
  _data['x'] = TYPE_CHAR | NUMERIC_TYPE_CHAR;
  _data['X'] = TYPE_CHAR | NUMERIC_TYPE_CHAR | UPPER_TYPE_CHAR;

  _data[' '] = SIGN_CHAR;
  _data['-'] = SIGN_CHAR;
  _data['+'] = SIGN_CHAR;

  _data['<'] = static_cast<uint8_t>(BWFSpec::Align::LEFT);
  _data['>'] = static_cast<uint8_t>(BWFSpec::Align::RIGHT);
  _data['^'] = static_cast<uint8_t>(BWFSpec::Align::CENTER);
  _data['='] = static_cast<uint8_t>(BWFSpec::Align::SIGN);
}

/// Parse a format specification.
BWFSpec::BWFSpec(TextView fmt)
{
  TextView num; // temporary for number parsing.
  intmax_t n;

  _name = fmt.take_prefix_at(':');
  // if it's parsable as a number, treat it as an index.
  n = tv_to_positive_decimal(_name, &num);
  if (num.size()) {
    _idx = static_cast<decltype(_idx)>(n);
  }

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
      if (!sz.size()) {
        return;
      }
      // sign
      if (is_sign(*sz)) {
        _sign = *sz;
        if (!(++sz).size()) {
          return;
        }
      }
      // radix prefix
      if ('#' == *sz) {
        _radix_lead_p = true;
        if (!(++sz).size()) {
          return;
        }
      }
      // 0 fill for integers
      if ('0' == *sz) {
        if (Align::NONE == _align) {
          _align = Align::SIGN;
        }
        _fill = '0';
        ++sz;
      }
      n = tv_to_positive_decimal(sz, &num);
      if (num.size()) {
        _min = static_cast<decltype(_min)>(n);
        sz.remove_prefix(num.size());
        if (!sz.size()) {
          return;
        }
      }
      // precision
      if ('.' == *sz) {
        n = tv_to_positive_decimal(++sz, &num);
        if (num.size()) {
          _prec = static_cast<decltype(_prec)>(n);
          sz.remove_prefix(num.size());
          if (!sz.size()) {
            return;
          }
        } else {
          throw std::invalid_argument("Precision mark without precision");
        }
      }
      // style (type). Hex, octal, etc.
      if (is_type(*sz)) {
        _type = *sz;
        if (!(++sz).size()) {
          return;
        }
      }
      // maximum width
      if (',' == *sz) {
        n = tv_to_positive_decimal(++sz, &num);
        if (num.size()) {
          _max = static_cast<decltype(_max)>(n);
          sz.remove_prefix(num.size());
          if (!sz.size()) {
            return;
          }
        } else {
          throw std::invalid_argument("Maximum width mark without width");
        }
        // Can only have a type indicator here if there was a max width.
        if (is_type(*sz)) {
          _type = *sz;
          if (!(++sz).size()) {
            return;
          }
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
    size_t extent = lw.extent();
    size_t min    = spec._min;
    size_t size   = lw.size();
    if (extent < min) {
      size_t delta = min - extent;
      char *base   = w.auxBuffer();        // should be first byte of @a lw e.g. lw.data() - avoid const_cast.
      char *limit  = base + lw.capacity(); // first invalid byte.
      char *dst;                           // used to track memory operation targest;
      char *last;                          // track limit of memory operation.
      size_t d2;
      switch (spec._align) {
      case BWFSpec::Align::RIGHT:
        dst = base + delta; // move existing content to here.
        if (dst < limit) {
          last = dst + size; // amount of data to move.
          if (last > limit) {
            last = limit;
          }
          std::memmove(dst, base, last - dst);
        }
        dst  = base;
        last = base + delta;
        if (last > limit) {
          last = limit;
        }
        while (dst < last) {
          *dst++ = spec._fill;
        }
        break;
      case BWFSpec::Align::CENTER:
        d2 = (delta + 1) / 2; // always > 0 because min > extent
        // Move the original content right to make space to fill on the left.
        dst = base + d2; // move existing content to here.
        if (dst < limit) {
          last = dst + size; // amount of data to move.
          if (last > limit) {
            last = limit;
          }
          std::memmove(dst, base, last - dst); // move content.
        }
        // Left fill.
        dst  = base;
        last = base + d2;
        if (last > limit) {
          last = limit;
        }
        while (dst < last) {
          *dst++ = spec._fill;
        }
        // Right fill.
        dst += size;
        last = dst + delta / 2; // round down
        if (last > limit) {
          last = limit;
        }
        while (dst < last) {
          *dst++ = spec._fill;
        }
        break;
      default:
        // Everything else is equivalent to LEFT - distinction is for more specialized
        // types such as integers.
        dst  = base + size;
        last = dst + delta;
        if (last > limit) {
          last = limit;
        }
        while (dst < last) {
          *dst++ = spec._fill;
        }
        break;
      }
      w.fill(min);
    } else {
      size_t max = spec._max;
      if (max < extent) {
        extent = max;
      }
      w.fill(extent);
    }
  }

  // Conversions from remainder to character, in upper and lower case versions.
  // Really only useful for hexadecimal currently.
  namespace
  {
    char UPPER_DIGITS[]                                 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char LOWER_DIGITS[]                                 = "0123456789abcdefghijklmnopqrstuvwxyz";
    static const std::array<uint64_t, 11> POWERS_OF_TEN = {
      {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000, 10000000000}};
  } // namespace

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

  template <typename F>
  void
  Write_Aligned(BufferWriter &w, F const &f, BWFSpec::Align align, int width, char fill, char neg)
  {
    switch (align) {
    case BWFSpec::Align::LEFT:
      if (neg) {
        w.write(neg);
      }
      f();
      while (width-- > 0) {
        w.write(fill);
      }
      break;
    case BWFSpec::Align::RIGHT:
      while (width-- > 0) {
        w.write(fill);
      }
      if (neg) {
        w.write(neg);
      }
      f();
      break;
    case BWFSpec::Align::CENTER:
      for (int i = width / 2; i > 0; --i) {
        w.write(fill);
      }
      if (neg) {
        w.write(neg);
      }
      f();
      for (int i = (width + 1) / 2; i > 0; --i) {
        w.write(fill);
      }
      break;
    case BWFSpec::Align::SIGN:
      if (neg) {
        w.write(neg);
      }
      while (width-- > 0) {
        w.write(fill);
      }
      f();
      break;
    default:
      if (neg) {
        w.write(neg);
      }
      f();
      break;
    }
  }

  BufferWriter &
  Format_Integer(BufferWriter &w, BWFSpec const &spec, uintmax_t i, bool neg_p)
  {
    size_t n     = 0;
    int width    = static_cast<int>(spec._min); // amount left to fill.
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
    if (neg) {
      --width;
    }
    if (prefix1) {
      --width;
      if (prefix2) {
        --width;
      }
    }
    width -= static_cast<int>(n);
    string_view digits{buff + sizeof(buff) - n, n};

    if (spec._align == BWFSpec::Align::SIGN) { // custom for signed case because prefix and digits are seperated.
      if (neg) {
        w.write(neg);
      }
      if (prefix1) {
        w.write(prefix1);
        if (prefix2) {
          w.write(prefix2);
        }
      }
      while (width-- > 0) {
        w.write(spec._fill);
      }
      w.write(digits);
    } else { // use generic Write_Aligned
      Write_Aligned(w,
                    [&]() {
                      if (prefix1) {
                        w.write(prefix1);
                        if (prefix2) {
                          w.write(prefix2);
                        }
                      }
                      w.write(digits);
                    },
                    spec._align, width, spec._fill, neg);
    }
    return w;
  }

  /// Format for floating point values. Seperates floating point into a whole number and a
  /// fraction. The fraction is converted into an unsigned integer based on the specified
  /// precision, spec._prec. ie. 3.1415 with precision two is seperated into two unsigned
  /// integers 3 and 14. The different pieces are assembled and placed into the BufferWriter.
  /// The default is two decimal places. ie. X.XX. The value is always written in base 10.
  ///
  /// format: whole.fraction
  ///     or: left.right
  BufferWriter &
  Format_Floating(BufferWriter &w, BWFSpec const &spec, double f, bool neg_p)
  {
    static const ts::string_view infinity_bwf{"Inf"};
    static const ts::string_view nan_bwf{"NaN"};
    static const ts::string_view zero_bwf{"0"};
    static const ts::string_view subnormal_bwf{"subnormal"};
    static const ts::string_view unknown_bwf{"unknown float"};

    // Handle floating values that are not normal
    if (!std::isnormal(f)) {
      ts::string_view unnormal;
      switch (std::fpclassify(f)) {
      case FP_INFINITE:
        unnormal = infinity_bwf;
        break;
      case FP_NAN:
        unnormal = nan_bwf;
        break;
      case FP_ZERO:
        unnormal = zero_bwf;
        break;
      case FP_SUBNORMAL:
        unnormal = subnormal_bwf;
        break;
      default:
        unnormal = unknown_bwf;
      }

      w.write(unnormal);
      return w;
    }

    uint64_t whole_part = static_cast<uint64_t>(f);
    if (whole_part == f || spec._prec == 0) { // integral
      return Format_Integer(w, spec, whole_part, neg_p);
    }

    static constexpr char dec = '.';
    double frac;
    size_t l = 0;
    size_t r = 0;
    char whole[std::numeric_limits<double>::digits10 + 1];
    char fraction[std::numeric_limits<double>::digits10 + 1];
    char neg               = 0;
    int width              = static_cast<int>(spec._min);                             // amount left to fill.
    unsigned int precision = (spec._prec == BWFSpec::DEFAULT._prec) ? 2 : spec._prec; // default precision 2

    frac = f - whole_part; // split the number

    if (neg_p) {
      neg = '-';
    } else if (spec._sign != '-') {
      neg = spec._sign;
    }

    // Shift the floating point based on the precision. Used to convert
    //  trailing fraction into an integer value.
    uint64_t shift;
    if (precision < POWERS_OF_TEN.size()) {
      shift = POWERS_OF_TEN[precision];
    } else { // not precomputed.
      shift = POWERS_OF_TEN.back();
      for (precision -= (POWERS_OF_TEN.size() - 1); precision > 0; --precision) {
        shift *= 10;
      }
    }

    uint64_t frac_part = static_cast<uint64_t>(frac * shift + 0.5 /* rounding */);

    l = bw_fmt::To_Radix<10>(whole_part, whole, sizeof(whole), bw_fmt::LOWER_DIGITS);
    r = bw_fmt::To_Radix<10>(frac_part, fraction, sizeof(fraction), bw_fmt::LOWER_DIGITS);

    // Clip fill width
    if (neg) {
      --width;
    }
    width -= static_cast<int>(l);
    --width; // '.'
    width -= static_cast<int>(r);

    string_view whole_digits{whole + sizeof(whole) - l, l};
    string_view frac_digits{fraction + sizeof(fraction) - r, r};

    Write_Aligned(w,
                  [&]() {
                    w.write(whole_digits);
                    w.write(dec);
                    w.write(frac_digits);
                  },
                  spec._align, width, spec._fill, neg);

    return w;
  }

  /// Write out the @a data as hexadecimal, using @a digits as the conversion.
  void
  Hex_Dump(BufferWriter &w, string_view data, const char *digits)
  {
    const char *ptr = data.data();
    for (auto n = data.size(); n > 0; --n) {
      char c = *ptr++;
      w.write(digits[(c >> 4) & 0xF]);
      w.write(digits[c & 0xf]);
    }
  }

} // namespace bw_fmt

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, string_view sv)
{
  int width = static_cast<int>(spec._min); // amount left to fill.
  if (spec._prec > 0) {
    sv.remove_prefix(spec._prec);
  }

  if ('x' == spec._type || 'X' == spec._type) {
    const char *digits = 'x' == spec._type ? bw_fmt::LOWER_DIGITS : bw_fmt::UPPER_DIGITS;
    width -= sv.size() * 2;
    if (spec._radix_lead_p) {
      w.write('0');
      w.write(spec._type);
      width -= 2;
    }
    bw_fmt::Write_Aligned(w, [&w, &sv, digits]() { bw_fmt::Hex_Dump(w, sv, digits); }, spec._align, width, spec._fill, 0);
  } else {
    width -= sv.size();
    bw_fmt::Write_Aligned(w, [&w, &sv]() { w.write(sv); }, spec._align, width, spec._fill, 0);
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, MemSpan const &span)
{
  static const BWFormat default_fmt{"{:#x}@{:p}"};
  if (spec._ext.size() && 'd' == spec._ext.front()) {
    const char *digits = 'X' == spec._type ? bw_fmt::UPPER_DIGITS : bw_fmt::LOWER_DIGITS;
    if (spec._radix_lead_p) {
      w.write('0');
      w.write(digits[33]);
    }
    bw_fmt::Hex_Dump(w, span.view(), digits);
  } else {
    w.print(default_fmt, span.size(), span.data());
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
      if (parsed_spec._name.size() == 0) { // no name provided, use implicit index.
        parsed_spec._idx = arg_idx;
      }
      if (parsed_spec._idx < 0) { // name wasn't missing or a valid index, assume global name.
        gf = bw_fmt::Global_Table_Find(parsed_spec._name);
      } else {
        ++arg_idx; // bump this if not a global name.
      }
      _items.emplace_back(parsed_spec, gf);
    }
  }
}

BWFormat::~BWFormat() {}

/// Parse out the next literal and/or format specifier from the format string.
/// Pass the results back in @a literal and @a specifier as appropriate.
/// Update @a fmt to strip the parsed text.
bool
BWFormat::parse(ts::TextView &fmt, string_view &literal, string_view &specifier)
{
  TextView::size_type off;

  // Check for brace delimiters.
  off = fmt.find_if([](char c) { return '{' == c || '}' == c; });
  if (off == TextView::npos) {
    // not found, it's a literal, ship it.
    literal = fmt;
    fmt.remove_prefix(literal.size());
    return false;
  }

  // Processing for braces that don't enclose specifiers.
  if (fmt.size() > off + 1) {
    char c1 = fmt[off];
    char c2 = fmt[off + 1];
    if (c1 == c2) {
      // double braces count as literals, but must tweak to out only 1 brace.
      literal = fmt.take_prefix_at(off + 1);
      return false;
    } else if ('}' == c1) {
      throw std::invalid_argument("BWFormat:: Unopened } in format string.");
    } else {
      literal = string_view{fmt.data(), off};
      fmt.remove_prefix(off + 1);
    }
  } else {
    throw std::invalid_argument("BWFormat: Invalid trailing character in format string.");
  }

  if (fmt.size()) {
    // Need to be careful, because an empty format is OK and it's hard to tell if
    // take_prefix_at failed to find the delimiter or found it as the first byte.
    off = fmt.find('}');
    if (off == TextView::npos) {
      throw std::invalid_argument("BWFormat: Unclosed { in format string");
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
    if (spot != bw_fmt::BWF_GLOBAL_TABLE.end()) {
      return spot->second;
    }
  }
  return nullptr;
}

std::ostream &
FixedBufferWriter::operator>>(std::ostream &s) const
{
  return s << this->view();
}

ssize_t
FixedBufferWriter::operator>>(int fd) const
{
  return ::write(fd, this->data(), this->size());
}

bool
bwf_register_global(string_view name, BWGlobalNameSignature formatter)
{
  return ts::bw_fmt::BWF_GLOBAL_TABLE.emplace(name, formatter).second;
}

} // namespace ts

namespace
{
void
BWF_Timestamp(ts::BufferWriter &w, ts::BWFSpec const &spec)
{
  // Unfortunately need to write to a temporary buffer or the sizing isn't correct if @a w is clipped
  // because @c strftime returns 0 if the buffer isn't large enough.
  char buff[32];
  std::time_t t = std::time(nullptr);
  auto n        = strftime(buff, sizeof(buff), "%Y %b %d %H:%M:%S", std::localtime(&t));
  w.write(ts::string_view{buff, n});
}

void
BWF_Now(ts::BufferWriter &w, ts::BWFSpec const &spec)
{
  bwformat(w, spec, std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

void
BWF_Tick(ts::BufferWriter &w, ts::BWFSpec const &spec)
{
  bwformat(w, spec, std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

void
BWF_ThreadID(ts::BufferWriter &w, ts::BWFSpec const &spec)
{
  bwformat(w, spec, pthread_self());
}

void
BWF_ThreadName(ts::BufferWriter &w, ts::BWFSpec const &spec)
{
#if defined(__FreeBSD_version)
  bwformat(w, spec, "thread"_sv); // no thread names in FreeBSD.
#else
  char name[32]; // manual says at least 16, bump that up a bit.
  pthread_getname_np(pthread_self(), name, sizeof(name));
  bwformat(w, spec, ts::string_view{name});
#endif
}

static bool BW_INITIALIZED __attribute__((unused)) = []() -> bool {
  ts::bw_fmt::BWF_GLOBAL_TABLE.emplace("now", &BWF_Now);
  ts::bw_fmt::BWF_GLOBAL_TABLE.emplace("tick", &BWF_Tick);
  ts::bw_fmt::BWF_GLOBAL_TABLE.emplace("timestamp", &BWF_Timestamp);
  ts::bw_fmt::BWF_GLOBAL_TABLE.emplace("thread-id", &BWF_ThreadID);
  ts::bw_fmt::BWF_GLOBAL_TABLE.emplace("thread-name", &BWF_ThreadName);
  return true;
}();
} // namespace

namespace std
{
ostream &
operator<<(ostream &s, ts::FixedBufferWriter &w)
{
  return s << w.view();
}
} // namespace std
