/** @file

  Unit tests for the multiplexer chunk decoder.

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

#include "chunk-decoder.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
struct FakeBlock {
  const char *data = nullptr;
  int64_t     size = 0;
  FakeBlock  *next = nullptr;
};

FakeBlock *first_block  = nullptr;
int64_t    reader_avail = 0;
int64_t    consumed     = 0;

TSIOBufferBlock
as_block(FakeBlock *block)
{
  return reinterpret_cast<TSIOBufferBlock>(block);
}

FakeBlock *
as_fake_block(TSIOBufferBlock block)
{
  return reinterpret_cast<FakeBlock *>(block);
}
} // namespace

TSIOBufferBlock
TSIOBufferBlockNext(TSIOBufferBlock blockp)
{
  FakeBlock *block = as_fake_block(blockp);

  return block == nullptr ? nullptr : as_block(block->next);
}

const char *
TSIOBufferBlockReadStart(TSIOBufferBlock blockp, TSIOBufferReader /* readerp ATS_UNUSED */, int64_t *avail)
{
  FakeBlock *block = as_fake_block(blockp);

  *avail = block == nullptr ? 0 : block->size;
  return block == nullptr ? nullptr : block->data;
}

TSIOBufferBlock
TSIOBufferReaderStart(TSIOBufferReader /* readerp ATS_UNUSED */)
{
  return as_block(first_block);
}

void
TSIOBufferReaderConsume(TSIOBufferReader /* readerp ATS_UNUSED */, int64_t nbytes)
{
  consumed     += nbytes;
  reader_avail -= nbytes;
}

int64_t
TSIOBufferReaderAvail(TSIOBufferReader /* readerp ATS_UNUSED */)
{
  return reader_avail;
}

TEST_CASE("ChunkDecoder accepts representable chunk sizes", "[multiplexer][chunk-decoder]")
{
  ChunkDecoder decoder;

  CHECK(decoder.parseSize("7fffffffffffffff\r\n", 18) == 18);
  CHECK_FALSE(decoder.isInvalid());
  CHECK_FALSE(decoder.isSizeState());
  CHECK_FALSE(decoder.isEnd());
}

TEST_CASE("ChunkDecoder rejects chunk sizes that exceed int64_t", "[multiplexer][chunk-decoder]")
{
  SECTION("one larger than INT64_MAX")
  {
    ChunkDecoder decoder;

    CHECK(decoder.parseSize("8000000000000000\r\n", 18) == 16);
    CHECK(decoder.isInvalid());
  }

  SECTION("17 hex digits")
  {
    ChunkDecoder decoder;

    CHECK(decoder.parseSize("10000000000000000\r\n", 19) == 17);
    CHECK(decoder.isInvalid());
  }
}

TEST_CASE("ChunkDecoder rejects malformed chunk-size characters", "[multiplexer][chunk-decoder]")
{
  SECTION("unexpected alphabetic character")
  {
    ChunkDecoder decoder;

    CHECK(decoder.parseSize("1z\r\n", 4) == 2);
    CHECK(decoder.isInvalid());
  }

  SECTION("embedded NUL byte")
  {
    ChunkDecoder decoder;
    const char   encoded[] = {'1', '\0', '\r', '\n'};

    CHECK(decoder.parseSize(encoded, sizeof(encoded)) == 2);
    CHECK(decoder.isInvalid());
  }
}

TEST_CASE("ChunkDecoder skips empty IOBuffer blocks while parsing chunk sizes", "[multiplexer][chunk-decoder]")
{
  ChunkDecoder decoder;
  FakeBlock    data_block{"1\r\nx", 4, nullptr};
  FakeBlock    empty_block{nullptr, 0, &data_block};

  first_block  = &empty_block;
  reader_avail = data_block.size;
  consumed     = 0;

  CHECK(decoder.decode(reinterpret_cast<TSIOBufferReader>(&empty_block)) == 1);
  CHECK(consumed == 3);
  CHECK_FALSE(decoder.isInvalid());

  first_block  = nullptr;
  reader_avail = 0;
  consumed     = 0;
}
