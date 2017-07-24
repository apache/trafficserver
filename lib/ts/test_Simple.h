#if defined TS_TEST_SIMPLE_H_

#error only include once

#else

#define TS_TEST_SIMPLE_H_

#endif

/** @file

    A bare-bones framework for unit testing.

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

/*
Copyright (c) 2016 Walter William Karas
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <cstdlib>
#include <iostream>
#include <vector>

struct Test_base;

std::vector<Test_base *> test_list;

// Function to set breakpoints on for debugging
//
void
pre_break()
{
}

// Base class for all tests.
//
class Test_base
{
public:
  Test_base() : _name(nullptr) { test_list.push_back(this); }

  bool
  operator()()
  {
    pre_break();
    return test();
  }

  const char *
  name() const
  {
    return _name;
  }

protected:
  // Set optional test name, to be displayed if the test fails.
  //
  const void
  name(const char *optional_name)
  {
    _name = optional_name;
  }

private:
  virtual bool
  test()
  {
    return false;
  }

  const char *_name;
};

// Simple named test, the parameter is the name as a string literal.  The body of the test function should follow the
// invocation of this macro.  It should return true if the test succeeds, false if it fails.
//
#define TEST(NAME) TEST_(name(NAME);, __LINE__)

// Simple annonynous test.  The body of the test function should follow the invocation of this macro.  It should return true if the
// test succeeds, false if it fails.
//
#define ATEST TEST_(, __LINE__)

// The definitions/declarations from here down should not be used outside this file.

#define TEST_(CONS_STATEMENT, SUFFIX) TEST2_(CONS_STATEMENT, SUFFIX)

#define TEST2_(CONS_STATEMENT, SUFFIX)     \
  class GenTest##SUFFIX : public Test_base \
  {                                        \
  public:                                  \
    GenTest##SUFFIX() { CONS_STATEMENT }   \
    bool test() override;                  \
  };                                       \
  GenTest##SUFFIX genTest##SUFFIX;         \
  bool GenTest##SUFFIX::test()

namespace SimpleTest
{
unsigned currTestNumber;
}

void
_ink_assert(const char *bool_expr, const char *file_spec, int line)
{
  std::cout << "ink_assert() failed: expression: " << bool_expr << " file: " << file_spec << " line: " << line
            << " test number: " << SimpleTest::currTestNumber << std::endl;

  exit(1);
}

bool
one_test(unsigned tno)
{
  SimpleTest::currTestNumber = tno;

  Test_base *t = test_list[tno];

  if (!((*t)())) {
    std::cout << "Test " << tno;

    if (t->name())
      std::cout << " (" << t->name() << ')';

    std::cout << " failed\n";

    return false;
  }
  return true;
}

int
main(int n_arg, const char *const *arg)
{
  bool success = true;
  if (n_arg < 3) {
    if (n_arg == 2) { // Run the single test whose (zero-base) number was specified on the command line.
      unsigned tno = static_cast<unsigned>(atoi(arg[1]));

      if (tno < test_list.size()) {
        success = one_test(static_cast<unsigned>(tno)) and success;
      } else {
        std::cout << "test number must be less than " << test_list.size() << '\n';
      }
    } else { // Run all the tests.
      for (unsigned tno = 0; tno < test_list.size(); ++tno) {
        success = one_test(static_cast<unsigned>(tno)) and success;
      }
    }
  }

  return success ? 0 : 1;
}
