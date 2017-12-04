/** @file

    Unit tests for ThrowSkipFBW.h.

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

#include "ThrowSkipFBW.h"
#include "ThrowSkipFBW.h"

#include "catch.hpp"

#include <cstring>
#include <memory>

namespace
{
// Array of segment sizes.  There's no pattern to these number, expect that the last three are the first three in reverse order.
//
const int Seg[2 * 3 * 5]{3, 2, 1, 10, 3, 100, 20, 1, 1, 555, 13, 3, 2, 1, 10, 3, 150, 28, 1, 1, 675, 3, 1, 17, 3, 101, 10, 1, 2, 3};

int
segAccum(const int *seg, int numSegs)
{
  int total = 0;
  for (int i = 0; i < numSegs; ++i) {
    total += seg[i];
  }
  return total;
}

// Total of numbers in 'Seg'.
const int Total{segAccum(Seg, 30)};

std::unique_ptr<char[]> src{new char[Total]};

std::unique_ptr<char[]> dest{new char[Total]};

void
cp(ts::BufferWriter &bw, int offset, const int *seg, int segCount)
{
  while (segCount--) {
    if (*seg == 1) {
      bw << src[offset];
    } else {
      bw.write(src.get() + offset, *seg);
    }
    offset += *seg;
    ++seg;
  }
}

// Returns true if all data in all segments was copied to the buffer.  Otherwise the copy was partial, and the function must
// be called again with the next buffer.
//
template <class TBW>
bool
oneBw(int srcOffset, const int *seg, int segCount, int bufOffset, int bufSize, int *bufIdxInOut, int *skipInOut)
{
  REQUIRE((srcOffset + *skipInOut) == (bufOffset + *bufIdxInOut));
  REQUIRE(*bufIdxInOut < bufSize);

  REQUIRE(std::memcmp(src.get(), dest.get(), bufOffset + *bufIdxInOut) == 0);

  TBW bw(dest.get() + bufOffset, bufSize, bufIdxInOut, skipInOut);

  bool allCopied = true;

  try {
    cp(bw, srcOffset, seg, segCount);

  } catch (typename TBW::OverflowException) {
    allCopied = false;
  }

  bw.legacyAdjust(bufIdxInOut, skipInOut);

  REQUIRE(*bufIdxInOut <= bufSize);

  int end{bufOffset + *bufIdxInOut - *skipInOut};

  if ((*bufIdxInOut >= 0) and (end < Total)) {
    int i = end;
    for (; i < Total; ++i) {
      if (dest[i]) {
        break;
      }
    }
    REQUIRE(i == Total);
  }

  if (end) {
    REQUIRE(std::memcmp(src.get(), dest.get(), end) == 0);
  }

  return (allCopied);
}

template <class TBW>
void
fullCopy(int srcSegsPerGroup, int destSegsPerGroup)
{
  std::memset(dest.get(), 0, Total);

  // Walk backwards through the table of segments to generate the buffer sequence, so that the buffers will be skewed from the
  // region of data copied by calls to oneBw().
  //
  const int *destSeg{Seg + 30 - destSegsPerGroup};

  int bufSize{segAccum(destSeg, destSegsPerGroup)};
  int bufOffset{0}, bufIdxInOut{0};

  const int *srcSeg{Seg};
  int srcOffset{0}, skipInOut{0};

  for (;;) {
    bool done{oneBw<TBW>(srcOffset, srcSeg, srcSegsPerGroup, bufOffset, bufSize, &bufIdxInOut, &skipInOut)};

    if (bufIdxInOut == bufSize) {
      // Buffer filled up, need next buffer.

      if (destSeg == Seg) {
        // Full copy is complete.
        REQUIRE(done);
        break;
      }

      destSeg -= destSegsPerGroup;

      bufOffset += bufSize;
      bufSize     = segAccum(destSeg, destSegsPerGroup);
      bufIdxInOut = 0;
      REQUIRE(bufOffset >= srcOffset); // TEMP
      skipInOut = bufOffset - srcOffset;
    }

    if (done) {
      // Current source segment group done, need next one.
      srcOffset += segAccum(srcSeg, srcSegsPerGroup);
      srcSeg += srcSegsPerGroup;
      skipInOut = 0;
    }
  }
  REQUIRE(std::memcmp(src.get(), dest.get(), Total) == 0);
}

template <class TBW>
void
tst()
{
  fullCopy<TBW>(1, 1);
  fullCopy<TBW>(2, 1);
  fullCopy<TBW>(3, 1);
  fullCopy<TBW>(6, 1);
  fullCopy<TBW>(30, 1);
  fullCopy<TBW>(1, 2);
  fullCopy<TBW>(2, 2);
  fullCopy<TBW>(3, 2);
  fullCopy<TBW>(6, 2);
  fullCopy<TBW>(30, 2);
  fullCopy<TBW>(1, 5);
  fullCopy<TBW>(2, 5);
  fullCopy<TBW>(3, 5);
  fullCopy<TBW>(6, 5);
  fullCopy<TBW>(30, 5);
  fullCopy<TBW>(1, 30);
  fullCopy<TBW>(2, 30);
  fullCopy<TBW>(3, 30);
  fullCopy<TBW>(6, 30);
  fullCopy<TBW>(30, 30);
}

} // end anonymous namespace

TEST_CASE("ThrowSkipFWB.h", "[TSFBW]")
{
  unsigned char j = 1;
  for (int i = 0; i < Total; ++i, j *= 7) {
    src[i] = j;
  }

  tst<ts::ThrowSkipFixedBufferWriter>();
  tst<ts::TestThrowSkipFixedBufferWriter>();
}
