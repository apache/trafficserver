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

#include <tscore/ink_assert.h>
#include <tscore/ink_align.h>

#include <LogUtils.h>

#include "test_LogUtils.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <cstring>

using namespace LogUtils;

namespace
{
void
test(const MIMEField *pairs, int numPairs, const char *asciiResult, int extraUnmarshalSpace = 0)
{
  char binBuf[1500], asciiBuf[1500];

  MIMEHdr hdr{pairs, numPairs};

  int binAlignSize = marshalMimeHdr(numPairs ? &hdr : nullptr, nullptr);

  REQUIRE(binAlignSize < sizeof(binBuf));

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

  int asciiSize = unmarshalMimeHdr(&bp, asciiBuf, std::strlen(asciiResult) + extraUnmarshalSpace);

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

#include <cstdlib>
#include <iostream>

void
_ink_assert(const char *a, const char *f, int line)
{
  std::cout << a << '\n' << f << '\n' << line << '\n';

  std::exit(1);
}

void
RecSignalManager(int, char const *, std::size_t)
{
}
