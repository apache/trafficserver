/** @file

    Unit tests for BufferFormat and bwprint.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#include <chrono>
#include <iostream>
#include <variant>

#include "swoc/MemSpan.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_std.h"
#include "swoc/bwf_ex.h"
#include "swoc/bwf_ip.h"

#include "catch.hpp"

using namespace std::literals;
using swoc::TextView;
using swoc::BufferWriter;
using swoc::bwf::Spec;
using swoc::LocalBufferWriter;

static constexpr TextView VERSION{"1.0.2"};

TEST_CASE("BWFormat substrings", "[swoc][bwf][substr]") {
  LocalBufferWriter<256> bw;
  std::string_view text{"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"};

  bw.clear().print("Text: |{0:20}|", text.substr(0, 10));
  REQUIRE(bw.view() == "Text: |0123456789          |");
  bw.clear().print("Text: |{:20}|", text.substr(0, 10));
  REQUIRE(bw.view() == "Text: |0123456789          |");
  bw.clear().print("Text: |{:20.10}|", text);
  REQUIRE(bw.view() == "Text: |0123456789          |");
  bw.clear().print("Text: |{0:>20}|", text.substr(0, 10));
  REQUIRE(bw.view() == "Text: |          0123456789|");
  bw.clear().print("Text: |{:>20}|", text.substr(0, 10));
  REQUIRE(bw.view() == "Text: |          0123456789|");
  bw.clear().print("Text: |{0:>20.10}|", text);
  REQUIRE(bw.view() == "Text: |          0123456789|");
  bw.clear().print("Text: |{0:->20}|", text.substr(9, 11));
  REQUIRE(bw.view() == "Text: |---------9abcdefghij|");
  bw.clear().print("Text: |{0:->20.11}|", text.substr(9));
  REQUIRE(bw.view() == "Text: |---------9abcdefghij|");
  bw.clear().print("Text: |{0:-<,20}|", text.substr(52, 10));
  REQUIRE(bw.view() == "Text: |QRSTUVWXYZ|");
}

namespace {
static constexpr std::string_view NA{"N/A"};

// Define some global generators

BufferWriter &
BWF_Timestamp(BufferWriter &w, Spec const &spec) {
  auto now   = std::chrono::system_clock::now();
  auto epoch = std::chrono::system_clock::to_time_t(now);
  LocalBufferWriter<48> lw;

  ctime_r(&epoch, lw.aux_data());
  lw.commit(19); // take only the prefix.
  lw.print(".{:03}", std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count() % 1000);
  bwformat(w, spec, lw.view().substr(4));
  return w;
}

BufferWriter &
BWF_Now(BufferWriter &w, Spec const &spec) {
  return swoc::bwf::Format_Integer(w, spec, std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()), false);
}

BufferWriter &
BWF_Version(BufferWriter &w, Spec const &spec) {
  return bwformat(w, spec, VERSION);
}

BufferWriter &
BWF_EvilDave(BufferWriter &w, Spec const &spec) {
  return bwformat(w, spec, "Evil Dave");
}

// Context object for several context name binding examples.
// Hardwired for example, production coode would load values from runtime activity.
struct Context {
  using Fields = std::unordered_map<std::string_view, std::string_view>;
  std::string url{"http://docs.solidwallofcode.com/libswoc/index.html?sureness=outofbounds"};
  std::string_view host{"docs.solidwallofcode.com"};
  std::string_view path{"/libswoc/index.html"};
  std::string_view scheme{"http"};
  std::string_view query{"sureness=outofbounds"};
  std::string tls_version{"tls/1.2"};
  std::string ip_family{"ipv4"};
  std::string ip_remote{"172.99.80.70"};
  Fields http_fields = {
    {{"Host", "docs.solidwallofcode.com"},
     {"YRP", "10.28.56.112"},
     {"Connection", "keep-alive"},
     {"Age", "956"},
     {"ETag", "1337beef"}}
  };
  static inline std::string A{"A"};
  static inline std::string alpha{"alpha"};
  static inline std::string B{"B"};
  static inline std::string bravo{"bravo"};
  Fields cookie_fields = {
    {{A, alpha}, {B, bravo}}
  };
};

} // namespace

void
EX_BWF_Format_Init() {
  swoc::bwf::Global_Names.assign("timestamp", &BWF_Timestamp);
  swoc::bwf::Global_Names.assign("now", &BWF_Now);
  swoc::bwf::Global_Names.assign("version", &BWF_Version);
  swoc::bwf::Global_Names.assign("dave", &BWF_EvilDave);
}

// Work with external / global names.
TEST_CASE("BufferWriter Example", "[bufferwriter][example]") {
  LocalBufferWriter<256> w;

  w.clear();
  w.print("{timestamp} Test Started");
  REQUIRE(w.view().substr(20) == "Test Started");
  w.clear();
  w.print("Time is {now} {now:x} {now:X} {now:#x}");
  REQUIRE(w.size() > 12);
}

TEST_CASE("BufferWriter Context Simple", "[bufferwriter][example][context]") {
  // Container for name bindings.
  using CookieBinding = swoc::bwf::ContextNames<Context const>;

  LocalBufferWriter<1024> w;

  Context CTX;

  // Generators.

  auto field_gen = [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & {
    if (auto spot = ctx.http_fields.find(spec._ext); spot != ctx.http_fields.end()) {
      bwformat(w, spec, spot->second);
    } else {
      bwformat(w, spec, NA);
    }
    return w;
  };

  auto cookie_gen = [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & {
    if (auto spot = ctx.cookie_fields.find(spec._ext); spot != ctx.cookie_fields.end()) {
      bwformat(w, spec, spot->second);
    } else {
      bwformat(w, spec, NA);
    }
    return w;
  };

  // Hook up the generators.
  CookieBinding cb;
  cb.assign("field", field_gen);
  cb.assign("cookie", cookie_gen);
  cb.assign("url",
            [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.url); });
  cb.assign("scheme",
            [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.scheme); });
  cb.assign("host",
            [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.host); });
  cb.assign("path",
            [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.path); });

  w.print_n(cb.bind(CTX), TextView{"YRP is {field::YRP}, Cookie B is {cookie::B}."});
  REQUIRE(w.view() == "YRP is 10.28.56.112, Cookie B is bravo.");
  w.clear();
  w.print_n(cb.bind(CTX), "{scheme}://{host}{path}");
  REQUIRE(w.view() == "http://docs.solidwallofcode.com/libswoc/index.html");
  w.clear();
  w.print_n(cb.bind(CTX), "Potzrebie is {field::potzrebie}");
  REQUIRE(w.view() == "Potzrebie is N/A");
};

TEST_CASE("BufferWriter Context 2", "[bufferwriter][example][context]") {
  LocalBufferWriter<1024> w;

  // Add field based access as methods to the base context.
  struct ExContext : public Context {
    void
    field_gen(BufferWriter &w, Spec const &spec, TextView const &field) const {
      if (auto spot = http_fields.find(field); spot != http_fields.end()) {
        bwformat(w, spec, spot->second);
      } else {
        bwformat(w, spec, NA);
      }
    };

    void
    cookie_gen(BufferWriter &w, Spec const &spec, TextView const &tag) const {
      if (auto spot = cookie_fields.find(tag); spot != cookie_fields.end()) {
        bwformat(w, spec, spot->second);
      } else {
        bwformat(w, spec, NA);
      }
    };

  } CTX;

  // Container for name bindings.
  // Override the name lookup to handle structured names.
  class CookieBinding : public swoc::bwf::ContextNames<ExContext const> {
    using super_type = swoc::bwf::ContextNames<ExContext const>;

  public:
    // Intercept name dispatch to check for structured names and handle those. If not structured,
    // chain up to super class to dispatch normally.
    BufferWriter &
    operator()(BufferWriter &w, Spec const &spec, ExContext const &ctx) const override {
      // Structured name prefixes.
      static constexpr TextView FIELD_TAG{"field"};
      static constexpr TextView COOKIE_TAG{"cookie"};

      TextView name{spec._name};
      TextView key = name.split_prefix_at('.');
      if (key == FIELD_TAG) {
        ctx.field_gen(w, spec, name);
      } else if (key == COOKIE_TAG) {
        ctx.cookie_gen(w, spec, name);
      } else if (!key.empty()) {
        // error case - unrecognized prefix
        w.print("!{}!", name);
      } else { // direct name, do normal dispatch.
        this->super_type::operator()(w, spec, ctx);
      }
      return w;
    }
  };

  // Hook up the generators.
  CookieBinding cb;
  cb.assign("url",
            [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.url); });
  cb.assign("scheme",
            [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.scheme); });
  cb.assign("host",
            [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.host); });
  cb.assign("path",
            [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.path); });
  cb.assign("version", BWF_Version);

  w.print_n(cb.bind(CTX), "B cookie is {cookie.B}");
  REQUIRE(w.view() == "B cookie is bravo");
  w.clear();
  w.print_n(cb.bind(CTX), "{scheme}://{host}{path}");
  REQUIRE(w.view() == "http://docs.solidwallofcode.com/libswoc/index.html");
  w.clear();
  w.print_n(cb.bind(CTX), "Version is {version}");
  REQUIRE(w.view() == "Version is 1.0.2");
  w.clear();
  w.print_n(cb.bind(CTX), "Potzrebie is {field.potzrebie}");
  REQUIRE(w.view() == "Potzrebie is N/A");
  w.clear();
  w.print_n(cb.bind(CTX), "Align: |{host:<30}|");
  REQUIRE(w.view() == "Align: |docs.solidwallofcode.com      |");
  w.clear();
  w.print_n(cb.bind(CTX), "Align: |{host:>30}|");
  REQUIRE(w.view() == "Align: |      docs.solidwallofcode.com|");
};

namespace {
// Alternate format string parsing.
// This is the extractor, an instance of which is passed to the formatting logic.
struct AltFormatEx {
  // Construct using @a fmt as the format string.
  AltFormatEx(TextView fmt);

  // Check for remaining text to parse.
  explicit operator bool() const;
  // Extract the next literal and/or specifier.
  bool operator()(std::string_view &literal, swoc::bwf::Spec &spec);
  // This holds the format string being parsed.
  TextView _fmt;
};

// Construct by copying a view of the format string.
AltFormatEx::AltFormatEx(TextView fmt) : _fmt{fmt} {}

// The extractor is empty if the format string is empty.
AltFormatEx::operator bool() const {
  return !_fmt.empty();
}

bool
AltFormatEx::operator()(std::string_view &literal, swoc::bwf::Spec &spec) {
  if (_fmt.size()) { // data left.
    literal = _fmt.take_prefix_at('%');
    if (_fmt.empty()) { // no '%' found, it's all literal, we're done.
      return false;
    }

    if (_fmt.size() >= 1) { // Something left that's a potential specifier.
      char c = _fmt[0];
      if (c == '%') { // %% -> not a specifier, slap the leading % on the literal, skip the trailing.
        literal = {literal.data(), literal.size() + 1};
        ++_fmt;
      } else if (c == '{') {
        ++_fmt; // drop open brace.
        auto style = _fmt.split_prefix_at('}');
        if (style.empty()) {
          throw std::invalid_argument("Unclosed open brace");
        }
        spec.parse(style);        // stuff between the braces
        if (spec._name.empty()) { // no format args, must have a name to be useable.
          throw std::invalid_argument("No name in specifier");
        }
        // Check for structured name - put the tag in _name and the value in _ext if found.
        TextView name{spec._name};
        auto key = name.split_prefix_at('.');
        if (key) {
          spec._ext  = name;
          spec._name = key;
        }
        return true;
      }
    }
  }
  return false;
}

} // namespace

TEST_CASE("bwf alternate syntax", "[libswoc][bwf][alternate]") {
  using BW       = BufferWriter;
  using AltNames = swoc::bwf::ContextNames<Context>;
  AltNames names;
  Context CTX;
  LocalBufferWriter<256> w;

  names.assign("tls", [](BW &w, Spec const &spec, Context &ctx) -> BW & { return ::swoc::bwformat(w, spec, ctx.tls_version); });
  names.assign("proto", [](BW &w, Spec const &spec, Context &ctx) -> BW & { return ::swoc::bwformat(w, spec, ctx.ip_family); });
  names.assign("chi", [](BW &w, Spec const &spec, Context &ctx) -> BW & { return ::swoc::bwformat(w, spec, ctx.ip_remote); });
  names.assign("url",
               [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.url); });
  names.assign("scheme", [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & {
    return bwformat(w, spec, ctx.scheme);
  });
  names.assign("host",
               [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.host); });
  names.assign("path",
               [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & { return bwformat(w, spec, ctx.path); });

  names.assign("field", [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & {
    if (auto spot = ctx.http_fields.find(spec._ext); spot != ctx.http_fields.end()) {
      bwformat(w, spec, spot->second);
    } else {
      bwformat(w, spec, NA);
    }
    return w;
  });

  names.assign("cookie", [](BufferWriter &w, Spec const &spec, Context const &ctx) -> BufferWriter & {
    if (auto spot = ctx.cookie_fields.find(spec._ext); spot != ctx.cookie_fields.end()) {
      bwformat(w, spec, spot->second);
    } else {
      bwformat(w, spec, NA);
    }
    return w;
  });

  names.assign("dave", &BWF_EvilDave);

  w.print_nfv(names.bind(CTX), AltFormatEx("This is chi - %{chi}"));
  REQUIRE(w.view() == "This is chi - 172.99.80.70");
  w.clear().print_nfv(names.bind(CTX), AltFormatEx("Use %% for a single"));
  REQUIRE(w.view() == "Use % for a single");
  w.clear().print_nfv(names.bind(CTX), AltFormatEx("Use %%{proto} for %{proto}, dig?"));
  REQUIRE(w.view() == "Use %{proto} for ipv4, dig?");
  w.clear().print_nfv(names.bind(CTX), AltFormatEx("Width |%{proto:10}| dig?"));
  REQUIRE(w.view() == "Width |ipv4      | dig?");
  w.clear().print_nfv(names.bind(CTX), AltFormatEx("Width |%{proto:>10}| dig?"));
  REQUIRE(w.view() == "Width |      ipv4| dig?");
  w.clear().print_nfv(names.bind(CTX), AltFormatEx("I hear %{dave} wants to see YRP=%{field.YRP} and cookie A is %{cookie.A}"));
  REQUIRE(w.view() == "I hear Evil Dave wants to see YRP=10.28.56.112 and cookie A is alpha");
}

/** C / printf style formatting for BufferWriter.
 *
 * This is a wrapper style class, it is not for use in a persistent context. The general use pattern
 * will be to pass a temporary instance in to the @c BufferWriter formatting. E.g
 *
 * @code
 * void bwprintf(BufferWriter& w, TextView fmt, arg1, arg2, arg3, ...) {
 *   w.print_v(C_Format(fmt), std::forward_as_tuple(args));
 * @endcode
 */
class C_Format {
public:
  /// Construct for @a fmt.
  C_Format(TextView const &fmt);

  /// Check if there is any more format to process.
  explicit operator bool() const;

  /// Get the next pieces of the format.
  bool operator()(std::string_view &literal, Spec &spec);

  /// Capture an argument use as a specifier value.
  void capture(BufferWriter &w, Spec const &spec, std::any const &value);

protected:
  TextView _fmt;        // The format string.
  Spec _saved;          // spec for which the width and/or prec is needed.
  bool _saved_p{false}; // flag for having a saved _spec.
  bool _prec_p{false};  // need the precision captured?
};
// class C_Format

// ---- Implementation ----
inline C_Format::C_Format(TextView const &fmt) : _fmt(fmt) {}

// C_Format operator bool
inline C_Format::operator bool() const {
  return _saved_p || !_fmt.empty();
}
// C_Format operator bool

// C_Format capture
void
C_Format::capture(BufferWriter &, Spec const &spec, std::any const &value) {
  unsigned v;
  if (typeid(int *) == value.type())
    v = static_cast<unsigned>(*std::any_cast<int *>(value));
  else if (typeid(unsigned *) == value.type())
    v = *std::any_cast<unsigned *>(value);
  else if (typeid(size_t *) == value.type())
    v = static_cast<unsigned>(*std::any_cast<size_t *>(value));
  else
    return;

  if (spec._ext == "w")
    _saved._min = v;
  if (spec._ext == "p") {
    _saved._prec = v;
  }
}
// C_Format capture

// C_Format parsing
bool
C_Format::operator()(std::string_view &literal, Spec &spec) {
  TextView parsed;

  // clean up any old business from a previous specifier.
  if (_prec_p) {
    spec._type = Spec::CAPTURE_TYPE;
    spec._ext  = "p";
    _prec_p    = false;
    return true;
  } else if (_saved_p) {
    spec     = _saved;
    _saved_p = false;
    return true;
  }

  if (!_fmt.empty()) {
    bool width_p = false;
    literal      = _fmt.take_prefix_at('%');
    if (_fmt.empty()) {
      return false;
    }
    if (!_fmt.empty()) {
      if ('%' == *_fmt) {
        literal = {literal.data(), literal.size() + 1};
        ++_fmt;
        return false;
      }
    }

    spec._align = Spec::Align::RIGHT; // default unless overridden.
    do {
      char c = *_fmt;
      if ('-' == c) {
        spec._align = Spec::Align::LEFT;
      } else if ('+' == c) {
        spec._sign = Spec::SIGN_ALWAYS;
      } else if (' ' == c) {
        spec._sign = Spec::SIGN_NEVER;
      } else if ('#' == c) {
        spec._radix_lead_p = true;
      } else if ('0' == c) {
        spec._fill = '0';
      } else {
        break;
      }
      ++_fmt;
    } while (!_fmt.empty());

    if (_fmt.empty()) {
      literal = TextView{literal.data(), _fmt.data()};
      return false;
    }

    if ('*' == *_fmt) {
      width_p = true; // signal need to capture width.
      ++_fmt;
    } else {
      auto size      = _fmt.size();
      unsigned width = swoc::svto_radix<10>(_fmt);
      if (size != _fmt.size()) {
        spec._min = width;
      }
    }

    if ('.' == *_fmt) {
      ++_fmt;
      if ('*' == *_fmt) {
        _prec_p = true;
        ++_fmt;
      } else {
        auto size  = _fmt.size();
        unsigned x = swoc::svto_radix<10>(_fmt);
        if (size != _fmt.size()) {
          spec._prec = x;
        } else {
          spec._prec = 0;
        }
      }
    }

    if (_fmt.empty()) {
      literal = TextView{literal.data(), _fmt.data()};
      return false;
    }

    char c = *_fmt++;
    // strip length modifiers.
    if ('l' == c || 'h' == c)
      c = *_fmt++;
    if ('l' == c || 'z' == c || 'j' == c || 't' == c || 'h' == c)
      c = *_fmt++;

    switch (c) {
    case 'c':
      spec._type = c;
      break;
    case 'i':
    case 'd':
    case 'j':
    case 'z':
      spec._type = 'd';
      break;
    case 'x':
    case 'X':
      spec._type = c;
      break;
    case 'f':
      spec._type = 'f';
      break;
    case 's':
      spec._type = 's';
      break;
    case 'p':
      spec._type = c;
      break;
    default:
      literal = TextView{literal.data(), _fmt.data()};
      return false;
    }
    if (width_p || _prec_p) {
      _saved_p = true;
      _saved   = spec;
      spec     = Spec::DEFAULT;
      if (width_p) {
        spec._type = Spec::CAPTURE_TYPE;
        spec._ext  = "w";
      } else if (_prec_p) {
        _prec_p    = false;
        spec._type = Spec::CAPTURE_TYPE;
        spec._ext  = "p";
      }
    }
    return true;
  }
  return false;
}

namespace {
template <typename... Args>
int
bwprintf(BufferWriter &w, TextView const &fmt, Args &&...args) {
  size_t n = w.size();
  w.print_nfv(swoc::bwf::NilBinding(), C_Format(fmt), swoc::bwf::ArgTuple{std::forward_as_tuple(args...)});
  return static_cast<int>(w.size() - n);
}

} // namespace

TEST_CASE("bwf printf", "[libswoc][bwf][printf]") {
  // C_Format tests
  LocalBufferWriter<256> w;

  bwprintf(w.clear(), "Fifty Six = %d", 56);
  REQUIRE(w.view() == "Fifty Six = 56");
  bwprintf(w.clear(), "int is %i", 101);
  REQUIRE(w.view() == "int is 101");
  bwprintf(w.clear(), "int is %zd", 102);
  REQUIRE(w.view() == "int is 102");
  bwprintf(w.clear(), "int is %ld", 103);
  REQUIRE(w.view() == "int is 103");
  bwprintf(w.clear(), "int is %s", 104);
  REQUIRE(w.view() == "int is 104");
  bwprintf(w.clear(), "int is %ld", -105);
  REQUIRE(w.view() == "int is -105");

  TextView digits{"0123456789"};
  bwprintf(w.clear(), "Chars |%*s|", 12, digits);
  REQUIRE(w.view() == "Chars |  0123456789|");
  bwprintf(w.clear(), "Chars %.*s", 4, digits);
  REQUIRE(w.view() == "Chars 0123");
  bwprintf(w.clear(), "Chars |%*.*s|", 12, 5, digits);
  REQUIRE(w.view() == "Chars |       01234|");
  // C_Format tests
}

// --- Format classes

struct As_Rot13 {
  std::string_view _src;

  As_Rot13(std::string_view src) : _src{src} {}
};

BufferWriter &
bwformat(BufferWriter &w, Spec const &spec, As_Rot13 const &wrap) {
  static constexpr auto rot13 = [](char c) -> char {
    return islower(c) ? (c + 13 - 'a') % 26 + 'a' : isupper(c) ? (c + 13 - 'A') % 26 + 'A' : c;
  };
  return bwformat(w, spec, swoc::transform_view_of(rot13, wrap._src));
}

As_Rot13
Rotter(std::string_view const &sv) {
  return As_Rot13(sv);
}

struct Thing {
  std::string _name;
  unsigned _n{0};
};

As_Rot13
Rotter(Thing const &thing) {
  return As_Rot13(thing._name);
}

TEST_CASE("bwf wrapper", "[libswoc][bwf][wrapper]") {
  LocalBufferWriter<256> w;
  std::string_view s1{"Frcvqru"};

  w.clear().print("Rot {}.", As_Rot13{s1});
  REQUIRE(w.view() == "Rot Sepideh.");

  w.clear().print("Rot {}.", As_Rot13(s1));
  REQUIRE(w.view() == "Rot Sepideh.");

  w.clear().print("Rot {}.", Rotter(s1));
  REQUIRE(w.view() == "Rot Sepideh.");

  Thing thing{"Rivy Qnir", 20};
  w.clear().print("Rot {}.", Rotter(thing));
  REQUIRE(w.view() == "Rot Evil Dave.");

  // Verify symmetry.
  w.clear().print("Rot {}.", As_Rot13("Sepideh"));
  REQUIRE(w.view() == "Rot Frcvqru.");
};
