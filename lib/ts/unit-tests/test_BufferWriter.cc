/** @file

    Unit tests for BufferWriter.h.

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

#include "catch.hpp"
#include <ts/BufferWriter.h>
#include <ts/string_view.h>
#include <cstring>

namespace
{
ts::string_view three[] = {"a", "", "bcd"};
}

TEST_CASE("BufferWriter::write(StringView)", "[BWWSV]")
{
  class X : public ts::BufferWriter
  {
    size_t i, j;

  public:
    bool good;

    X() : i(0), j(0), good(true) {}

    X &
    write(char c) override
    {
      while (j == three[i].size()) {
        ++i;
        j = 0;
      }

      if ((i >= 3) or (c != three[i][j])) {
        good = false;
      }

      ++j;

      return *this;
    }

    bool
    error() const override
    {
      return false;
    }

    // Dummies.
    const char *
    data() const override
    {
      return nullptr;
    }
    size_t
    capacity() const override
    {
      return 0;
    }
    size_t
    extent() const override
    {
      return 0;
    }
    X &clip(size_t) override { return *this; }
    X &extend(size_t) override { return *this; }
    std::ostream &
    operator>>(std::ostream &stream) const override
    {
      return stream;
    }
    ssize_t
    operator>>(int fd) const override
    {
      return 0;
    }
  };

  X x;

  static_cast<ts::BufferWriter &>(x).write(three[0]).write(three[1]).write(three[2]);

  REQUIRE(x.good);
}

namespace
{
template <size_t N> using LBW = ts::LocalBufferWriter<N>;
}

TEST_CASE("Minimal Local Buffer Writer", "[BWLM]")
{
  LBW<1> bw;

  REQUIRE(!((bw.capacity() != 1) or (bw.size() != 0) or bw.error() or (bw.remaining() != 1)));

  bw.write('#');

  REQUIRE(!((bw.capacity() != 1) or (bw.size() != 1) or bw.error() or (bw.remaining() != 0)));

  REQUIRE(bw.view() == "#");

  bw.write('#');

  REQUIRE(bw.error());

  bw.reduce(1);

  REQUIRE(!((bw.capacity() != 1) or (bw.size() != 1) or bw.error() or (bw.remaining() != 0)));

  REQUIRE(bw.view() == "#");
}

namespace
{
template <class BWType>
bool
twice(BWType &bw)
{
  if ((bw.capacity() != 20) or (bw.size() != 0) or bw.error() or (bw.remaining() != 20)) {
    return false;
  }

  bw.write('T');

  if ((bw.capacity() != 20) or (bw.size() != 1) or bw.error() or (bw.remaining() != 19)) {
    return false;
  }

  if (bw.view() != "T") {
    return false;
  }

  bw.write("he").write(' ').write("quick").write(' ').write("brown");

  if ((bw.capacity() != 20) or bw.error() or (bw.remaining() != (21 - sizeof("The quick brown")))) {
    return false;
  }

  if (bw.view() != "The quick brown") {
    return false;
  }

  bw.reduce(0);

  bw << "The" << ' ' << "quick" << ' ' << "brown";

  if ((bw.capacity() != 20) or bw.error() or (bw.remaining() != (21 - sizeof("The quick brown")))) {
    return false;
  }

  if (bw.view() != "The quick brown") {
    return false;
  }

  bw.reduce(0);

  bw.write("The", 3).write(' ').write("quick", 5).write(' ').write(ts::string_view("brown", 5));

  if ((bw.capacity() != 20) or bw.error() or (bw.remaining() != (21 - sizeof("The quick brown")))) {
    return false;
  }

  if (bw.view() != "The quick brown") {
    return false;
  }

  std::strcpy(bw.auxBuffer(), " fox");
  bw.fill(sizeof(" fox") - 1);

  if (bw.error()) {
    return false;
  }

  if (bw.view() != "The quick brown fox") {
    return false;
  }

  bw.write('x');

  if (bw.error()) {
    return false;
  }

  bw.write('x');

  if (!bw.error()) {
    return false;
  }

  bw.write('x');

  if (!bw.error()) {
    return false;
  }

  bw.reduce(sizeof("The quick brown fox") - 1);

  if (bw.error()) {
    return false;
  }

  if (bw.view() != "The quick brown fox") {
    return false;
  }

  bw.reduce(sizeof("The quick brown") - 1);
  bw.clip(bw.capacity() + 2 - (sizeof("The quick brown fox") - 1)).write(" fox");

  if (bw.view() != "The quick brown f") {
    return false;
  }

  if (!bw.error()) {
    return false;
  }

  bw.extend(2).write("ox");

  if (bw.error()) {
    return false;
  }

  if (bw.view() != "The quick brown fox") {
    return false;
  }

  return true;
}

} // end anonymous namespace

TEST_CASE("Concrete Buffer Writers 2", "[BWC2]")
{
  LBW<20> bw;

  REQUIRE(twice(bw));

  char space[21];

  space[20] = '!';

  ts::FixedBufferWriter fbw(space, 20);

  REQUIRE(twice(fbw));

  REQUIRE(space[20] == '!');

  LBW<20> bw20(bw);
  LBW<30> bw30(bw); // test cross length constructors
  LBW<10> bw10(bw);

  REQUIRE(bw20.view() == "The quick brown fox");

  bw30 = bw20;
  REQUIRE(bw30.view() == "The quick brown fox");

  bw10 = bw20;
  REQUIRE(bw10.view() == "The quick ");
  bw10.reduce(0);
  bw10.write("01234567890123456789");
  REQUIRE(bw10.extent() == 20);
  REQUIRE(bw10.view() == "0123456789");
  REQUIRE(bw10.remaining() == 0);
  bw20 = bw10;
  REQUIRE(bw20.view() == "0123456789");
  REQUIRE(bw20.extent() == 10);
  REQUIRE(bw20.size() == 10);

  ts::FixedBufferWriter abw{bw20.auxWriter()};
  REQUIRE(abw.remaining() == 10);
  abw.write("abcdefghijklmnopqrstuvwxyz");
  bw20.fill(abw.extent());
  REQUIRE(bw20.size() == 20);
  REQUIRE(bw20.extent() == 36);
  REQUIRE(bw20.view() == "0123456789abcdefghij");
}

TEST_CASE("Discard Buffer Writer", "[BWD]")
{
  char scratch[1] = {'!'};
  ts::FixedBufferWriter bw(scratch, 0);

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == 0);

  bw.write('T');

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == 1);

  bw.write("he").write(' ').write("quick").write(' ').write("brown");

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown") - 1));

  bw.reduce(0);

  bw.write("The", 3).write(' ').write("quick", 5).write(' ').write(ts::string_view("brown", 5));

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown") - 1));

  bw.fill(sizeof(" fox") - 1);

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown fox") - 1));

  bw.reduce(sizeof("The quick brown fox") - 1);

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown fox") - 1));

  bw.reduce(sizeof("The quick brown") - 1);

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown") - 1));

  // Make sure no actual writing.
  //
  REQUIRE(scratch[0] == '!');
}

TEST_CASE("LocalBufferWriter clip and extend")
{
  ts::LocalBufferWriter<10> bw;

  bw.clip(7);
  bw << "aaaaaa";
  REQUIRE(bw.view() == "aaa");

  bw.extend(3);
  bw << "bbbbbb";
  REQUIRE(bw.view() == "aaabbb");

  bw.extend(4);
  bw.fill(static_cast<size_t>(snprintf(bw.auxBuffer(), bw.remaining(), "ccc")));
  REQUIRE(bw.view() == "aaabbbccc");
}
