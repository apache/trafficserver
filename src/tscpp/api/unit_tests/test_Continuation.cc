/** @file

    Unit tests for Continuation.h.

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
#include "tscpp/api/Continuation.h"

using atscppapi::Continuation;

using atscppapi::ContinueInMemberFunc;

namespace
{
class X
{
private:
  int foo(TSEvent, void *edata);

public:
  using CallFoo = ContinueInMemberFunc<X, &X::foo>;
};

X x;

int
X::foo(TSEvent, void *edata)
{
  REQUIRE(this == &x);

  return 666;
}

} // end anonymous namespace

TEST_CASE("Continuation", "[Cont]")
{
  static X::CallFoo cf(x, nullptr);

  REQUIRE(cf.call(TS_EVENT_IMMEDIATE) == 666);

  REQUIRE(X::CallFoo::once(x, nullptr)->call(TS_EVENT_IMMEDIATE) == 666);
}

// Mocks

namespace
{
TSEventFunc contFuncp = nullptr;

void *contDatap = nullptr;

int dummy;
TSCont const DummyTSCont = reinterpret_cast<TSCont>(&dummy);
} // namespace

TSCont
TSContCreate(TSEventFunc funcp, TSMutex mutexp)
{
  REQUIRE(mutexp == nullptr);
  contFuncp = funcp;
  return DummyTSCont;
}

void *
TSContDataGet(TSCont contp)
{
  REQUIRE(contp == DummyTSCont);
  return contDatap;
}

void
TSContDataSet(TSCont contp, void *data)
{
  REQUIRE(contp == DummyTSCont);
  contDatap = data;
}

void TSContDestroy(TSCont) {}

int
TSContCall(TSCont contp, TSEvent event, void *edata)
{
  REQUIRE(contp == DummyTSCont);
  return contFuncp(contp, event, edata);
}

#include <cstdlib>
#include <iostream>

void
_TSReleaseAssert(const char *text, const char *file, int line)
{
  std::cout << "_TSReleaseAssert: " << text << " File:" << file << " Line:" << line << '\n';

  std::exit(1);
}
