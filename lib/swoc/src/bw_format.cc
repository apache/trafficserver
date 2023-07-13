// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019

/** @file

    Formatted output for BufferWriter.
 */

#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <sys/param.h>
#include <unistd.h>

#include "swoc/BufferWriter.h"
#include "swoc/bwf_base.h"
#include "swoc/bwf_ex.h"
#include "swoc/swoc_meta.h"
#include "swoc/DiscreteRange.h"

using namespace std::literals;
using namespace swoc::literals;

swoc::bwf::ExternalNames swoc::bwf::Global_Names;
using swoc::svto_radix;

namespace swoc { inline namespace SWOC_VERSION_NS {

namespace bwf {

const Spec Spec::DEFAULT;

const Spec::Property Spec::_prop;

#pragma GCC diagnostic ignored "-Wchar-subscripts"

Spec::Property::Property() {
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

  _data[SIGN_NEVER]  = SIGN_CHAR;
  _data[SIGN_NEG]    = SIGN_CHAR;
  _data[SIGN_ALWAYS] = SIGN_CHAR;

  _data['<'] = static_cast<uint8_t>(Spec::Align::LEFT);
  _data['>'] = static_cast<uint8_t>(Spec::Align::RIGHT);
  _data['^'] = static_cast<uint8_t>(Spec::Align::CENTER);
  _data['='] = static_cast<uint8_t>(Spec::Align::SIGN);
}

Spec::Spec(const TextView &fmt) {
  this->parse(fmt);
}

/// Parse a format specification.
bool
Spec::parse(TextView fmt) {
  TextView num; // temporary for number parsing.

  _name = fmt.take_prefix_at(':');
  // if it's parsable as a number, treat it as an index.
  num    = _name;
  auto n = svto_radix<10>(num);
  if (num.empty()) {
    _idx = static_cast<decltype(_idx)>(n);
  }

  if (fmt.size()) {
    TextView sz = fmt.take_prefix_at(':'); // the format specifier.
    _ext        = fmt;                     // anything past the second ':' is the extension.
    if (sz.size()) {
      // fill and alignment
      if ('%' == *sz) { // enable URI encoding of the fill character so
        // metasyntactic chars can be used if needed.
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
        return true;
      }
      // sign
      if (is_sign(*sz)) {
        _sign = *sz;
        if (!(++sz).size()) {
          return true;
        }
      }
      // radix prefix
      if ('#' == *sz) {
        _radix_lead_p = true;
        if (!(++sz).size()) {
          return true;
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
      num = sz;
      n   = svto_radix<10>(num);
      if (num.size() < sz.size()) {
        _min = static_cast<decltype(_min)>(n);
        sz   = num;
        if (!sz.size()) {
          return true;
        }
      }
      // precision
      if ('.' == *sz) {
        num = ++sz;
        n   = svto_radix<10>(num);
        if (num.size() < sz.size()) {
          _prec = static_cast<decltype(_prec)>(n);
          sz    = num;
          if (!sz.size()) {
            return true;
          }
        } else {
          throw std::invalid_argument("Precision mark without precision");
        }
      }
      // style (type). Hex, octal, etc.
      if (is_type(*sz)) {
        _type = *sz;
        if (!(++sz).size()) {
          return true;
        }
      }
      // maximum width
      if (',' == *sz) {
        num = ++sz;
        n   = svto_radix<10>(num);
        if (num.size() < sz.size()) {
          _max = static_cast<decltype(_max)>(n);
          sz   = num;
          if (!sz.size()) {
            return true;
          }
        } else {
          throw std::invalid_argument("Maximum width mark without width");
        }
        // Can only have a type indicator here if there was a max width.
        if (is_type(*sz)) {
          _type = *sz;
          if (!(++sz).size()) {
            return true;
          }
        }
      }
    }
  }
  return true;
}

/// Parse out the next literal and/or format specifier from the format string.
/// Pass the results back in @a literal and @a specifier as appropriate.
/// Update @a fmt to strip the parsed text.
/// @return @c true if a specifier was parsed, @c false if not.
bool
Format::TextViewExtractor::parse(TextView &fmt, std::string_view &literal, std::string_view &specifier) {
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
      // double braces count as literals, but must tweak to output only 1 brace.
      literal = fmt.take_prefix(off + 1);
      return false;
    } else if ('}' == c1) {
      throw std::invalid_argument("Unopened } in format string.");
    } else {
      literal = std::string_view{fmt.data(), off};
      fmt.remove_prefix(off + 1);
    }
  } else {
    throw std::invalid_argument("Invalid trailing character in format string.");
  }

  if (fmt.size()) {
    // Need to be careful, because an empty format is OK and it's hard to tell
    // if take_prefix_at failed to find the delimiter or found it as the first
    // byte.
    off = fmt.find('}');
    if (off == TextView::npos) {
      throw std::invalid_argument("BWFormat: Unclosed { in format string");
    }
    specifier = fmt.take_prefix(off);
    return true;
  }
  return false;
}

bool
Format::TextViewExtractor::operator()(std::string_view &literal_v, Spec &spec) {
  if (!_fmt.empty()) {
    std::string_view spec_v;
    if (parse(_fmt, literal_v, spec_v)) {
      return spec.parse(spec_v);
    }
  }
  return false;
}

bool
Format::FormatExtractor::operator()(std::string_view &literal_v, swoc::bwf::Spec &spec) {
  literal_v = {};
  if (_idx < int(_fmt.size()) && _fmt[_idx]._type == Spec::LITERAL_TYPE) {
    literal_v = _fmt[_idx++]._ext;
  }
  if (_idx < int(_fmt.size()) && _fmt[_idx]._type != Spec::LITERAL_TYPE) {
    spec = _fmt[_idx++];
    return true;
  }
  return false;
}

void
Err_Bad_Arg_Index(BufferWriter &w, int i, size_t n) {
  static const Format fmt{"{{BAD_ARG_INDEX:{} of {}}}"sv};
  w.print(fmt, i, n);
}

/** This performs generic alignment operations.

   If a formatter specialization performs this operation instead, that should
   result in output that is at least @a spec._min characters wide, which will
   cause this function to make no further adjustments.
 */
void
Adjust_Alignment(BufferWriter &aux, Spec const &spec) {
  size_t extent = aux.extent();
  size_t min    = spec._min;
  if (extent < min) {
    size_t delta      = min - extent;
    size_t left_delta = 0, right_delta = delta; // left justify values
    if (Spec::Align::RIGHT == spec._align) {
      left_delta  = delta;
      right_delta = 0;
    } else if (Spec::Align::CENTER == spec._align) {
      left_delta  = delta / 2;
      right_delta = (delta + 1) / 2;
    }
    if (left_delta > 0) {
      size_t work_area = extent + left_delta;
      aux.commit(left_delta);          // cover work area.
      aux.copy(left_delta, 0, extent); // move to create space for left fill.
      aux.discard(work_area);          // roll back to write the left fill.
      for (int i = left_delta; i > 0; --i) {
        aux.write(spec._fill);
      }
      aux.commit(extent);
    }
    for (int i = right_delta; i > 0; --i) {
      aux.write(spec._fill);
    }

  } else {
    size_t max = spec._max;
    if (max < extent) {
      aux.discard(extent - max);
    }
  }
}

// Conversions from remainder to character, in upper and lower case versions.
// Really only useful for hexadecimal currently.
namespace {
char UPPER_DIGITS[]                                 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char LOWER_DIGITS[]                                 = "0123456789abcdefghijklmnopqrstuvwxyz";
static const std::array<uint64_t, 11> POWERS_OF_TEN = {
  {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000, 10000000000}};
} // namespace

/// Templated radix based conversions. Only a small number of radix are
/// supported and providing a template minimizes cut and paste code while also
/// enabling compiler optimizations (e.g. for power of 2 radix the modulo /
/// divide become bit operations).
template <size_t RADIX>
size_t
To_Radix(uintmax_t n, char *buff, size_t width, char *digits) {
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

/** Output a string with a specified alignment.
 *
 * @tparam F Output functor.
 * @param w Output buffer.
 * @param f Functor that generates output.
 * @param align Alignment.
 * @param width Field width.
 * @param fill Fill character.
 * @param neg Character to use for negative (null char means do not output).
 */
template <typename F>
void
Write_Aligned(BufferWriter &w, F const &f, Spec::Align align, int width, char fill, char neg) {
  switch (align) {
  case Spec::Align::LEFT:
    if (neg) {
      w.write(neg);
    }
    f();
    while (width-- > 0) {
      w.write(fill);
    }
    break;
  case Spec::Align::RIGHT:
    while (width-- > 0) {
      w.write(fill);
    }
    if (neg) {
      w.write(neg);
    }
    f();
    break;
  case Spec::Align::CENTER:
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
  case Spec::Align::SIGN:
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
Format_Integer(BufferWriter &w, Spec const &spec, uintmax_t i, bool neg_p) {
  size_t n     = 0;
  int width    = static_cast<int>(spec._min); // amount left to fill.
  char neg     = 0;
  char prefix1 = spec._radix_lead_p ? '0' : 0;
  char prefix2 = 0;
  char buff[std::numeric_limits<uintmax_t>::digits + 1];

  if (spec._sign != Spec::SIGN_NEVER) {
    if (neg_p) {
      neg = '-';
    } else if (spec._sign == Spec::SIGN_ALWAYS) {
      neg = spec._sign;
    }
  }

  switch (spec._type) {
  case 'x':
    prefix2 = 'x';
    n       = bwf::To_Radix<16>(i, buff, sizeof(buff), bwf::LOWER_DIGITS);
    break;
  case 'X':
    prefix2 = 'X';
    n       = bwf::To_Radix<16>(i, buff, sizeof(buff), bwf::UPPER_DIGITS);
    break;
  case 'b':
    prefix2 = 'b';
    n       = bwf::To_Radix<2>(i, buff, sizeof(buff), bwf::LOWER_DIGITS);
    break;
  case 'B':
    prefix2 = 'B';
    n       = bwf::To_Radix<2>(i, buff, sizeof(buff), bwf::UPPER_DIGITS);
    break;
  case 'o':
    n = bwf::To_Radix<8>(i, buff, sizeof(buff), bwf::LOWER_DIGITS);
    break;
  default:
    prefix1 = 0;
    n       = bwf::To_Radix<10>(i, buff, sizeof(buff), bwf::LOWER_DIGITS);
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
  std::string_view digits{buff + sizeof(buff) - n, n};

  if (spec._align == Spec::Align::SIGN) { // custom for signed case because
    // prefix and digits are seperated.
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
    Write_Aligned(
      w,
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

/// Format for floating point values. Seperates floating point into a whole
/// number and a fraction. The fraction is converted into an unsigned integer
/// based on the specified precision, spec._prec. ie. 3.1415 with precision two
/// is seperated into two unsigned integers 3 and 14. The different pieces are
/// assembled and placed into the BufferWriter. The default is two decimal
/// places. ie. X.XX. The value is always written in base 10.
///
/// format: whole.fraction
///     or: left.right
BufferWriter &
Format_Float(BufferWriter &w, Spec const &spec, double f, bool negative_p) {
  static const std::string_view infinity_bwf{"Inf"};
  static const std::string_view nan_bwf{"NaN"};
  static const std::string_view zero_bwf{"0"};
  static const std::string_view subnormal_bwf{"subnormal"};
  static const std::string_view unknown_bwf{"unknown float"};

  // Handle floating values that are not normal
  if (!std::isnormal(f)) {
    std::string_view unnormal;
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
    return Format_Integer(w, spec, whole_part, negative_p);
  }

  static constexpr char dec = '.';
  double frac;
  size_t l = 0;
  size_t r = 0;
  char whole[std::numeric_limits<double>::digits10 + 1];
  char fraction[std::numeric_limits<double>::digits10 + 1];
  char neg               = 0;
  int width              = static_cast<int>(spec._min);                          // amount left to fill.
  unsigned int precision = (spec._prec == Spec::DEFAULT._prec) ? 2 : spec._prec; // default precision 2

  frac = f - whole_part; // split the number

  if (negative_p) {
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

  l = bwf::To_Radix<10>(whole_part, whole, sizeof(whole), bwf::LOWER_DIGITS);
  r = bwf::To_Radix<10>(frac_part, fraction, sizeof(fraction), bwf::LOWER_DIGITS);

  // Clip fill width
  if (neg) {
    --width;
  }
  width -= static_cast<int>(l);
  --width; // '.'
  width -= static_cast<int>(r);

  std::string_view whole_digits{whole + sizeof(whole) - l, l};
  std::string_view frac_digits{fraction + sizeof(fraction) - r, r};

  Write_Aligned(
    w,
    [&]() {
      w.write(whole_digits);
      w.write(dec);
      w.write(frac_digits);
    },
    spec._align, width, spec._fill, neg);

  return w;
}

void
Format_As_Hex(BufferWriter &w, std::string_view view, const char *digits) {
  const char *ptr = view.data();
  for (auto n = view.size(); n > 0; --n) {
    char c = *ptr++;
    w.write(digits[(c >> 4) & 0xF]);
    w.write(digits[c & 0xf]);
  }
}

/// Preparse format string for later use.
Format::Format(TextView fmt) {
  Spec lit_spec;
  int arg_idx = 0;
  auto ex{bind(fmt)};
  std::string_view literal_v;

  lit_spec._type = Spec::LITERAL_TYPE;

  while (ex) {
    Spec spec;
    bool spec_p = ex(literal_v, spec);

    if (literal_v.size()) {
      lit_spec._ext = literal_v;
      _items.emplace_back(lit_spec);
    }

    if (spec_p) {
      if (spec._name.size() == 0) { // no name provided, use implicit index.
        spec._idx = arg_idx++;
      }
      if (spec._idx >= 0) {
        ++arg_idx;
      }
      _items.emplace_back(spec);
    }
  }
}

bool
Format::is_literal() const {
  for (auto const& spec : _items) {
    if (Spec::LITERAL_TYPE != spec._type) {
      return false;
    }
  }
  return true;
}

NameBinding::~NameBinding() {}

} // namespace bwf

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, std::string_view sv) {
  auto width = int(spec._min); // amount to fill.
  if (spec._prec > 0) {
    sv = sv.substr(0, spec._prec);
  }
  if ('x' == spec._type || 'X' == spec._type) {
    return bwformat(w, spec, bwf::HexDump(sv.data(), sv.size()));
  } else if ('s' == spec._type) {
    bwformat(w, spec, transform_view_of(&tolower, sv));
  } else if ('S' == spec._type) {
    bwformat(w, spec, transform_view_of(&toupper, sv));
  } else {
    width -= sv.size();
    bwf::Write_Aligned(
      w, [&w, &sv]() { w.write(sv); }, spec._align, width, spec._fill, 0);
  }
  return w;
}

// Generic pointer formatting
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, const void *ptr) {
  using namespace swoc::literals;
  bwf::Spec ptr_spec{spec};
  ptr_spec._radix_lead_p = true;

  if (ptr == nullptr) {
    if (spec._type == 's' || spec._type == 'S') {
      ptr_spec._type = bwf::Spec::DEFAULT_TYPE;
      ptr_spec._ext  = ""_sv; // clear any extension.
      return bwformat(w, spec, spec._type == 's' ? "null"_sv : "NULL"_sv);
    } else if (spec._type == bwf::Spec::DEFAULT_TYPE) {
      return w; // print nothing if there is no format character override.
    }
  }

  if (ptr_spec._type == bwf::Spec::DEFAULT_TYPE || ptr_spec._type == 'p') {
    ptr_spec._type = 'x'; // if default or 'p;, switch to lower hex.
  } else if (ptr_spec._type == 'P') {
    ptr_spec._type = 'X'; // P means upper hex, overriding other specializations.
  }
  return bwf::Format_Integer(w, ptr_spec, reinterpret_cast<intptr_t>(ptr), false);
}
// doc end

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::HexDump const &hex) {
  char fmt_type      = spec._type;
  const char *digits = bwf::UPPER_DIGITS;

  if ('X' != fmt_type) {
    fmt_type = 'x';
    digits   = bwf::LOWER_DIGITS;
  }

  int width = int(spec._min) - hex._view.size() * 2; // amount left to fill.
  if (spec._radix_lead_p) {
    w.write('0');
    w.write(fmt_type);
    width -= 2;
  }
  bwf::Write_Aligned(
    w, [&w, &hex, digits]() { bwf::Format_As_Hex(w, hex._view, digits); }, spec._align, width, spec._fill, 0);
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, MemSpan<void const> const &span) {
  if ('x' == spec._type || 'X' == spec._type) {
    const char *digits =  ('X' == spec._type) ? bwf::UPPER_DIGITS : bwf::LOWER_DIGITS;
    size_t block       = spec._prec > 0 ? spec._prec : span.size();
    TextView view{span.rebind<char const>()};
    bool space_p = false;
    while (view) {
      if (space_p)
        w.write(' ');
      space_p = true;
      if (spec._radix_lead_p) {
        w.write('0').write(digits[33]);
      }
      bwf::Format_As_Hex(w, view.prefix(block), digits);
      view.remove_prefix(block);
    }
  } else {
    static const bwf::Format default_fmt{"{:#x}@{:p}"};
    w.print(default_fmt, span.size(), span.data());
  }
  return w;
}

std::ostream &
FixedBufferWriter::operator>>(std::ostream &s) const {
  return s << this->view();
}

namespace {
// Hand rolled, might not be totally compliant everywhere, but probably close
// enough. The long string will be locally accurate. Clang requires the double
// braces. Why, Turing only knows.
static const std::array<std::string_view, 134> ERRNO_SHORT_NAME = {{
  "SUCCESS",
  "EPERM",
  "ENOENT",
  "ESRCH",
  "EINTR",
  "EIO",
  "ENXIO",
  "E2BIG ",
  "ENOEXEC",
  "EBADF",
  "ECHILD",
  "EAGAIN",
  "ENOMEM",
  "EACCES",
  "EFAULT",
  "ENOTBLK",
  "EBUSY",
  "EEXIST",
  "EXDEV",
  "ENODEV",
  "ENOTDIR",
  "EISDIR",
  "EINVAL",
  "ENFILE",
  "EMFILE",
  "ENOTTY",
  "ETXTBSY",
  "EFBIG",
  "ENOSPC",
  "ESPIPE",
  "EROFS",
  "EMLINK",
  "EPIPE",
  "EDOM",
  "ERANGE",
  "EDEADLK",
  "ENAMETOOLONG",
  "ENOLCK",
  "ENOSYS",
  "ENOTEMPTY",
  "ELOOP",
  "EWOULDBLOCK",
  "ENOMSG",
  "EIDRM",
  "ECHRNG",
  "EL2NSYNC",
  "EL3HLT",
  "EL3RST",
  "ELNRNG",
  "EUNATCH",
  "ENOCSI",
  "EL2HTL",
  "EBADE",
  "EBADR",
  "EXFULL",
  "ENOANO",
  "EBADRQC",
  "EBADSLT",
  "EDEADLOCK",
  "EBFONT",
  "ENOSTR",
  "ENODATA",
  "ETIME",
  "ENOSR",
  "ENONET",
  "ENOPKG",
  "EREMOTE",
  "ENOLINK",
  "EADV",
  "ESRMNT",
  "ECOMM",
  "EPROTO",
  "EMULTIHOP",
  "EDOTDOT",
  "EBADMSG",
  "EOVERFLOW",
  "ENOTUNIQ",
  "EBADFD",
  "EREMCHG",
  "ELIBACC",
  "ELIBBAD",
  "ELIBSCN",
  "ELIBMAX",
  "ELIBEXEC",
  "EILSEQ",
  "ERESTART",
  "ESTRPIPE",
  "EUSERS",
  "ENOTSOCK",
  "EDESTADDRREQ",
  "EMSGSIZE",
  "EPROTOTYPE",
  "ENOPROTOOPT",
  "EPROTONOSUPPORT",
  "ESOCKTNOSUPPORT",
  "EOPNOTSUPP",
  "EPFNOSUPPORT",
  "EAFNOSUPPORT",
  "EADDRINUSE",
  "EADDRNOTAVAIL",
  "ENETDOWN",
  "ENETUNREACH",
  "ENETRESET",
  "ECONNABORTED",
  "ECONNRESET",
  "ENOBUFS",
  "EISCONN",
  "ENOTCONN",
  "ESHUTDOWN",
  "ETOOMANYREFS",
  "ETIMEDOUT",
  "ECONNREFUSED",
  "EHOSTDOWN",
  "EHOSTUNREACH",
  "EALREADY",
  "EINPROGRESS",
  "ESTALE",
  "EUCLEAN",
  "ENOTNAM",
  "ENAVAIL",
  "EISNAM",
  "EREMOTEIO",
  "EDQUOT",
  "ENOMEDIUM",
  "EMEDIUMTYPE",
  "ECANCELED",
  "ENOKEY",
  "EKEYEXPIRED",
  "EKEYREVOKED",
  "EKEYREJECTED",
  "EOWNERDEAD",
  "ENOTRECOVERABLE",
  "ERFKILL",
  "EHWPOISON",
}};
static constexpr DiscreteRange<unsigned> ERRNO_RANGE{0, ERRNO_SHORT_NAME.size() - 1};
// This provides convenient safe access to the errno short name array.
auto errno_short_name = [](unsigned n) { return ERRNO_RANGE.contains(n) ? ERRNO_SHORT_NAME[n] : "Unknown"sv; };
} // namespace

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::Errno const &e) {
  static const bwf::Format number_fmt{"[{}]"sv}; // numeric value format.

  if (spec.has_numeric_type()) { // if numeric type, print just the numeric part
    w.print(number_fmt, e._e);
  } else {
    TextView ext{spec._ext};
    bool short_p = false;
    if (ext.empty() || ext.npos != ext.find('s')) {
      w.write(errno_short_name(e._e));
      short_p = true;
    }
    if (ext.empty() || ext.npos != ext.find('l')) {
      if (short_p) {
        w.write(": ");
      }
      w.write(strerror(e._e));
    }
    if (spec._type != 's' && spec._type != 'S') {
      w.write(' ');
      w.print(number_fmt, e._e);
    }
  }
  return w;
}

bwf::Date::Date(std::string_view fmt) : _epoch(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())), _fmt(fmt) {}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::Date const &date) {
  if (spec.has_numeric_type()) {
    bwformat(w, spec, date._epoch);
  } else {
    struct tm t;
    auto r = w.remaining();
    size_t n{0};
    // Verify @a fmt is null terminated, even outside the bounds of the view.
    if (date._fmt.data()[date._fmt.size() - 1] != 0 && date._fmt.data()[date._fmt.size()] != 0) {
      throw(std::invalid_argument{"BWF Date String is not null terminated."});
    }
    // Get the time, GMT or local if specified.
    if (spec._ext == "local"sv) {
      localtime_r(&date._epoch, &t);
    } else {
      gmtime_r(&date._epoch, &t);
    }
    // Try a direct write, faster if it works.
    if (r > 0) {
      n = strftime(w.aux_data(), r, date._fmt.data(), &t);
    }
    if (n > 0) {
      w.commit(n);
    } else {
      // Direct write didn't work. Unfortunately need to write to a temporary
      // buffer or the sizing isn't correct if @a w is clipped because @c
      // strftime returns 0 if the buffer isn't large enough.
      char buff[256]; // hope for the best - no real way to resize appropriately on failure.
      n = strftime(buff, sizeof(buff), date._fmt.data(), &t);
      w.write(buff, n);
    }
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::Pattern const &pattern) {
  if (! pattern._text.empty()) { // If there's no text, no point in looping.
    auto limit        = std::min<size_t>(spec._max, pattern._text.size() * pattern._n);
    decltype(limit) n = 0;
    while (n < limit) {
      w.write(pattern._text);
      n += pattern._text.size();
    }
  }
  return w;
}

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, std::error_code const &ec) {
  static const auto GENERIC_CATEGORY = &std::generic_category();
  static const auto SYSTEM_CATEGORY  = &std::system_category();

  // This provides convenient safe access to the errno short name array.
  static const swoc::bwf::Format number_fmt{"[{}]"_sv}; // numeric value format.
  if (spec.has_numeric_type()) {
    // if numeric type, print just the numeric part.
    bwformat(w, spec, ec.value());
  } else {
    if ((&ec.category() == GENERIC_CATEGORY || &ec.category() == SYSTEM_CATEGORY) && swoc::ERRNO_RANGE.contains(ec.value())) {
      bwformat(w, spec, swoc::ERRNO_SHORT_NAME[ec.value()]);
    } else {
      w.write(ec.message());
    }
    if (spec._type != 's' && spec._type != 'S') {
      w.write(' ');
      w.print(number_fmt, ec.value());
    }
  }
  return w;
}

}} // namespace swoc::SWOC_VERSION_NS

namespace std {
ostream &
operator<<(ostream &s, swoc::FixedBufferWriter &w) {
  return s << w.view();
}

} // namespace std
