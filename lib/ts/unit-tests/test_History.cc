/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <string_view>

#include "ts/History.h"
#include "BufferWriter.h"
#include "catch.hpp"

using std::string_view;

#define REMEMBER(e, r) history.push_back(MakeSourceLocation(), e, r)
#define SM_REMEMBER(sm, e, r) sm->history.push_back(MakeSourceLocation(), e, r)

// State Machine mock
template <unsigned Count> class SM
{
public:
  History<Count> history;
};

TEST_CASE("History", "[libts][History]")
{
  char buf[128];

  History<HISTORY_DEFAULT_SIZE> history;

  REMEMBER(1, 1);
  REMEMBER(2, 2);
  REMEMBER(3, NO_REENTRANT);

  REQUIRE(history[0].event == 1);
  REQUIRE(history[0].reentrancy == 1);

  REQUIRE(history[1].event == 2);
  REQUIRE(history[1].reentrancy == 2);

  REQUIRE(history[2].event == 3);
  REQUIRE(history[2].reentrancy == static_cast<short>(NO_REENTRANT));

  history[0].location.str(buf, sizeof(buf));
  REQUIRE(string_view{buf} == "test_History.cc:48 (____C_A_T_C_H____T_E_S_T____0)");

  history[1].location.str(buf, sizeof(buf));
  REQUIRE(string_view{buf} == "test_History.cc:49 (____C_A_T_C_H____T_E_S_T____0)");

  ts::LocalBufferWriter<128> w;
  SM<HISTORY_DEFAULT_SIZE> *sm = new SM<HISTORY_DEFAULT_SIZE>;
  SM_REMEMBER(sm, 1, 1);
  SM_REMEMBER(sm, 2, 2);
  SM_REMEMBER(sm, 3, NO_REENTRANT);

  w.print("{}", sm->history[0].location);
  REQUIRE(w.view() == "test_History.cc:69 (____C_A_T_C_H____T_E_S_T____0)");

  w.reset().print("{}", sm->history[1].location);
  REQUIRE(w.view() == "test_History.cc:70 (____C_A_T_C_H____T_E_S_T____0)");

  REQUIRE(sm->history[0].event == 1);
  REQUIRE(sm->history[0].reentrancy == 1);

  REQUIRE(sm->history[1].event == 2);
  REQUIRE(sm->history[1].reentrancy == 2);

  REQUIRE(sm->history[2].event == 3);
  REQUIRE(sm->history[2].reentrancy == static_cast<short>(NO_REENTRANT));

  SM<2> *sm2 = new SM<2>;

  REQUIRE(sm2->history.size() == 0);
  REQUIRE(sm2->history.overflowed() == false);

  SM_REMEMBER(sm2, 1, 1);

  REQUIRE(sm2->history.size() == 1);
  REQUIRE(sm2->history.overflowed() == false);

  SM_REMEMBER(sm2, 2, 2);

  REQUIRE(sm2->history.size() == 2);
  REQUIRE(sm2->history.overflowed() == true);

  SM_REMEMBER(sm2, 3, NO_REENTRANT);

  REQUIRE(sm2->history.size() == 2);
  REQUIRE(sm2->history.overflowed() == true);

  w.reset().print("{}", sm2->history[0].location);
  REQUIRE(w.view() == "test_History.cc:103 (____C_A_T_C_H____T_E_S_T____0)");

  w.reset().print("{}", sm2->history[1].location);
  REQUIRE(w.view() == "test_History.cc:98 (____C_A_T_C_H____T_E_S_T____0)");

  sm2->history.clear();
  REQUIRE(sm2->history.size() == 0);

  delete sm;
  delete sm2;
}
