/** @file

    Tokenizer tests.

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

#include "tscore/Tokenizer.h"

#include <cstdio>
#include <cstring>

#include <catch.hpp>

TEST_CASE("Tokenizer", "[libts][Tokenizer]")
{
  Tokenizer remap(" \t");

  const char *line = "map https://abc.com https://abc.com @plugin=conf_remap.so @pparam=proxy.config.abc='ABC DEF'";

  const char *toks[] = {"map", "https://abc.com", "https://abc.com", "@plugin=conf_remap.so", "@pparam=proxy.config.abc='ABC DEF'"};

  unsigned count = remap.Initialize(const_cast<char *>(line), (COPY_TOKS | ALLOW_SPACES));

  if (count != 5) {
    std::printf("check that we parsed 5 tokens\n");
    CHECK(false);
  }
  if (count != remap.count()) {
    std::printf("parsed %u tokens, but now we have %u tokens\n", count, remap.count());
    CHECK(false);
  }

  for (unsigned i = 0; i < count; ++i) {
    if (std::strcmp(remap[i], toks[i]) != 0) {
      std::printf("expected token %u to be '%s' but found '%s'\n", count, toks[i], remap[i]);
      CHECK(false);
    }
  }
}
