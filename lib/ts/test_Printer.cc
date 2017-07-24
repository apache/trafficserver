/** @file

    Unit tests for Printer.h.

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

#include <ts/Printer.h>

#include <ts/test_Simple.h>

#include <ts/MemView.h>

#include <cstring>

using SV = ts::StringView;

bool
eqSV(SV sV1, SV sV2)
{
  return (sV1.size() == sV2.size()) and (memcmp(sV1.ptr(), sV2.ptr(), sV1.size()) == 0);
}

SV three[] = {SV("a", SV::literal), SV("", SV::literal), SV("bcd", SV::literal)};

TEST("BasePrinterIface::_pushBack(StringView)")
{
  class X : public ts::BasePrinterIface
  {
    size_t i, j;

  public:
    bool good;

    X() : i(0), j(0), good(true) {}

    void
    _pushBack(char c) override
    {
      while (j == three[i].size()) {
        ++i;
        j = 0;
      }

      if ((i >= 3) or (c != three[i][j])) {
        good = false;
      }

      ++j;
    }

    bool
    error() const override
    {
      return (false);
    }
  };

  X x;

  x(three[0], three[1], three[2]);

  return (x.good);
}

template <size_t N> using BP = ts::BuffPrinter<N>;

ATEST
{
  BP<1> bp;

  if ((bp.capacity() != 1) or (bp.size() != 0) or bp.error() or (bp.auxBufCapacity() != 1)) {
    return false;
  }

  bp('#');

  if ((bp.capacity() != 1) or (bp.size() != 1) or bp.error() or (bp.auxBufCapacity() != 0)) {
    return false;
  }

  if (!eqSV(bp, SV("#", SV::literal))) {
    return false;
  }

  bp('#');

  if (!bp.error()) {
    return (false);
  }

  bp.resize(1);

  if ((bp.capacity() != 1) or (bp.size() != 1) or bp.error() or (bp.auxBufCapacity() != 0)) {
    return false;
  }

  if (!eqSV(bp, SV("#", SV::literal))) {
    return false;
  }

  return true;
}

ATEST
{
  BP<20> bp;

  if ((bp.capacity() != 20) or (bp.size() != 0) or bp.error() or (bp.auxBufCapacity() != 20)) {
    return false;
  }

  bp('T');

  if ((bp.capacity() != 20) or (bp.size() != 1) or bp.error() or (bp.auxBufCapacity() != 19)) {
    return false;
  }

  if (!eqSV(bp, SV("T", SV::literal))) {
    return false;
  }

  bp.l("he")(' ', SV("quick", SV::literal), ' ').l("brown");

  SV tQB("The quick brown", SV::literal);

  if ((bp.capacity() != 20) or bp.error() or (bp.auxBufCapacity() != (20 - tQB.size()))) {
    return false;
  }

  if (!eqSV(bp, tQB)) {
    return false;
  }

  strcpy(bp.auxBuf(), " fox");
  bp.auxPrint(sizeof(" fox") - 1);

  if (bp.error()) {
    return false;
  }

  SV tQBF("The quick brown fox", SV::literal);

  if (!eqSV(bp, tQBF)) {
    return false;
  }

  bp('x');

  if (bp.error()) {
    return false;
  }

  bp('x');

  if (!bp.error()) {
    return false;
  }

  bp('x');

  if (!bp.error()) {
    return false;
  }

  bp.resize(tQBF.size());

  if (bp.error()) {
    return false;
  }

  if (!eqSV(bp, tQBF)) {
    return false;
  }

  ts::BuffPrinter<20> bp2(bp), bp3;

  bp3 = bp2;

  if (!eqSV(bp2, tQBF)) {
    return false;
  }

  if (!eqSV(bp3, tQBF)) {
    return false;
  }

  return true;
}
