/** @file

  Inlines base64 images from the ATS cache

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
/** @file

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
#include <assert.h>
#include <cstring>

#include "chunk-decoder.h"

void
ChunkDecoder::parseSizeCharacter(const char a)
{
  assert(state_ == State::kSize);
  if (a >= '0' && a <= '9') {
    size_ = (size_ << 4) | (a - '0');
  } else if (a >= 'A' && a <= 'F') {
    size_ = (size_ << 4) | (a - 'A' + 10);
  } else if (a >= 'a' && a <= 'f') {
    size_ = (size_ << 4) | (a - 'a' + 10);
  } else if (a == '\r') {
    state_ = size_ == 0 ? State::kEndN : State::kDataN;
  } else {
    assert(false); // invalid input
  }
}

int
ChunkDecoder::parseSize(const char *p, const int64_t s)
{
  assert(p != NULL);
  int length = 0;
  while (state_ != State::kData && *p != '\0' && length < s) {
    assert(state_ < State::kUpperBound); // VALID RANGE
    switch (state_) {
    case State::kData:
    case State::kEnd:
    case State::kInvalid:
    case State::kUnknown:
    case State::kUpperBound:
      assert(false);
      break;

    case State::kDataN:
      assert(*p == '\n');
      state_ = (*p == '\n') ? State::kData : State::kInvalid;
      break;

    case State::kEndN:
      assert(*p == '\n');
      state_ = (*p == '\n') ? State::kEnd : State::kInvalid;
      return length;

    case State::kSizeR:
      assert(*p == '\r');
      state_ = (*p == '\r') ? State::kSizeN : State::kInvalid;
      break;

    case State::kSizeN:
      assert(*p == '\n');
      state_ = (*p == '\n') ? State::kSize : State::kInvalid;
      break;

    case State::kSize:
      parseSizeCharacter(*p);
      break;
    }
    ++length;
    ++p;
    assert(state_ != State::kInvalid);
  }
  return length;
}

bool
ChunkDecoder::isSizeState(void) const
{
  return state_ == State::kDataN || state_ == State::kEndN || state_ == State::kSize || state_ == State::kSizeN ||
         state_ == State::kSizeR;
}

int
ChunkDecoder::decode(const TSIOBufferReader &r)
{
  assert(r != NULL);

  if (state_ == State::kEnd) {
    return 0;
  }

  {
    const int l = TSIOBufferReaderAvail(r);
    if (l < size_) {
      size_ -= l;
      return l;
    }
  }

  int64_t size;
  TSIOBufferBlock block = TSIOBufferReaderStart(r);

  if (isSizeState()) {
    while (block != NULL && size_ == 0) {
      const char *p = TSIOBufferBlockReadStart(block, r, &size);
      assert(p != NULL);
      const int i = parseSize(p, size);
      size -= i;
      TSIOBufferReaderConsume(r, i);
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

  int length = 0;

  while (block != NULL && state_ == State::kData) {
    const char *p = TSIOBufferBlockReadStart(block, r, &size);
    if (p != NULL) {
      if (size > size_) {
        length += size_;
        size_  = 0;
        state_ = State::kSizeR;
        break;
      } else {
        length += size;
        size_ -= size;
      }
    }
    block = TSIOBufferBlockNext(block);
  }

  return length;
}
