/** @file

    Unit tests for BufferWriter.h.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#include <cstring>
#include "swoc/MemSpan.h"
#include "swoc/TextView.h"
#include "swoc/MemArena.h"
#include "swoc/BufferWriter.h"
#include "swoc/ArenaWriter.h"
#include "catch.hpp"

using swoc::TextView;
using swoc::MemSpan;

namespace {
std::string_view three[] = {"a", "", "bcd"};
}

TEST_CASE("BufferWriter::write(StringView)", "[BWWSV]") {
  class X : public swoc::BufferWriter {
    size_t i, j;

  public:
    bool good;

    X() : i(0), j(0), good(true) {}

    X &
    write(char c) override {
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
    error() const override {
      return false;
    }

    // Dummies.
    const char *
    data() const override {
      return nullptr;
    }
    size_t
    capacity() const override {
      return 0;
    }
    size_t
    extent() const override {
      return 0;
    }
    X &restrict(size_t) override { return *this; }
    X &
    restore(size_t) override {
      return *this;
    }
    bool
    commit(size_t) override {
      return true;
    }
    X &
    discard(size_t) override {
      return *this;
    }
    X &
    copy(size_t, size_t, size_t) override {
      return *this;
    }
    std::ostream &
    operator>>(std::ostream &stream) const override {
      return stream;
    }
  };

  X x;

  static_cast<swoc::BufferWriter &>(x).write(three[0]).write(three[1]).write(three[2]);

  REQUIRE(x.good);
}

namespace {
template <size_t N> using LBW = swoc::LocalBufferWriter<N>;
}

TEST_CASE("Minimal Local Buffer Writer", "[BWLM]") {
  LBW<1> bw;

  REQUIRE(!((bw.capacity() != 1) or (bw.size() != 0) or bw.error() or (bw.remaining() != 1)));

  bw.write('#');

  REQUIRE(!((bw.capacity() != 1) or (bw.size() != 1) or bw.error() or (bw.remaining() != 0)));

  REQUIRE(bw.view() == "#");

  bw.write('!');

  REQUIRE(bw.error());

  bw.discard(1);

  REQUIRE(!((bw.capacity() != 1) or (bw.size() != 1) or bw.error() or (bw.remaining() != 0)));

  REQUIRE(bw.view() == "#");
}

namespace {
template <class BWType>
bool
twice(BWType &bw) {
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

  bw.clear();

  bw << "The" << ' ' << "quick" << ' ' << "brown";

  if ((bw.capacity() != 20) or bw.error() or (bw.remaining() != (21 - sizeof("The quick brown")))) {
    return false;
  }

  if (bw.view() != "The quick brown") {
    return false;
  }

  bw.clear();

  bw.write("The", 3).write(' ').write("quick", 5).write(' ').write(std::string_view("brown", 5));

  if ((bw.capacity() != 20) or bw.error() or (bw.remaining() != (21 - sizeof("The quick brown")))) {
    return false;
  }

  if (bw.view() != "The quick brown") {
    return false;
  }

  std::strcpy(bw.aux_buffer(), " fox");
  bw.commit(sizeof(" fox") - 1);

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

  bw.reduce(0);

  if (bw.error()) {
    return false;
  }

  if (bw.view() != "The quick brown fox") {
    return false;
  }

  bw.reduce(4);
  bw.discard(bw.capacity() + 2 - (sizeof("The quick brown fox") - 1)).write(" fox");

  if (bw.view() != "The quick brown f") {
    return false;
  }

  if (!bw.error()) {
    return false;
  }

  bw.restore(2).write("ox");

  if (bw.error()) {
    return false;
  }

  if (bw.view() != "The quick brown fox") {
    return false;
  }

  return true;
}

} // end anonymous namespace

TEST_CASE("Discard Buffer Writer", "[BWD]") {
  char scratch[1] = {'!'};
  swoc::FixedBufferWriter bw(scratch, 0);

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == 0);

  bw.write('T');

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == 1);

  bw.write("he").write(' ').write("quick").write(' ').write("brown");

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown") - 1));

  bw.clear();

  bw.write("The", 3).write(' ').write("quick", 5).write(' ').write(std::string_view("brown", 5));

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown") - 1));

  bw.commit(sizeof(" fox") - 1);

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown fox") - 1));

  bw.discard(0);

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown fox") - 1));

  bw.discard(4);

  REQUIRE(bw.size() == 0);
  REQUIRE(bw.extent() == (sizeof("The quick brown") - 1));

  // Make sure no actual writing.
  //
  REQUIRE(scratch[0] == '!');
}

TEST_CASE("LocalBufferWriter discard/restore", "[BWD]") {
  swoc::LocalBufferWriter<10> bw;

  bw.restrict(7);
  bw.write("aaaaaa");
  REQUIRE(bw.view() == "aaa");

  bw.restore(3);
  bw.write("bbbbbb");
  REQUIRE(bw.view() == "aaabbb");

  bw.restore(4);
  bw.commit(static_cast<size_t>(snprintf(bw.aux_data(), bw.remaining(), "ccc")));
  REQUIRE(bw.view() == "aaabbbccc");
}

TEST_CASE("Writing", "[BW]") {
  swoc::LocalBufferWriter<1024> bw;

  // Test run length encoding.
  TextView s1       = "Delain";
  TextView s2       = "Nightwish";
  uint8_t const r[] = {
    uint8_t(s1.size()), 'D', 'e', 'l', 'a', 'i', 'n', uint8_t(s2.size()), 'N', 'i', 'g', 'h', 't', 'w', 'i', 's', 'h'};

  bw.print("{}{}{}{}", char(s1.size()), s1, char(s2.size()), s2);
  auto result{swoc::MemSpan{bw.view()}.rebind<uint8_t const>()};
  REQUIRE(result[0] == s1.size());
  REQUIRE(result[s1.size() + 1] == s2.size());
  REQUIRE(MemSpan(r) == result);
}

TEST_CASE("ArenaWriter write", "[BW][ArenaWriter]") {
  swoc::MemArena arena{256};
  swoc::ArenaWriter aw{arena};
  std::array<char, 85> buffer;

  for (char c = 'a'; c <= 'z'; ++c) {
    memset(buffer.data(), c, buffer.size());
    aw.write(buffer.data(), buffer.size());
  }

  auto constexpr N = 26 * buffer.size();
  REQUIRE(aw.extent() == N);
  REQUIRE(aw.size() == N);
  REQUIRE(arena.remaining() >= N);

  // It's all in the remnant, so allocating it shouldn't affect the overall reserved memory.
  auto k    = arena.reserved_size();
  auto span = arena.alloc(N);
  REQUIRE(arena.reserved_size() == k);
  // The allocated data should be identical to that in the writer.
  REQUIRE(0 == memcmp(span.data(), aw.data(), span.size()));

  bool valid_p = true;
  auto tv      = swoc::TextView(span.rebind<char>());
  try {
    for (char c = 'a'; c <= 'z'; ++c) {
      for (size_t i = 0; i < buffer.size(); ++i) {
        if (c != *tv++) {
          throw std::exception{};
        }
      }
    }
  } catch (std::exception &ex) {
    valid_p = false;
  }
  REQUIRE(valid_p == true);
}

TEST_CASE("ArenaWriter print", "[BW][ArenaWriter]") {
  swoc::MemArena arena{256};
  swoc::ArenaWriter aw{arena};
  std::array<char, 85> buffer;
  swoc::TextView view{buffer.data(), buffer.size()};

  for (char c = 'a'; c <= 'z'; ++c) {
    memset(buffer.data(), c, buffer.size());
    aw.print("{}{}{}{}{}", view.substr(0, 25), view.substr(25, 15), view.substr(40, 17), view.substr(57, 19), view.substr(76, 9));
  }

  auto constexpr N = 26 * buffer.size();
  REQUIRE(aw.extent() == N);
  REQUIRE(aw.size() == N);
  REQUIRE(arena.remaining() >= N);

  // It's all in the remnant, so allocating it shouldn't affect the overall reserved memory.
  auto k    = arena.reserved_size();
  auto span = arena.alloc(N).rebind<char>();
  REQUIRE(arena.reserved_size() == k);
  // The allocated data should be identical to that in the writer.
  REQUIRE(0 == memcmp(span.data(), aw.data(), span.size()));

  bool valid_p = true;
  auto tv      = swoc::TextView(span);
  try {
    for (char c = 'a'; c <= 'z'; ++c) {
      for (size_t i = 0; i < buffer.size(); ++i) {
        if (c != *tv++) {
          throw std::exception{};
        }
      }
    }
  } catch (std::exception &ex) {
    valid_p = false;
  }
  REQUIRE(valid_p == true);
}

#if 0
// Need Endpoint or some other IP address parsing support to load the test values.
TEST_CASE("BufferWriter IP", "[libswoc][ip][bwf]") {
  IpEndpoint ep;
  std::string_view addr_1{"[ffee::24c3:3349:3cee:143]:8080"};
  std::string_view addr_2{"172.17.99.231:23995"};
  std::string_view addr_3{"[1337:ded:BEEF::]:53874"};
  std::string_view addr_4{"[1337::ded:BEEF]:53874"};
  std::string_view addr_5{"[1337:0:0:ded:BEEF:0:0:956]:53874"};
  std::string_view addr_6{"[1337:0:0:ded:BEEF:0:0:0]:53874"};
  std::string_view addr_7{"172.19.3.105:4951"};
  std::string_view addr_null{"[::]:53874"};
  swoc::LocalBufferWriter<1024> w;

  REQUIRE(0 == ats_ip_pton(addr_1, &ep.sa));
  w.clear().print("{}", ep);
  REQUIRE(w.view() == addr_1);
  w.clear().print("{::p}", ep);
  REQUIRE(w.view() == "8080");
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == addr_1.substr(1, 24)); // check the brackets are dropped.
  w.clear().print("[{::a}]", ep);
  REQUIRE(w.view() == addr_1.substr(0, 26)); // check the brackets are dropped.
  w.clear().print("[{0::a}]:{0::p}", ep);
  REQUIRE(w.view() == addr_1); // check the brackets are dropped.
  w.clear().print("{::=a}", ep);
  REQUIRE(w.view() == "ffee:0000:0000:0000:24c3:3349:3cee:0143");
  w.clear().print("{:: =a}", ep);
  REQUIRE(w.view() == "ffee:   0:   0:   0:24c3:3349:3cee: 143");
  ep.setToLoopback(AF_INET6);
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "::1");
  REQUIRE(0 == ats_ip_pton(addr_3, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337:ded:beef::");
  REQUIRE(0 == ats_ip_pton(addr_4, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337::ded:beef");

  REQUIRE(0 == ats_ip_pton(addr_5, &ep.sa));
  w.clear().print("{:X:a}", ep);
  REQUIRE(w.view() == "1337::DED:BEEF:0:0:956");

  REQUIRE(0 == ats_ip_pton(addr_6, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337:0:0:ded:beef::");

  REQUIRE(0 == ats_ip_pton(addr_null, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "::");

  REQUIRE(0 == ats_ip_pton(addr_2, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == addr_2.substr(0, 13));
  w.clear().print("{0::a}", ep);
  REQUIRE(w.view() == addr_2.substr(0, 13));
  w.clear().print("{::ap}", ep);
  REQUIRE(w.view() == addr_2);
  w.clear().print("{::f}", ep);
  REQUIRE(w.view() == IP_PROTO_TAG_IPV4);
  w.clear().print("{::fpa}", ep);
  REQUIRE(w.view() == "172.17.99.231:23995 ipv4");
  w.clear().print("{0::a} .. {0::p}", ep);
  REQUIRE(w.view() == "172.17.99.231 .. 23995");
  w.clear().print("<+> {0::a} <+> {0::p}", ep);
  REQUIRE(w.view() == "<+> 172.17.99.231 <+> 23995");
  w.clear().print("<+> {0::a} <+> {0::p} <+>", ep);
  REQUIRE(w.view() == "<+> 172.17.99.231 <+> 23995 <+>");
  w.clear().print("{:: =a}", ep);
  REQUIRE(w.view() == "172. 17. 99.231");
  w.clear().print("{::=a}", ep);
  REQUIRE(w.view() == "172.017.099.231");

  // Documentation examples
  REQUIRE(0 == ats_ip_pton(addr_7, &ep.sa));
  w.clear().print("To {}", ep);
  REQUIRE(w.view() == "To 172.19.3.105:4951");
  w.clear().print("To {0::a} on port {0::p}", ep); // no need to pass the argument twice.
  REQUIRE(w.view() == "To 172.19.3.105 on port 4951");
  w.clear().print("To {::=}", ep);
  REQUIRE(w.view() == "To 172.019.003.105:04951");
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "172.19.3.105");
  w.clear().print("{::=a}", ep);
  REQUIRE(w.view() == "172.019.003.105");
  w.clear().print("{::0=a}", ep);
  REQUIRE(w.view() == "172.019.003.105");
  w.clear().print("{:: =a}", ep);
  REQUIRE(w.view() == "172. 19.  3.105");
  w.clear().print("{:>20:a}", ep);
  REQUIRE(w.view() == "        172.19.3.105");
  w.clear().print("{:>20:=a}", ep);
  REQUIRE(w.view() == "     172.019.003.105");
  w.clear().print("{:>20: =a}", ep);
  REQUIRE(w.view() == "     172. 19.  3.105");
  w.clear().print("{:<20:a}", ep);
  REQUIRE(w.view() == "172.19.3.105        ");

  w.clear().print("{:p}", reinterpret_cast<sockaddr const *>(0x1337beef));
  REQUIRE(w.view() == "0x1337beef");
}
#endif
