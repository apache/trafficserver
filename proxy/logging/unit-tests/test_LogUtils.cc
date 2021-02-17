/** @file

  Catch-based tests for LogUtils.h.

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

#include <string_view>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <cstdio>

#include <tscore/ink_assert.h>
#include <tscore/ink_align.h>

#include <LogUtils.h>

#include "unit-tests/test_LogUtils.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace LogUtils;

namespace
{
void
test(const MIMEField *pairs, int numPairs, const char *asciiResult, int extraUnmarshalSpace = 0)
{
  char binBuf[1500], asciiBuf[1500];

  MIMEHdr hdr{pairs, numPairs};

  int binAlignSize = marshalMimeHdr(numPairs ? &hdr : nullptr, nullptr);

  REQUIRE(binAlignSize < static_cast<int>(sizeof(binBuf)));

  hdr.reset();

  REQUIRE(marshalMimeHdr(numPairs ? &hdr : nullptr, binBuf) == binAlignSize);

  int binSize{1};

  if (binBuf[0]) {
    for (; binBuf[binSize] or binBuf[binSize + 1]; ++binSize) {
    }

    binSize += 2;

  } else {
    binSize = 1;
  }

  REQUIRE(INK_ALIGN_DEFAULT(binSize) == binAlignSize);

  char *bp = binBuf;

  unsigned int asciiSize = unmarshalMimeHdr(&bp, asciiBuf, std::strlen(asciiResult) + extraUnmarshalSpace);

  REQUIRE(asciiSize == std::strlen(asciiResult));

  REQUIRE((bp - binBuf) == binAlignSize);

  REQUIRE(std::memcmp(asciiBuf, asciiResult, asciiSize) == 0);
}

} // namespace

TEST_CASE("LogUtilsHttp", "[LUHP]")
{
#define X "12345678"
#define X2 X X
#define X3 X2 X2
#define X4 X3 X3
#define X5 X4 X4
#define X6 X5 X5
#define X7 X6 X6
#define X8 X7 X7

  const MIMEField pairs[] = {{"Argh", "Ugh"}, {"Argh2", "UghUgh"}, {"alltogethernow", X8}};

  test(pairs, 1, "{{{Argh}:{Ugh}}}");
  test(pairs, 2, "{{{Argh}:{Ugh}}{{Argh2}:{UghUgh}}}");
  test(pairs, 2, "{{{Argh}:{Ugh}}{{Argh2}:{Ug...}}}");
  test(pairs, 2, "{{{Argh}:{Ugh}}{{Argh2}:{U...}}}");
  test(pairs, 2, "{{{Argh}:{Ugh}}{{Argh2}:{...}}}");
  test(pairs, 2, "{{{Argh}:{Ugh}}}");
  test(pairs, 2, "{{{Argh}:{Ugh}}}", 1);
  test(pairs, 2, "{{{Argh}:{Ugh}}}", sizeof("{{Argh2}:{...}}") - 2);
  test(pairs, 3, "{{{Argh}:{Ugh}}{{Argh2}:{UghUgh}}{{alltogethernow}:{" X8 "}}}");

  test(pairs, 3, "{{{Argh}:{Ugh}}{{Argh2}:{UghUgh}}}");
  test(pairs, 3, "{{{Argh}:{Ugh}}{{Argh2}:{Ug...}}}");
  test(pairs, 3, "{{{Argh}:{Ugh}}{{Argh2}:{U...}}}");
  test(pairs, 3, "{{{Argh}:{Ugh}}{{Argh2}:{...}}}");
  test(pairs, 3, "{{{Argh}:{Ugh}}}");
  test(pairs, 3, "{{{Argh}:{Ugh}}}", 1);
  test(pairs, 3, "{{{Argh}:{Ugh}}}", sizeof("{{Argh2}:{...}}") - 2);

  test(nullptr, 0, "{}");
  test(nullptr, 0, "");
  test(nullptr, 0, "", 1);
}

void
_ink_assert(const char *a, const char *f, int line)
{
  std::cout << a << '\n' << f << '\n' << line << '\n';

  std::exit(1);
}

void
RecSignalManager(int, char const *, std::size_t)
{
  ink_release_assert(false);
}

TEST_CASE("get_unrolled_filename parses possible log files as expected", "[get_unrolled_filename]")
{
  // Rolled log inputs.
  constexpr ts::TextView with_underscore = "squid.log_some.hostname.com.20191029.18h15m02s-20191029.18h30m02s.old";
  REQUIRE(get_unrolled_filename(with_underscore) == "squid.log");

  constexpr ts::TextView without_underscore = "diags.log.20191114.21h43m16s-20191114.21h43m17s.old";
  REQUIRE(get_unrolled_filename(without_underscore) == "diags.log");

  constexpr ts::TextView dot_file = ".log.20191114.21h43m16s-20191114.21h43m17s.old";
  // Maybe strange, but why not?
  REQUIRE(get_unrolled_filename(dot_file) == ".log");

  // Non-rolled log inputs.
  REQUIRE(get_unrolled_filename("") == "");

  constexpr ts::TextView not_a_log = "logging.yaml";
  REQUIRE(get_unrolled_filename(not_a_log) == not_a_log);

  constexpr ts::TextView no_dot = "logging_yaml";
  REQUIRE(get_unrolled_filename(no_dot) == no_dot);
}

TEST_CASE("LogUtils pure escapify url", "[pure_esc_url]")
{
  char input[][32] = {
    " ",
    "%",
    "% ",
    "%20",
  };
  const char *expected[] = {
    "%20",
    "%25",
    "%25%20",
    "%2520",
  };
  char output[128];
  int output_len;

  int n = sizeof(input) / sizeof(input[0]);
  for (int i = 0; i < n; ++i) {
    LogUtils::pure_escapify_url(NULL, input[i], std::strlen(input[i]), &output_len, output, 128);
    CHECK(std::string_view(output) == expected[i]);
  }
}

TEST_CASE("LogUtils escapify url", "[esc_url]")
{
  char input[][32] = {
    " ",
    "%",
    "% ",
    "%20",
  };
  const char *expected[] = {
    "%20",
    "%25",
    "%25%20",
    "%20",
  };
  char output[128];
  int output_len;

  int n = sizeof(input) / sizeof(input[0]);
  for (int i = 0; i < n; ++i) {
    LogUtils::escapify_url(NULL, input[i], std::strlen(input[i]), &output_len, output, 128);
    CHECK(std::string_view(output) == expected[i]);
  }
}
