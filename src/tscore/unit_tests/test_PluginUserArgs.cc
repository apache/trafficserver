/**
  @file Test for Regex.cc

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

#include "tscore/PluginUserArgs.h"
#include "catch.hpp"

TEST_CASE("get_user_arg_offset", "[libts][PluginUserArgs]")
{
  CHECK(get_user_arg_offset(TS_USER_ARGS_TXN) == 1000);
  CHECK(get_user_arg_offset(TS_USER_ARGS_SSN) == 2000);
  CHECK(get_user_arg_offset(TS_USER_ARGS_VCONN) == 3000);
  CHECK(get_user_arg_offset(TS_USER_ARGS_GLB) == 4000);
}

TEST_CASE("SanityCheckUserIndex", "[libts][PluginUserArgs]")
{
  CHECK_FALSE(SanityCheckUserIndex(TS_USER_ARGS_TXN, 0));
  CHECK_FALSE(SanityCheckUserIndex(TS_USER_ARGS_TXN, 1));
  CHECK_FALSE(SanityCheckUserIndex(TS_USER_ARGS_TXN, 999));
  CHECK_FALSE(SanityCheckUserIndex(TS_USER_ARGS_TXN, 2000));

  CHECK(SanityCheckUserIndex(TS_USER_ARGS_TXN, 1000));
  CHECK(SanityCheckUserIndex(TS_USER_ARGS_TXN, 1001));
  CHECK(SanityCheckUserIndex(TS_USER_ARGS_TXN, 1999));

  CHECK_FALSE(SanityCheckUserIndex(TS_USER_ARGS_SSN, 1000));
  CHECK_FALSE(SanityCheckUserIndex(TS_USER_ARGS_SSN, 3000));
  CHECK(SanityCheckUserIndex(TS_USER_ARGS_SSN, 2000));
  CHECK(SanityCheckUserIndex(TS_USER_ARGS_SSN, 2001));
  CHECK(SanityCheckUserIndex(TS_USER_ARGS_SSN, 2999));

  CHECK_FALSE(SanityCheckUserIndex(TS_USER_ARGS_VCONN, 2000));
  CHECK(SanityCheckUserIndex(TS_USER_ARGS_VCONN, 3000));

  CHECK_FALSE(SanityCheckUserIndex(TS_USER_ARGS_GLB, 3000));
  CHECK(SanityCheckUserIndex(TS_USER_ARGS_GLB, 4000));
}
