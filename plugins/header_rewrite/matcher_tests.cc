/**
  @file Test for matcher.cc

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

#include "matcher.h"
#include "ts/apidefs.h"
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

int
_TSAssert(const char *, const char *, int)
{
  return 0;
}

void
TSError(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

TSHttpStatus
TSHttpHdrStatusGet(TSMBuffer, TSMLoc)
{
  return TS_HTTP_STATUS_OK;
}

TSReturnCode
TSHandleMLocRelease(TSMBuffer, TSMLoc, TSMLoc)
{
  return TS_SUCCESS;
}

const char *
TSHttpHookNameLookup(TSHttpHookID)
{
  return nullptr;
}

TSReturnCode
TSHttpTxnClientReqGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnServerReqGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnClientRespGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnServerRespGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_SUCCESS;
}

TEST_CASE("Matcher", "[plugins][header_rewrite]")
{
  Matchers<std::string> foo(MATCH_EQUAL);
  TSHttpTxn             txn = nullptr;
  TSCont                c   = nullptr;
  Resources             res(txn, c);

  foo.set("FOO", CondModifiers::MOD_NOCASE);
  REQUIRE(foo.test("foo", res) == true);
}

TEST_CASE("MatcherSet", "[plugins][header_rewrite]")
{
  Matchers<std::string> foo(MATCH_SET);
  TSHttpTxn             txn = nullptr;
  TSCont                c   = nullptr;
  Resources             res(txn, c);

  foo.set("foo, bar, baz", CondModifiers::MOD_NOCASE);
  REQUIRE(foo.test("FOO", res) == true);
}
