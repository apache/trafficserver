/** @file

    Intrusive pointer test.

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

#include <ts/Scalar.h>
#include <string>
#include <cstdarg>
#include <iostream>

namespace ts
{
using namespace ApacheTrafficServer;
}

typedef ts::Scalar<1, off_t> Bytes;
typedef ts::Scalar<16, off_t> Paragraphs;
typedef ts::Scalar<1024, off_t> KB;
typedef ts::Scalar<KB::SCALE * 1024, off_t> MB;

#define FAIL_LINE " line ", __LINE__

struct TestBox {
  typedef TestBox self; ///< Self reference type.

  std::string _name;

  static int _count;
  static int _fail;

  TestBox(char const *name) : _name(name) {}
  TestBox(std::string const &name) : _name(name) {}
  template <typename... Rest>
  bool
  result(bool r, Rest &&... rest)
  {
    ++_count;
    if (!r) {
      std::cout << "FAIL: [" << _name << ":" << _count << "] ";
      (void)(int[]){0, ((std::cout << rest), 0)...};
      std::cout << std::endl;
      ++_fail;
    }
    return r;
  }

  template <typename A, typename B, typename... Rest>
  bool
  equal(A const &expected, B const &got, Rest const &... rest)
  {
    return result(expected == got, "Expected ", expected, " got ", got, rest...);
  }

  template <typename A, typename B, typename... Rest>
  bool
  lt(A const &lhs, B const &rhs, Rest const &... rest)
  {
    return result(lhs < rhs, "Expected {", lhs, " < ", rhs, "} ", rest...);
  }

  template <typename A, typename B, typename... Rest>
  bool
  le(A const &lhs, B const &rhs, Rest const &... rest)
  {
    return result(lhs <= rhs, "Expected {", lhs, " <= ", rhs, "} ", rest...);
  }

  template <typename A, typename B, typename... Rest>
  bool
  gt(A const &lhs, B const &rhs, Rest const &... rest)
  {
    return result(lhs > rhs, "Expected {", lhs, " > ", rhs, "} ", rest...);
  }

  template <typename A, typename B, typename... Rest>
  bool
  ge(A const &lhs, B const &rhs, Rest const &... rest)
  {
    return result(lhs >= rhs, "Expected {", lhs, " >= ", rhs, "} ", rest...);
  }

  static void
  print_summary()
  {
    printf("Tests: %d of %d passed - %s\n", (_count - _fail), _count, _fail ? "FAIL" : "SUCCESS");
  }
};

int TestBox::_count = 0;
int TestBox::_fail  = 0;

// Extremely simple test.
void
Test_1()
{
  constexpr static int SCALE = 4096;
  typedef ts::Scalar<SCALE> PageSize;

  TestBox test("TS.Scalar basic");
  PageSize pg1(1);

  test.equal(pg1.count(), 1, "Count wrong", FAIL_LINE);
  test.equal(pg1.units(), SCALE, "Units wrong", FAIL_LINE);
}

// Test multiples.
void
Test_2()
{
  constexpr static int SCALE_1 = 8192;
  constexpr static int SCALE_2 = 512;

  typedef ts::Scalar<SCALE_1> Size_1;
  typedef ts::Scalar<SCALE_2> Size_2;

  TestBox test("TS.Scalar Conversion of scales of multiples");
  Size_2 sz_a(2);
  Size_2 sz_b(57);
  Size_2 sz_c(SCALE_1 / SCALE_2);
  Size_2 sz_d(29 * SCALE_1 / SCALE_2);

  Size_1 sz = ts::round_up(sz_a);
  test.equal(sz.count(), 1, FAIL_LINE);
  sz = ts::round_down(sz_a);
  test.equal(sz.count(), 0, FAIL_LINE);

  sz = ts::round_up(sz_b);
  test.equal(sz.count(), 4, FAIL_LINE);
  sz = ts::round_down(sz_b);
  test.equal(sz.count(), 3, FAIL_LINE);

  sz = ts::round_up(sz_c);
  test.equal(sz.count(), 1, FAIL_LINE);
  sz = ts::round_down(sz_c);
  test.equal(sz.count(), 1, FAIL_LINE);

  sz = ts::round_up(sz_d);
  test.equal(sz.count(), 29, FAIL_LINE);
  sz = ts::round_down(sz_d);
  test.equal(sz.count(), 29, FAIL_LINE);

  sz.assign(119);
  sz_b = sz; // Should be OK because SCALE_1 is an integer multiple of SCALE_2
  //  sz = sz_b; // Should not compile.
  test.equal(sz_b.count(), 119 * (SCALE_1 / SCALE_2), FAIL_LINE);
}

// Test common factor.
void
Test_3()
{
  constexpr static int SCALE_1 = 30;
  constexpr static int SCALE_2 = 20;

  typedef ts::Scalar<SCALE_1> Size_1;
  typedef ts::Scalar<SCALE_2> Size_2;

  TestBox test("TS.Scalar common factor conversions");
  Size_2 sz_a(2);
  Size_2 sz_b(97);

  Size_1 sz = round_up(sz_a);
  test.equal(sz.count(), 2, FAIL_LINE);
  sz = round_down(sz_a);
  test.equal(sz.count(), 1, FAIL_LINE);

  sz = ts::round_up(sz_b);
  test.equal(sz.count(), 65, FAIL_LINE);
  sz = ts::round_down(sz_b);
  test.equal(sz.count(), 64, FAIL_LINE);
}

void
Test_4()
{
  TestBox test("TS.Scalar: relatively prime tests");

  ts::Scalar<9> m_9;
  ts::Scalar<4> m_4, m_test;

  m_9.assign(95);
  //  m_4 = m_9; // Should fail to compile with static assert.
  //  m_9 = m_4; // Should fail to compile with static assert.

  m_4 = ts::round_up(m_9);
  test.equal(m_4.count(), 214, "Rounding up 9->4", FAIL_LINE);
  m_4 = ts::round_down(m_9);
  test.equal(m_4.count(), 213, "Rounding down 9->4", FAIL_LINE);

  m_4.assign(213);
  m_9 = ts::round_up(m_4);
  test.equal(m_9.count(), 95, FAIL_LINE);
  m_9 = ts::round_down(m_4);
  test.equal(m_9.count(), 94, FAIL_LINE);

  m_test = m_4; // Verify assignment of identical scale values compiles.
  test.equal(m_test.count(), 213, FAIL_LINE);
}

void
Test_5()
{
  TestBox test("TS.Scalar: arithmetics");

  typedef ts::Scalar<1024> KBytes;
  typedef ts::Scalar<1024, long int> KiBytes;
  typedef ts::Scalar<1, int64_t> Bytes;
  typedef ts::Scalar<1024 * KBytes::SCALE> MBytes;

  Bytes bytes(96);
  KBytes kbytes(2);
  MBytes mbytes(5);

  Bytes z1 = bytes + 128;
  test.equal(z1.count(), 224, FAIL_LINE);
  KBytes z2 = kbytes + 3;
  test.equal(z2.count(), 5, FAIL_LINE);
  Bytes z3(bytes);
  z3 += kbytes;
  test.equal(z3.units(), 2048 + 96, FAIL_LINE);
  MBytes z4 = mbytes;
  z4 += 5;
  z2 += z4;
  test.equal(z2.units(), (10 << 20) + (5 << 10), FAIL_LINE);

  z1 += 128;
  test.equal(z1.count(), 352, FAIL_LINE);

  z2.assign(2);
  z1 = 3 * z2;
  test.equal(z1.count(), 6144, FAIL_LINE);
  z1 *= 5;
  test.equal(z1.count(), 30720, FAIL_LINE);
  z1 /= 3;
  test.equal(z1.count(), 10240, FAIL_LINE);

  z2.assign(3148);
  auto x = z2 + MBytes(1);
  test.equal(x.scale(), z2.scale(), FAIL_LINE);
  test.equal(x.count(), 4172, FAIL_LINE);

  z2 = ts::round_down(262150);
  test.equal(z2.count(), 256, FAIL_LINE);

  z2 = ts::round_up(262150);
  test.equal(z2.count(), 257, FAIL_LINE);

  KBytes q(ts::round_down(262150));
  test.equal(q.count(), 256, FAIL_LINE);

  z2 += ts::round_up(97384);
  test.equal(z2.count(), 353, FAIL_LINE);

  decltype(z2) a = z2 + ts::round_down(167229);
  test.equal(a.count(), 516, FAIL_LINE);

  KiBytes k(3148);
  auto kx = k + MBytes(1);
  test.equal(kx.scale(), k.scale(), FAIL_LINE);
  test.equal(kx.count(), 4172, FAIL_LINE);

  k = ts::round_down(262150);
  test.equal(k.count(), 256, FAIL_LINE);

  k = ts::round_up(262150);
  test.equal(k.count(), 257, FAIL_LINE);

  KBytes kq(ts::round_down(262150));
  test.equal(kq.count(), 256, FAIL_LINE);

  k += ts::round_up(97384);
  test.equal(k.count(), 353, FAIL_LINE);

  decltype(k) ka = k + ts::round_down(167229);
  test.equal(ka.count(), 516, FAIL_LINE);
}

// test comparisons
void
Test_6()
{
  using ts::Scalar;
  typedef Scalar<8 * KB::SCALE, off_t> StoreBlocks;
  typedef Scalar<127 * MB::SCALE, off_t> SpanBlocks;

  TestBox test("TS.Scalar: comparisons");

  StoreBlocks a(80759700);
  SpanBlocks b(4968);
  SpanBlocks delta(1);

  test.lt(a, b, FAIL_LINE);
  test.lt(b, a + delta, FAIL_LINE);
}

void
Test_7()
{
  using ts::Scalar;
  TestBox test("TS.Scalar: constructor tests");

  static const off_t N = 7 * 1024;
  Bytes b(N + 384);
  KB kb(round_down(b));

  test.equal(kb, N, "Round down wrong ", FAIL_LINE);
  test.lt(kb, N + 1, FAIL_LINE);
  test.gt(kb, N - 1, FAIL_LINE);

  test.lt(kb, b, FAIL_LINE);
  test.le(kb, b, FAIL_LINE);
  test.gt(b, kb, FAIL_LINE);
  test.ge(b, kb, FAIL_LINE);

  ++kb;

  test.lt(b, kb, FAIL_LINE);
  test.le(b, kb, FAIL_LINE);
  test.gt(kb, b, FAIL_LINE);
  test.ge(kb, b, FAIL_LINE);
}

struct KBytes_tag {
  static std::string const label;
};
std::string const KBytes_tag::label(" bytes");

void
Test_IO()
{
  typedef ts::Scalar<1024, long int, KBytes_tag> KBytes;
  typedef ts::Scalar<1024, int> KiBytes;

  KBytes x(12);
  KiBytes y(12);

  std::cout << "Testing" << std::endl;
  std::cout << "x is " << x << std::endl;
  std::cout << "y is " << y << std::endl;
}

void
test_Compile()
{
  // These tests aren't normally run, they exist to detect compiler issues.

  typedef ts::Scalar<1024, short> KBytes;
  typedef ts::Scalar<1024, int> KiBytes;
  int delta = 10;

  KBytes x(12);
  KiBytes y(12);

  if (x > 12) {
    std::cout << "Operator > works" << std::endl;
  }
  if (y > 12) {
    std::cout << "Operator > works" << std::endl;
  }

  (void)(x += 10);
  (void)(x += static_cast<int>(10));
  (void)(x += static_cast<long int>(10));
  (void)(x += delta);
  (void)(y += 10);
  (void)(y += static_cast<int>(10));
  (void)(y += static_cast<long int>(10));
  (void)(y += delta);
}

int
main(int, char **)
{
  Test_1();
  Test_2();
  Test_3();
  Test_4();
  Test_5();
  Test_6();
  Test_7();
  Test_IO();
  TestBox::print_summary();
  return 0;
}
