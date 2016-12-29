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
#include <stdarg.h>
#include <iostream>

namespace ts
{
using namespace ApacheTrafficServer;
}

struct TestBox {
  typedef TestBox self; ///< Self reference type.

  std::string _name;

  static int _count;
  static int _fail;

  TestBox(char const *name) : _name(name) {}
  TestBox(std::string const &name) : _name(name) {}
  bool check(bool result, char const *fmt, ...) __attribute__((format(printf, 3, 4)));

  static void
  print_summary()
  {
    printf("Tests: %d of %d passed - %s\n", (_count - _fail), _count, _fail ? "FAIL" : "SUCCESS");
  }
};

int TestBox::_count = 0;
int TestBox::_fail  = 0;

bool
TestBox::check(bool result, char const *fmt, ...)
{
  ++_count;

  if (!result) {
    static constexpr size_t N = 1 << 16;
    size_t n                  = N;
    size_t x;
    char *s;
    char buffer[N]; // just stack, go big.

    s = buffer;
    x = snprintf(s, n, "%s: ", _name.c_str());
    n -= x;
    s += x;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, n, fmt, ap);
    va_end(ap);
    printf("%s\n", buffer);
    ++_fail;
  }
  return result;
}

// Extremely simple test.
void
Test_1()
{
  constexpr static int SCALE = 4096;
  typedef ts::Scalar<SCALE> PageSize;

  TestBox test("TS Scalar basic");
  PageSize pg1(1);

  test.check(pg1.count() == 1, "Count wrong, got %d expected %d", pg1.count(), 1);
  test.check(pg1.units() == SCALE, "Units wrong, got %d expected %d", pg1.units(), SCALE);
}

// Test multiples.
void
Test_2()
{
  constexpr static int SCALE_1 = 8192;
  constexpr static int SCALE_2 = 512;

  typedef ts::Scalar<SCALE_1> Size_1;
  typedef ts::Scalar<SCALE_2> Size_2;

  TestBox test("TS Scalar Conversion of scales of multiples");
  Size_2 sz_a(2);
  Size_2 sz_b(57);
  Size_2 sz_c(SCALE_1 / SCALE_2);
  Size_2 sz_d(29 * SCALE_1 / SCALE_2);

  auto sz = ts::scaled_up<Size_1>(sz_a);
  test.check(sz.count() == 1, "Rounding up, got %d expected %d", sz.count(), 1);
  sz = ts::scaled_down<Size_1>(sz_a);
  test.check(sz.count() == 0, "Rounding down: got %d expected %d", sz.count(), 0);

  sz = ts::scaled_up<Size_1>(sz_b);
  test.check(sz.count() == 4, "Rounding up, got %d expected %d", sz.count(), 4);
  sz = ts::scaled_down<Size_1>(sz_b);
  test.check(sz.count() == 3, "Rounding down, got %d expected %d", sz.count(), 3);

  sz = ts::scaled_up<Size_1>(sz_c);
  test.check(sz.count() == 1, "Rounding up, got %d expected %d", sz.count(), 1);
  sz = ts::scaled_down<Size_1>(sz_c);
  test.check(sz.count() == 1, "Rounding down, got %d expected %d", sz.count(), 1);

  sz = ts::scaled_up<Size_1>(sz_d);
  test.check(sz.count() == 29, "Rounding up, got %d expected %d", sz.count(), 29);
  sz = ts::scaled_down<Size_1>(sz_d);
  test.check(sz.count() == 29, "Rounding down, got %d expected %d", sz.count(), 29);

  sz   = 119;
  sz_b = sz; // Should be OK because SCALE_1 is an integer multiple of SCALE_2
  //  sz = sz_b; // Should not compile.
  test.check(sz_b.count() == 119 * (SCALE_1 / SCALE_2), "Integral conversion, got %d expected %d", sz_b.count(),
             119 * (SCALE_1 / SCALE_2));
}

// Test common factor.
void
Test_3()
{
  constexpr static int SCALE_1 = 30;
  constexpr static int SCALE_2 = 20;

  typedef ts::Scalar<SCALE_1> Size_1;
  typedef ts::Scalar<SCALE_2> Size_2;

  TestBox test("TS Scalar common factor conversions");
  Size_2 sz_a(2);
  Size_2 sz_b(97);

  auto sz = ts::scaled_up<Size_1>(sz_a);
  test.check(sz.count() == 2, "Rounding up, got %d expected %d", sz.count(), 2);
  sz = ts::scaled_down<Size_1>(sz_a);
  test.check(sz.count() == 1, "Rounding down: got %d expected %d", sz.count(), 0);

  sz = ts::scaled_up<Size_1>(sz_b);
  test.check(sz.count() == 65, "Rounding up, got %d expected %d", sz.count(), 65);
  sz = ts::scaled_down<Size_1>(sz_b);
  test.check(sz.count() == 64, "Rounding down, got %d expected %d", sz.count(), 64);
}

void
Test_4()
{
  TestBox test("TS Scalar: relatively prime tests");

  ts::Scalar<9> m_9;
  ts::Scalar<4> m_4, m_test;

  m_9 = 95;
  //  m_4 = m_9; // Should fail to compile with static assert.
  //  m_9 = m_4; // Should fail to compile with static assert.

  m_4 = ts::scaled_up<decltype(m_4)>(m_9);
  test.check(m_4.count() == 214, "Rounding down, got %d expected %d", m_4.count(), 214);
  m_4 = ts::scaled_down<decltype(m_4)>(m_9);
  test.check(m_4.count() == 213, "Rounding down, got %d expected %d", m_4.count(), 213);

  m_4 = 213;
  m_9 = ts::scaled_up<decltype(m_9)>(m_4);
  test.check(m_9.count() == 95, "Rounding down, got %d expected %d", m_9.count(), 95);
  m_9 = ts::scaled_down<decltype(m_9)>(m_4);
  test.check(m_9.count() == 94, "Rounding down, got %d expected %d", m_9.count(), 94);

  m_test = m_4; // Verify assignment of identical scale values compiles.
  test.check(m_test.count() == 213, "Assignment got %d expected %d", m_4.count(), 213);
}

void
Test_5()
{
  TestBox test("TS Scalar: arithmetic operator tests");

  typedef ts::Scalar<1024> KBytes;
  typedef ts::Scalar<1, int64_t> Bytes;
  typedef ts::Scalar<1024 * KBytes::SCALE> MBytes;

  Bytes bytes(96);
  KBytes kbytes(2);
  MBytes mbytes(5);

  Bytes z1 = bytes + 128;
  test.check(z1.count() == 224, "Addition got %ld expected %d", z1.count(), 224);
  KBytes z2 = kbytes + 3;
  test.check(z2.count() == 5, "Addition got %d expected %d", z2.count(), 5);
  Bytes z3(bytes);
  z3 += kbytes;
  test.check(z3.units() == 2048 + 96, "Addition got %ld expected %d", z3.units(), 2048 + 96);
  MBytes z4 = mbytes;
  z4 += 5;
  z2 += z4;
  test.check(z2.units() == ((10 << 20) + (5 << 10)), "Addition got %d expected %d", z2.units(), (10 << 20) + (2 << 10));

  z1 += 128;
  test.check(z1.count() == 352, "Addition got %ld expected %d", z1.count(), 352);
}

void
test_Compile()
{
  // These tests aren't normally run, they exist to detect compiler issues.

  typedef ts::Scalar<1024, long int> KBytes;
  typedef ts::Scalar<1024, int> KiBytes;

  KBytes x(12);
  KiBytes y(12);

  if (x > 12)
    std::cout << "Operator > works" << std::endl;
  if (y > 12)
    std::cout << "Operator > works" << std::endl;
}

int
main(int, char **)
{
  Test_1();
  Test_2();
  Test_3();
  Test_4();
  Test_5();
  TestBox::print_summary();
  return 0;
}
