/** @file

    Scalar unit testing.

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

#include "swoc/Scalar.h"
#include "swoc/bwf_base.h"
#include "catch.hpp"

using Bytes      = swoc::Scalar<1, off_t>;
using Paragraphs = swoc::Scalar<16, off_t>;
using KB         = swoc::Scalar<1024, off_t>;
using MB         = swoc::Scalar<KB::SCALE * 1024, off_t>;

TEST_CASE("Scalar", "[libswoc][Scalar]") {
  constexpr static int SCALE   = 4096;
  constexpr static int SCALE_1 = 8192;
  constexpr static int SCALE_2 = 512;

  using PageSize = swoc::Scalar<SCALE>;

  PageSize pg1(1);
  REQUIRE(pg1.count() == 1);
  REQUIRE(pg1.value() == SCALE);

  using Size_1 = swoc::Scalar<SCALE_1>;
  using Size_2 = swoc::Scalar<SCALE_2>;

  Size_2 sz_a(2);
  Size_2 sz_b(57);
  Size_2 sz_c(SCALE_1 / SCALE_2);
  Size_2 sz_d(29 * SCALE_1 / SCALE_2);

  Size_1 sz = swoc::round_up(sz_a);
  REQUIRE(sz.count() == 1);
  sz = swoc::round_down(sz_a);
  REQUIRE(sz.count() == 0);

  sz = swoc::round_up(sz_b);
  REQUIRE(sz.count() == 4);
  sz = swoc::round_down(sz_b);
  REQUIRE(sz.count() == 3);

  sz = swoc::round_up(sz_c);
  REQUIRE(sz.count() == 1);
  sz = swoc::round_down(sz_c);
  REQUIRE(sz.count() == 1);

  sz = swoc::round_up(sz_d);
  REQUIRE(sz.count() == 29);
  sz = swoc::round_down(sz_d);
  REQUIRE(sz.count() == 29);

  sz.assign(119);
  sz_b = sz; // Should be OK because SCALE_1 is an integer multiple of SCALE_2
  //  sz = sz_b; // Should not compile.
  REQUIRE(sz_b.count() == 119 * (SCALE_1 / SCALE_2));

  // Test generic rounding.
  REQUIRE(120 == swoc::round_up<10>(118));
  REQUIRE(120 == swoc::round_up<10>(120));
  REQUIRE(130 == swoc::round_up<10>(121));

  REQUIRE(110 == swoc::round_down<10>(118));
  REQUIRE(120 == swoc::round_down<10>(120));
  REQUIRE(120 == swoc::round_down<10>(121));

  REQUIRE(200 == swoc::round_up<100>(118));
  REQUIRE(1200 == swoc::round_up<100>(1118));
  REQUIRE(1200 == swoc::round_up<100>(1200));
  REQUIRE(1300 == swoc::round_up<100>(1210));

  REQUIRE(100 == swoc::round_down<100>(118));
  REQUIRE(1100 == swoc::round_down<100>(1118));
  REQUIRE(1200 == swoc::round_down<100>(1200));
  REQUIRE(1200 == swoc::round_down<100>(1210));
}
TEST_CASE("Scalar Factors", "[libswoc][Scalar][factors]") {
  constexpr static int SCALE_1 = 30;
  constexpr static int SCALE_2 = 20;

  using Size_1 = swoc::Scalar<SCALE_1>;
  using Size_2 = swoc::Scalar<SCALE_2>;

  Size_2 sz_a(2);
  Size_2 sz_b(97);

  Size_1 sz = round_up(sz_a);
  REQUIRE(sz.count() == 2);
  sz = round_down(sz_a);
  REQUIRE(sz.count() == 1);

  sz = swoc::round_up(sz_b);
  REQUIRE(sz.count() == 65);
  sz = swoc::round_down(sz_b);
  REQUIRE(sz.count() == 64);

  swoc::Scalar<9> m_9;
  swoc::Scalar<4> m_4, m_test;

  m_9.assign(95);
  //  m_4 = m_9; // Should fail to compile with static assert.
  //  m_9 = m_4; // Should fail to compile with static assert.

  m_4 = swoc::round_up(m_9);
  REQUIRE(m_4.count() == 214);
  m_4 = swoc::round_down(m_9);
  REQUIRE(m_4.count() == 213);

  m_4.assign(213);
  m_9 = swoc::round_up(m_4);
  REQUIRE(m_9.count() == 95);
  m_9 = swoc::round_down(m_4);
  REQUIRE(m_9.count() == 94);

  m_test = m_4; // Verify assignment of identical scale values compiles.
  REQUIRE(m_test.count() == 213);
}

TEST_CASE("Scalar Arithmetic", "[libswoc][Scalar][arithmetic]") {
  using KBytes  = swoc::Scalar<1024>;
  using KiBytes = swoc::Scalar<1024, long int>;
  using Bytes   = swoc::Scalar<1, int64_t>;
  using MBytes  = swoc::Scalar<1024 * KBytes::SCALE>;

  Bytes bytes(96);
  KBytes kbytes(2);
  MBytes mbytes(5);

  Bytes z1 = swoc::round_up(bytes + 128);
  REQUIRE(z1.count() == 224);
  KBytes z2 = kbytes + kbytes(3);
  REQUIRE(z2.count() == 5);
  Bytes z3(bytes);
  z3 += kbytes;
  REQUIRE(z3.value() == 2048 + 96);
  MBytes z4 = mbytes;
  z4.inc(5);
  z2 += z4;
  REQUIRE(z2.value() == (10 << 20) + (5 << 10));

  z1.inc(128);
  REQUIRE(z1.count() == 352);

  z2.assign(2);
  z1 = 3 * z2;
  REQUIRE(z1.count() == 6144);
  z1 *= 5;
  REQUIRE(z1.count() == 30720);
  z1 /= 3;
  REQUIRE(z1.count() == 10240);

  z2.assign(3148);
  auto x = z2 + MBytes(1);
  REQUIRE(x.scale() == z2.scale());
  REQUIRE(x.count() == 4172);

  z2 = swoc::round_down(262150);
  REQUIRE(z2.count() == 256);

  z2 = swoc::round_up(262150);
  REQUIRE(z2.count() == 257);

  KBytes q(swoc::round_down(262150));
  REQUIRE(q.count() == 256);

  z2 += swoc::round_up(97384);
  REQUIRE(z2.count() == 353);

  decltype(z2) a = swoc::round_down(z2 + 167229);
  REQUIRE(a.count() == 516);

  KiBytes k(3148);
  auto kx = k + MBytes(1);
  REQUIRE(kx.scale() == k.scale());
  REQUIRE(kx.count() == 4172);

  k = swoc::round_down(262150);
  REQUIRE(k.count() == 256);

  k = swoc::round_up(262150);
  REQUIRE(k.count() == 257);

  KBytes kq(swoc::round_down(262150));
  REQUIRE(kq.count() == 256);

  k += swoc::round_up(97384);
  REQUIRE(k.count() == 353);

  decltype(k) ka = swoc::round_down(k + 167229);
  REQUIRE(ka.count() == 516);

  using StoreBlocks = swoc::Scalar<8 * KB::SCALE, off_t>;
  using SpanBlocks  = swoc::Scalar<127 * MB::SCALE, off_t>;

  StoreBlocks store_b(80759700);
  SpanBlocks span_b(4968);
  SpanBlocks delta(1);

  REQUIRE(store_b < span_b);
  REQUIRE(span_b < store_b + delta);
  store_b += delta;
  REQUIRE(span_b < store_b);

  static const off_t N = 7 * 1024;
  Bytes b(N + 384);
  KB kb(round_down(b));

  REQUIRE(kb == N);
  REQUIRE(kb < N + 1);
  REQUIRE(kb > N - 1);

  REQUIRE(kb < b);
  REQUIRE(kb <= b);
  REQUIRE(b > kb);
  REQUIRE(b >= kb);

  ++kb;

  REQUIRE(b < kb);
  REQUIRE(b <= kb);
  REQUIRE(kb > b);
  REQUIRE(kb >= b);
}

struct KBytes_tag {
  static constexpr std::string_view label{" bytes"};
};

TEST_CASE("Scalar Formatting", "[libswoc][Scalar][bwf]") {
  using KBytes  = swoc::Scalar<1024, long int, KBytes_tag>;
  using KiBytes = swoc::Scalar<1000, int>;

  KBytes x(12);
  KiBytes y(12);
  swoc::LocalBufferWriter<128> w;

  w.print("x is {}", x);
  REQUIRE(w.view() == "x is 12288 bytes");
  w.clear().print("y is {}", y);
  REQUIRE(w.view() == "y is 12000");
}
