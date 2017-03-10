/** @file

   MemView testing.

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

#include <ts/MemView.h>
#include <algorithm>
#include <iomanip>
#include <ios>
#include <iostream>
#include <string>

using namespace ts;

template <typename T, typename S>
bool
CheckEqual(T const &lhs, S const &rhs, std::string const &prefix)
{
  bool zret = lhs == rhs;
  if (!zret) {
    std::cout << "FAIL: " << prefix << ": Expected " << lhs << " to be " << rhs << std::endl;
  }
  return zret;
}

bool
Test_1()
{
  std::string text = "01234567";
  StringView a(text);

  std::cout << "Text = |" << a << '|' << std::endl;
  std::cout << "     = |" << std::setw(5) << a << '|' << std::endl;
  std::cout << "     = |" << std::setw(12) << a << '|' << std::endl;
  std::cout << "     = |" << std::setw(12) << std::right << a << '|' << std::endl;
  std::cout << "     = |" << std::setw(12) << std::left << a << '|' << std::endl;
  std::cout << "     = |" << std::setw(12) << std::right << std::setfill('_') << a << '|' << std::endl;
  std::cout << "     = |" << std::setw(12) << std::left << std::setfill('_') << a << '|' << std::endl;
  return true;
}

bool
Test_2()
{
  bool zret = true;
  StringView sva("litt\0ral");
  StringView svb("litt\0ral", StringView::literal);
  StringView svc("litt\0ral", StringView::array);

  zret = zret && CheckEqual(sva.size(), 4U, "strlen constructor");
  zret = zret && CheckEqual(svb.size(), 8U, "literal constructor");
  zret = zret && CheckEqual(svc.size(), 9U, "array constructor");

  return zret;
}

// These tests are purely compile time.
void
Test_Compile()
{
  int i[12];
  char c[29];
  void *x = i, *y = i + 12;
  MemView mvi(i, i + 12);
  MemView mci(c, c + 29);
  MemView mcv(x, y);
}

int
main(int, char *argv[])
{
  bool zret = true;

  zret = zret && Test_1();
  zret = zret && Test_2();

  return zret ? 0 : 1;
}
