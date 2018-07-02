/** @file

  Multiplexes request to other origins.

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

#pragma once

#include <ts/ts.h>
#include <cinttypes>

/** Class to handle state for decoding chunked data.
 */
class ChunkDecoder
{
  /// Parse states.
  enum State {
    kInvalid, ///< Invalid state.

    kData,  ///< Expecting data.
    kDataN, ///< Expecting LF after size.
    kEnd,
    kEndN,
    kSize,  ///< Expecting chunk size.
    kSizeN, ///< Expecting LF after data.
    kSizeR, ///< Expecting CR after data.

    kUpperBound,
  };

  State state_  = kSize;
  int64_t size_ = 0;

public:
  /// Default Constructor. Construct to empty state of expected size 0.
  ChunkDecoder() {}

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
