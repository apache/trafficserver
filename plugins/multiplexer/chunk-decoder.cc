/** @file

  Chunk decoding.

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
#include <algorithm>
#include <cassert>
#include <limits>

#include "chunk-decoder.h"

namespace
{
int
parse_hex_digit(const char a)
{
  if (a >= '0' && a <= '9') {
    return a - '0';
  }
  if (a >= 'A' && a <= 'F') {
    return a - 'A' + 10;
  }
  if (a >= 'a' && a <= 'f') {
    return a - 'a' + 10;
  }
  return -1;
}
} // namespace

void
ChunkDecoder::parseSizeCharacter(const char a)
{
  assert(state_ == State::kSize);
  const int digit = parse_hex_digit(a);
  if (digit >= 0) {
    constexpr int64_t max_chunk_size = std::numeric_limits<int64_t>::max();
    if (size_ > (max_chunk_size - digit) / 16) {
      state_ = State::kInvalid;
      size_  = 0;
      return;
    }
    size_ = size_ * 16 + digit;
  } else if (a == '\r') {
    state_ = size_ == 0 ? State::kEndN : State::kDataN;
  } else {
    state_ = State::kInvalid;
    return;
  }
  return;
}

int64_t
ChunkDecoder::parseSize(const char *p, const int64_t s)
{
  assert(p != nullptr);
  assert(s > 0);
  int64_t length = 0;
  while (state_ != State::kData && state_ != State::kInvalid && length < s) {
    assert(state_ < State::kUpperBound); // VALID RANGE
    switch (state_) {
    case State::kData:
    case State::kEnd:
    case State::kUpperBound:
      assert(false);
      break;

    case State::kInvalid:
      break;

    case State::kDataN:
      state_ = (*p == '\n') ? State::kData : State::kInvalid;
      break;

    case State::kEndN:
      state_ = (*p == '\n') ? State::kEnd : State::kInvalid;
      if (state_ == State::kEnd) {
        return length;
      }
      break;

    case State::kSizeR:
      state_ = (*p == '\r') ? State::kSizeN : State::kInvalid;
      break;

    case State::kSizeN:
      state_ = (*p == '\n') ? State::kSize : State::kInvalid;
      break;

    case State::kSize:
      parseSizeCharacter(*p);
      break;
    }
    ++length;
    ++p;
  }
  return length;
}

bool
ChunkDecoder::isSizeState() const
{
  return state_ == State::kDataN || state_ == State::kEndN || state_ == State::kSize || state_ == State::kSizeN ||
         state_ == State::kSizeR;
}

int64_t
ChunkDecoder::decode(const TSIOBufferReader &r)
{
  assert(r != nullptr);

  if (state_ == State::kInvalid) {
    return -1;
  }

  if (state_ == State::kEnd) {
    return 0;
  }

  {
    const int64_t l = TSIOBufferReaderAvail(r);
    if (l == 0) {
      return 0;
    } else if (l < size_) {
      size_ -= l;
      return l;
    }
  }

  int64_t         size;
  TSIOBufferBlock block = TSIOBufferReaderStart(r);

  // Trying to parse a size.
  if (isSizeState()) {
    while (block != nullptr && size_ == 0 && !isInvalid()) {
      const char *p = TSIOBufferBlockReadStart(block, r, &size);
      if (size == 0) {
        block = TSIOBufferBlockNext(block);
        continue;
      }
      assert(p != nullptr);
      const int64_t i  = parseSize(p, size);
      size            -= i;
      TSIOBufferReaderConsume(r, i);
      if (isInvalid()) {
        return -1;
      }
      if (state_ == State::kEnd) {
        assert(size_ == 0);
        return 0;
      }
      if (isSizeState()) {
        assert(size == 0);
        block = TSIOBufferBlockNext(block);
      }
    }
  }

  int64_t length = 0;

  while (block != nullptr && state_ == State::kData) {
    assert(size_ > 0);
    const char *p = TSIOBufferBlockReadStart(block, r, &size);
    if (p != nullptr) {
      if (size >= size_) {
        length += size_;
        size_   = 0;
        state_  = State::kSizeR;
        break;
      } else {
        length += size;
        size_  -= size;
      }
    }
    block = TSIOBufferBlockNext(block);
  }

  return length;
}
