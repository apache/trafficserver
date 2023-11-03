/** @file

    Catch-based unit tests for MIOBufferWriter class.

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

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <cstdint>

struct IOBufferBlock {
  std::int64_t write_avail();

  char *end();

  void fill(int64_t);
};

struct MIOBuffer {
  IOBufferBlock *first_write_block();

  void add_block();
};

#define UNIT_TEST_BUFFER_WRITER
#include "iocore/eventsystem/MIOBufferWriter.h"
#include "../MIOBufferWriter.cc"

IOBufferBlock iobb[1];
unsigned int iobbIdx{0};

const unsigned int BlockSize = 11 * 11;
char block[BlockSize];
unsigned int blockUsed{0};

std::int64_t
IOBufferBlock::write_avail()
{
  REQUIRE(this == (iobb + iobbIdx));
  return BlockSize - blockUsed;
}

char *
IOBufferBlock::end()
{
  REQUIRE(this == (iobb + iobbIdx));
  return block + blockUsed;
}

void
IOBufferBlock::fill(int64_t len)
{
  static std::uint8_t dataCheck;

  REQUIRE(this == (iobb + iobbIdx));

  while (len-- and (blockUsed < BlockSize)) {
    REQUIRE(block[blockUsed] == static_cast<char>(dataCheck));

    ++blockUsed;

    dataCheck += 7;
  }

  REQUIRE(len == -1);
}

MIOBuffer theMIOBuffer;

IOBufferBlock *
MIOBuffer::first_write_block()
{
  REQUIRE(this == &theMIOBuffer);

  REQUIRE(blockUsed <= BlockSize);

  if (blockUsed == BlockSize) {
    return nullptr;
  }

  return iobb + iobbIdx;
}

void
MIOBuffer::add_block()
{
  REQUIRE(this == &theMIOBuffer);

  REQUIRE(blockUsed == BlockSize);

  blockUsed = 0;

  ++iobbIdx;
}

std::string
genData(int numBytes)
{
  static std::uint8_t genData;

  std::string s(numBytes, ' ');

  for (int i{0}; i < numBytes; ++i) {
    s[i]     = genData;
    genData += 7;
  }

  return s;
}

void
writeOnce(MIOBufferWriter &bw, std::size_t len)
{
  static bool toggle;

  std::string s{genData(len)};
  swoc::MemSpan src(s);

  if (len == 1) {
    bw.write(s[0]);
  } else if (toggle) {
    auto span = bw.aux_span();

    if (span.size() >= src.size()) {
      memcpy(span, src);
      bw.commit(src.size());
    } else {
      auto trailer = src.clip_suffix(src.size() - span.size());
      memcpy(span, src);
      bw.commit(span.size());
      bw.write(trailer);
    }
  } else {
    bw.write(src);
  }

  toggle = !toggle;

  REQUIRE(bw.aux_span().size() <= BlockSize);
}

class InkAssertExcept
{
};

TEST_CASE("MIOBufferWriter", "[MIOBW]")
{
  MIOBufferWriter bw(&theMIOBuffer);

  REQUIRE(bw.remaining() == BlockSize);

  writeOnce(bw, 0);
  writeOnce(bw, 1);
  writeOnce(bw, 1);
  writeOnce(bw, 1);
  writeOnce(bw, 10);
  writeOnce(bw, 1000);
  writeOnce(bw, 1);
  writeOnce(bw, 0);
  writeOnce(bw, 1);
  writeOnce(bw, 2000);
  writeOnce(bw, 69);
  writeOnce(bw, 666);

  for (int i = 0; i < 3000; i += 13) {
    writeOnce(bw, i);
  }

  writeOnce(bw, 0);
  writeOnce(bw, 1);

  REQUIRE(bw.extent() == ((iobbIdx * BlockSize) + blockUsed));

  REQUIRE_THROWS_AS(bw.commit(bw.remaining() + 1), InkAssertExcept);
  REQUIRE_THROWS_AS(bw.data(), InkAssertExcept);
}

void
_ink_assert(const char *a, const char *f, int l)
{
  throw InkAssertExcept();
}
