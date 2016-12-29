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

#include <ts/Metric.h>
#include <string>
#include <stdarg.h>

namespace ts {
  using namespace ApacheTrafficServer;
}

struct TestBox {
  typedef TestBox self;  ///< Self reference type.
  
  std::string _name;
  
  static int _count;
  static int _fail;

  TestBox(char const* name) : _name(name) {}
  TestBox(std::string const& name) : _name(name) {}
  bool check(bool result, char const *fmt, ...) __attribute__((format(printf, 3, 4)));

  static void print_summary()
  {
    printf("Tests: %d of %d passed - %s\n", (_count - _fail), _count, _fail ? "FAIL" : "SUCCESS");
  }
};

int TestBox::_count = 0;
int TestBox::_fail = 0;

bool
TestBox::check(bool result, char const *fmt, ...)
{
  ++_count;
  
  if (!result) {
    static constexpr size_t N = 1 << 16;
    size_t n = N;
    size_t x;
    char* s;
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

void
Test_1()
{
  constexpr static int SCALE = 4096;
  typedef ts::Metric<SCALE> PageSize;

  TestBox test("TS Metric");
  PageSize pg1(1);

  test.check(pg1.count() == 1, "Count wrong, got %d expected %d", pg1.count(), 1);
  test.check(pg1.units() == SCALE, "Units wrong, got %d expected %d", pg1.units(), SCALE);
}

void
Test_2()
{
  constexpr static int SCALE_1 = 8192;
  constexpr static int SCALE_2 = 512;
  
  typedef ts::Metric<SCALE_1> Size_1;
  typedef ts::Metric<SCALE_2> Size_2;

  TestBox test("TS Metric Conversions");
  Size_2 sz_a(2);
  Size_2 sz_b(57);
  Size_2 sz_c(SCALE_1 / SCALE_2);
  Size_2 sz_d(29 * SCALE_1 / SCALE_2);

  auto sz = ts::metric_round_up<Size_1>(sz_a);
  test.check(sz.count() == 1 , "Rounding up, got %d expected %d", sz.count(), 1);
  sz = ts::metric_round_down<Size_1>(sz_a);
  test.check(sz.count() == 0 , "Rounding down: got %d expected %d", sz.count(), 0);

  sz = ts::metric_round_up<Size_1>(sz_b);
  test.check(sz.count() == 4 , "Rounding up, got %d expected %d", sz.count(), 4);
  sz = ts::metric_round_down<Size_1>(sz_b);
  test.check(sz.count() == 3 , "Rounding down, got %d expected %d", sz.count(), 3);
  
  sz = ts::metric_round_up<Size_1>(sz_c);
  test.check(sz.count() == 1 , "Rounding up, got %d expected %d", sz.count(), 1);
  sz = ts::metric_round_down<Size_1>(sz_c);
  test.check(sz.count() == 1 , "Rounding down, got %d expected %d", sz.count(), 1);
  
  sz = ts::metric_round_up<Size_1>(sz_d);
  test.check(sz.count() == 29 , "Rounding up, got %d expected %d", sz.count(), 29);
  sz = ts::metric_round_down<Size_1>(sz_d);
  test.check(sz.count() == 29 , "Rounding down, got %d expected %d", sz.count(), 29);
}
int
main(int, char **)
{
  Test_1();
  Test_2();
  TestBox::print_summary();
  return 0;
}

