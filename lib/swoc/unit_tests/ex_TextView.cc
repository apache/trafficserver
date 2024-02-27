// SPDX-License-Identifier: Apache-2.0
/** @file

    TextView example code.

    This code is run during unit tests to verify that it compiles and runs correctly, but the primary
    purpose of the code is for documentation, not testing per se. This means editing the file is
    almost certain to require updating documentation references to code in this file.
*/

#include <array>
#include <functional>
#include "swoc/swoc_file.h"

#include "swoc/TextView.h"
#include "catch.hpp"

using swoc::TextView;
using namespace swoc::literals;

// CSV parsing.
namespace {
// Standard results array so these names can be used repeatedly.
std::array<TextView, 6> alphabet{
  {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot"}
};

// -- doc csv start
void
parse_csv(TextView src, std::function<void(TextView)> const &f) {
  while (src.ltrim_if(&isspace)) {
    TextView token{src.take_prefix_at(',').rtrim_if(&isspace)};
    if (token) { // skip empty tokens (double separators)
      f(token);
    }
  }
}
// -- doc csv end

// -- doc csv non-empty start
void
parse_csv_non_empty(TextView src, std::function<void(TextView)> const &f) {
  TextView token;
  while ((token = src.take_prefix_at(',').trim_if(&isspace))) {
    f(token);
  }
}
// -- doc csv non-empty end

// -- doc kv start
void
parse_kw(TextView src, std::function<void(TextView, TextView)> const &f) {
  while (src) {
    TextView value{src.take_prefix_at(',').trim_if(&isspace)};
    if (value) {
      TextView key{value.take_prefix_at('=')};
      // Trim any space that might have been around the '='.
      f(key.rtrim_if(&isspace), value.ltrim_if(&isspace));
    }
  }
}
// -- doc kv end

} // namespace

TEST_CASE("TextView Example CSV", "[libswoc][example][textview][csv]") {
  char const *src           = "alpha,bravo,  charlie,delta  ,  echo  ,, ,foxtrot";
  char const *src_non_empty = "alpha,bravo,  charlie,   delta, echo  ,foxtrot";
  int idx                   = 0;
  parse_csv(src, [&](TextView tv) -> void { REQUIRE(tv == alphabet[idx++]); });
  idx = 0;
  parse_csv_non_empty(src_non_empty, [&](TextView tv) -> void { REQUIRE(tv == alphabet[idx++]); });
};

TEST_CASE("TextView Example KW", "[libswoc][example][textview][kw]") {
  TextView src{"alpha=1, bravo= 2,charlie = 3,  delta =4  ,echo ,, ,foxtrot=6"};
  size_t idx = 0;
  parse_kw(src, [&](TextView key, TextView value) -> void {
    REQUIRE(key == alphabet[idx++]);
    if (idx == 5) {
      REQUIRE(!value);
    } else {
      REQUIRE(svtou(value) == idx);
    }
  });
};

// Example: streaming token parsing, with quote stripping.

TEST_CASE("TextView Tokens", "[libswoc][example][textview][tokens]") {
  auto tokenizer = [](TextView &src, char sep, bool strip_quotes_p = true) -> TextView {
    TextView::size_type idx = 0;
    // Characters of interest in a null terminated string.
    char sep_list[3] = {'"', sep, 0};
    bool in_quote_p  = false;
    while (idx < src.size()) {
      // Next character of interest.
      idx = src.find_first_of(sep_list, idx);
      if (TextView::npos == idx) {
        // no more, consume all of @a src.
        break;
      } else if ('"' == src[idx]) {
        // quote, skip it and flip the quote state.
        in_quote_p = !in_quote_p;
        ++idx;
      } else if (sep == src[idx]) { // separator.
        if (in_quote_p) {
          // quoted separator, skip and continue.
          ++idx;
        } else {
          // found token, finish up.
          break;
        }
      }
    }

    // clip the token from @a src and trim whitespace.
    auto zret = src.take_prefix(idx).trim_if(&isspace);
    if (strip_quotes_p) {
      zret.trim('"');
    }
    return zret;
  };

  auto extract_tag = [](TextView src) -> TextView {
    src.trim_if(&isspace);
    if (src.prefix(2) == "W/"_sv) {
      src.remove_prefix(2);
    }
    if (!src.empty() && *src == '"') {
      src = (++src).take_prefix_at('"');
    }
    return src;
  };

  auto match = [&](TextView tag, TextView src, bool strong_p = true) -> bool {
    if (strong_p && tag.prefix(2) == "W/"_sv) {
      return false;
    }
    tag = extract_tag(tag);
    while (src) {
      TextView token{tokenizer(src, ',')};
      if (!strong_p) {
        token = extract_tag(token);
      }
      if (token == tag || token == "*"_sv) {
        return true;
      }
    }
    return false;
  };

  // Basic testing.
  TextView src = "one, two";
  REQUIRE(tokenizer(src, ',') == "one");
  REQUIRE(tokenizer(src, ',') == "two");
  REQUIRE(src.empty());
  src = R"("one, two")"; // quotes around comma.
  REQUIRE(tokenizer(src, ',') == "one, two");
  REQUIRE(src.empty());
  src = R"lol(one, "two" , "a,b  ", some "a,,b" stuff, last)lol";
  REQUIRE(tokenizer(src, ',') == "one");
  REQUIRE(tokenizer(src, ',') == "two");
  REQUIRE(tokenizer(src, ',') == "a,b  ");
  REQUIRE(tokenizer(src, ',') == R"lol(some "a,,b" stuff)lol");
  REQUIRE(tokenizer(src, ',') == "last");
  REQUIRE(src.empty());

  src = R"("one, two)"; // unterminated quote.
  REQUIRE(tokenizer(src, ',') == "one, two");
  REQUIRE(src.empty());

  src = R"lol(one, "two" , "a,b  ", some "a,,b" stuff, last)lol";
  REQUIRE(tokenizer(src, ',', false) == "one");
  REQUIRE(tokenizer(src, ',', false) == R"q("two")q");
  REQUIRE(tokenizer(src, ',', false) == R"q("a,b  ")q");
  REQUIRE(tokenizer(src, ',', false) == R"lol(some "a,,b" stuff)lol");
  REQUIRE(tokenizer(src, ',', false) == "last");
  REQUIRE(src.empty());

  // Test against ETAG like data.
  TextView tag = R"o("TAG956")o";
  src          = R"o("TAG1234", W/"TAG999", "TAG956", "TAG777")o";
  REQUIRE(match(tag, src));
  tag = R"o("TAG599")o";
  REQUIRE(!match(tag, src));
  REQUIRE(match(tag, R"o("*")o"));
  tag = R"o("TAG999")o";
  REQUIRE(!match(tag, src));
  REQUIRE(match(tag, src, false));
  tag = R"o(W/"TAG777")o";
  REQUIRE(!match(tag, src));
  REQUIRE(match(tag, src, false));
  tag = "TAG1234";
  REQUIRE(match(tag, src));
  REQUIRE(!match(tag, {})); // don't crash on empty source list.
  REQUIRE(!match({}, src)); // don't crash on empty tag.
}

// Example: line parsing from a file.

TEST_CASE("TextView Lines", "[libswoc][example][textview][lines]") {
  swoc::file::path path{"doc/conf.py"};
  std::error_code ec;

  auto content   = swoc::file::load(path, ec);
  size_t n_lines = 0;

  TextView src{content};
  while (!src.empty()) {
    auto line = src.take_prefix_at('\n').trim_if(&isspace);
    if (line.empty() || '#' == *line)
      continue;
    ++n_lines;
  }
  // To verify this
  // cat doc/conf.py | grep -v '^ *#' | grep -v '^$' | wc
  REQUIRE(n_lines == 86);
};

#include <set>
#include "swoc/swoc_ip.h"

TEST_CASE("TextView misc", "[libswoc][example][textview][misc]") {
  TextView src = "  alpha.bravo.old:charlie.delta.old  :  echo.foxtrot.old  ";
  REQUIRE("alpha.bravo" == src.take_prefix_at(':').remove_suffix_at('.').ltrim_if(&isspace));
  REQUIRE("charlie.delta" == src.take_prefix_at(':').remove_suffix_at('.').ltrim_if(&isspace));
  REQUIRE("echo.foxtrot" == src.take_prefix_at(':').remove_suffix_at('.').ltrim_if(&isspace));
  REQUIRE(src.empty());
}

TEST_CASE("TextView parsing", "[libswoc][example][text][parsing]") {
  static const std::set<std::string_view> DC_TAGS{"amb", "ata", "aue", "bga", "bra", "cha", "coa", "daa", "dca", "deb", "dnb",
                                                  "esa", "fra", "frb", "hkb", "inc", "ir2", "jpa", "laa", "lob", "mib", "nya",
                                                  "rob", "seb", "sgb", "sja", "swb", "tpb", "twb", "via", "waa"};
  TextView parsed;
  swoc::IP4Addr addr;

  std::error_code ec;
  auto data{swoc::file::load("unit_tests/examples/resolver.txt"_tv, ec)};
  REQUIRE(data.size() > 2); // if this fails, there's something wrong with the path or current directory.

  TextView content{data};
  while (content) {
    auto line{content.take_prefix_at('\n').trim_if(&isspace)}; // get the next line.
    if (line.empty() || *line == '#') {                        // skip empty and lines starting with '#'
      continue;
    }
    auto addr_txt  = line.take_prefix_at(';');
    auto conf_txt  = line.ltrim_if(&isspace).take_prefix_if(&isspace);
    auto dcnum_txt = line.ltrim_if(&isspace).take_prefix_if(&isspace);
    auto dc_txt    = line.ltrim_if(&isspace).take_prefix_if(&isspace);

    // First element must be a valid IPv4 address.
    REQUIRE(addr.load(addr_txt) == true);

    // Confidence value must be an unsigned integer after the '='.
    auto conf_value{conf_txt.split_suffix_at('=')};
    swoc::svtou(conf_value, &parsed);
    REQUIRE(conf_value == parsed); // valid integer

    // Number of elements in @a dc_txt - verify it's an integer.
    auto dcnum_value{dcnum_txt.split_suffix_at('=')};
    auto dc_n = swoc::svtou(dcnum_value, &parsed);
    REQUIRE(dcnum_value == parsed); // valid integer

    // Verify the expected prefix for the DC list.
    static constexpr TextView DC_PREFIX{"dc=["};
    if (!dc_txt.starts_with(DC_PREFIX) || dc_txt.remove_prefix(DC_PREFIX.size()).empty() || dc_txt.back() != ']') {
      continue;
    }

    dc_txt.rtrim("], \t"); // drop trailing brackets, commas, spaces, tabs.
    // walk the comma separated tokens
    unsigned dc_count = 0;
    while (dc_txt) {
      auto key                = dc_txt.take_prefix_at(',');
      auto value              = key.take_suffix_at('=');
      [[maybe_unused]] auto n = swoc::svtou(value, &parsed);
      // Each element must be one of the known tags, followed by '=' and an integer.
      REQUIRE(parsed == value); // value integer.
      REQUIRE(DC_TAGS.find(key) != DC_TAGS.end());
      ++dc_count;
    }
    REQUIRE(dc_count == dc_n);
  };
};
