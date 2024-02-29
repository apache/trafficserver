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

/** Return text that is representative of a file to parse.
 * @return File-like content to parse.
 */
std::string_view
get_resolver_text()
{
  constexpr std::string_view CONTENT = R"END(
# Some comment
172.16.10.10;	conf=45	dcnum=31	dc=[cha=12,dca=30,nya=35,ata=39,daa=41,dnb=56,mib=61,sja=68,laa=69,swb=72,lob=103,fra=109,coa=112,amb=115,ir2=117,deb=122,frb=123,via=128,esa=133,waa=141,seb=141,rob=147,bga=147,bra=169,tpb=217,jpa=218,twb=220,hkb=222,aue=237,inc=240,sgb=245,]
172.16.10.11;	conf=45	dcnum=31	dc=[cha=17,dca=33,daa=38,nya=40,ata=41,mib=53,dnb=53,swb=63,sja=64,laa=69,lob=106,fra=110,coa=110,amb=111,frb=121,deb=122,esa=123,ir2=128,via=132,seb=139,waa=143,rob=144,bga=145,bra=159,tpb=215,hkb=215,twb=219,jpa=219,inc=226,aue=238,sgb=246,]
172.16.10.12;	conf=45	dcnum=31	dc=[cha=19,dca=33,nya=40,daa=41,ata=44,mib=52,dnb=53,sja=65,swb=68,laa=71,fra=104,lob=105,coa=110,amb=114,ir2=118,deb=119,frb=122,esa=127,via=128,seb=135,waa=137,rob=143,bga=145,bra=165,tpb=216,jpa=219,hkb=219,twb=222,inc=228,aue=229,sgb=246,]
# Another comment followed by a blank line.

172.16.10.13;	conf=45	dcnum=31	dc=[cha=16,dca=30,nya=36,daa=41,ata=47,mib=51,dnb=56,swb=66,sja=66,laa=71,lob=103,coa=107,amb=109,fra=112,ir2=117,deb=118,frb=123,esa=132,via=133,waa=136,bga=141,rob=142,seb=144,bra=167,twb=205,tpb=215,jpa=223,hkb=223,aue=230,inc=233,sgb=242,]
172.16.10.14;	conf=45	dcnum=31	dc=[cha=19,dca=31,nya=37,ata=44,daa=46,dnb=47,mib=58,swb=65,sja=66,laa=70,lob=104,fra=109,amb=109,coa=112,frb=120,deb=121,ir2=122,esa=125,via=130,waa=141,rob=143,seb=145,bga=155,bra=170,tpb=219,twb=221,jpa=224,inc=227,hkb=227,aue=236,sgb=242,]
172.16.10.15;	conf=45	dcnum=31	dc=[cha=24,dca=32,nya=37,daa=38,ata=44,dnb=57,mib=64,sja=65,laa=66,swb=68,lob=100,coa=106,fra=112,amb=112,deb=116,ir2=123,esa=124,frb=125,via=128,waa=136,bga=145,rob=148,seb=151,bra=173,twb=206,jpa=217,tpb=227,aue=228,hkb=230,inc=234,sgb=247,]


172.16.11.10;	conf=45	dcnum=31	dc=[cha=23,dca=33,dnb=35,nya=39,ata=39,daa=44,mib=55,sja=63,swb=69,laa=69,lob=107,fra=110,amb=115,frb=116,ir2=121,coa=121,deb=124,esa=125,via=129,waa=141,seb=141,rob=141,bga=141,bra=163,jpa=213,twb=216,hkb=220,tpb=221,inc=221,aue=239,sgb=246,]
172.16.11.11;	conf=45	dcnum=31	dc=[cha=15,dca=31,nya=36,ata=37,daa=40,dnb=50,swb=61,mib=62,sja=66,laa=69,coa=107,fra=109,amb=113,deb=117,lob=119,ir2=122,frb=124,esa=125,via=129,waa=137,seb=141,rob=142,bga=148,bra=162,tpb=211,twb=217,jpa=219,hkb=226,inc=231,sgb=243,aue=245,]
172.16.11.12;	conf=45	dcnum=31	dc=[cha=15,dca=35,nya=36,daa=36,dnb=43,ata=47,mib=50,sja=64,laa=67,swb=69,lob=100,coa=104,amb=113,fra=114,deb=119,ir2=123,frb=123,via=126,esa=129,waa=140,seb=143,bga=148,bra=158,rob=198,jpa=206,twb=209,tpb=217,hkb=217,inc=227,aue=233,sgb=245,]
172.16.11.13;	conf=45	dcnum=31	dc=[cha=16,dca=33,nya=34,dnb=38,daa=43,ata=44,mib=57,swb=67,sja=70,laa=70,lob=103,coa=106,amb=107,fra=113,ir2=114,frb=119,deb=120,via=128,esa=130,waa=138,seb=139,bga=143,rob=145,bra=170,jpa=213,twb=219,tpb=219,hkb=224,inc=235,aue=239,sgb=248,]
172.16.11.14;	conf=45	dcnum=31	dc=[cha=18,dca=31,nya=38,daa=41,ata=42,dnb=47,mib=56,sja=65,swb=68,laa=75,lob=103,fra=109,coa=111,amb=114,frb=118,ir2=119,deb=126,via=128,esa=132,waa=136,seb=137,rob=146,bga=146,bra=161,tpb=212,jpa=216,twb=222,inc=223,hkb=224,sgb=242,aue=242,]
172.16.11.15;	conf=45	dcnum=31	dc=[cha=23,dca=32,nya=36,ata=37,daa=38,dnb=54,sja=66,swb=67,laa=67,mib=73,amb=107,lob=109,fra=109,deb=115,frb=120,coa=125,ir2=126,esa=134,via=137,seb=137,waa=141,rob=142,bga=156,bra=162,tpb=213,twb=222,jpa=224,hkb=228,aue=230,inc=233,sgb=255,]
172.16.14.10;	conf=45	dcnum=31	dc=[daa=30,ata=38,cha=43,dnb=51,dca=51,mib=54,laa=57,sja=58,nya=60,swb=69,coa=106,lob=127,fra=129,amb=133,ir2=134,deb=143,frb=146,esa=150,via=153,seb=163,rob=165,bga=165,bra=168,waa=169,tpb=204,jpa=207,aue=208,twb=213,hkb=223,sgb=239,inc=271,]
172.16.14.11;	conf=45	dcnum=31	dc=[daa=24,ata=40,cha=45,dnb=47,laa=55,mib=56,dca=56,nya=57,sja=67,swb=73,coa=111,lob=125,amb=133,ir2=138,fra=140,frb=145,deb=147,via=153,esa=155,waa=157,seb=158,bga=166,bra=171,rob=172,tpb=209,twb=213,jpa=218,hkb=218,aue=223,sgb=243,inc=270,]
172.16.14.12;	conf=45	dcnum=31	dc=[daa=33,cha=44,dnb=46,ata=48,mib=54,dca=55,nya=56,laa=56,sja=64,swb=72,coa=119,lob=127,amb=132,fra=133,ir2=137,deb=139,frb=140,esa=150,via=154,waa=159,seb=164,bga=168,rob=170,bra=170,jpa=209,twb=212,tpb=212,aue=212,hkb=220,sgb=243,inc=269,]
172.16.14.13;	conf=45	dcnum=31	dc=[daa=31,cha=43,ata=43,dca=50,mib=52,laa=54,nya=60,sja=61,dnb=61,swb=85,coa=113,lob=127,amb=134,fra=135,ir2=138,deb=144,esa=145,frb=150,waa=156,via=156,seb=166,bga=168,rob=172,bra=174,twb=208,aue=209,hkb=214,jpa=215,tpb=218,sgb=242,inc=271,]

# Some more comments.
# And a blank line at the end.

)END";

  return CONTENT;
}

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
  auto const content   = get_resolver_text();
  size_t n_lines = 0;

  TextView src{content};
  while (!src.empty()) {
    auto line = src.take_prefix_at('\n').trim_if(&isspace);
    if (line.empty() || '#' == *line) {
      continue;
    }
    ++n_lines;
  }
  // To verify this
  // grep -v '^$' lib/swoc/unit_tests/examples/resolver.txt | grep -v '^ *#' |  wc
  REQUIRE(n_lines == 16);
};

#include <set>
#include "swoc/swoc_ip.h"

TEST_CASE("TextView misc", "[libswoc][example][textview][misc]") {
  auto src = "  alpha.bravo.old:charlie.delta.old  :  echo.foxtrot.old  "_tv;
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

  auto const data = get_resolver_text();
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
