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
#ifndef CHUNK_DECODER_H
#define CHUNK_DECODER_H

#include <ts/ts.h>
#include <inttypes.h>

class ChunkDecoder
{
  struct State {
    enum STATES {
      kUnknown,

      kInvalid,

      kData,
      kDataN,
      kEnd,
      kEndN,
      kSize,
      kSizeN,
      kSizeR,

      kUpperBound,
    };
  };

  State::STATES state_;
  int64_t size_;

public:
  ChunkDecoder(void) : state_(State::kSize), size_(0) {}
  void parseSizeCharacter(const char);
  int parseSize(const char *, const int64_t);
  int decode(const TSIOBufferReader &);
  bool isSizeState(void) const;

  inline bool
  isEnd(void) const
  {
    return state_ == State::kEnd;
  }
};

#endif // CHUNK_DECODER_H
