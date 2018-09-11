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

#include "tscore/History.h"
#include "tscore/TestBox.h"

#define REMEMBER(e, r)                             \
  {                                                \
    history.push_back(MakeSourceLocation(), e, r); \
  }

#define SM_REMEMBER(sm, e, r)                          \
  {                                                    \
    sm->history.push_back(MakeSourceLocation(), e, r); \
  }

template <unsigned Count> class SM
{
public:
  History<Count> history;
};

REGRESSION_TEST(History_test)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  char buf[128];

  TestBox tb(t, pstatus);
  *pstatus = REGRESSION_TEST_PASSED;

  History<HISTORY_DEFAULT_SIZE> history;
  REMEMBER(1, 1);
  REMEMBER(2, 2);
  REMEMBER(3, NO_REENTRANT);

  tb.check(history[0].event == 1 && history[0].reentrancy == 1, "Checking that event is 1 and reentrancy is 1");
  tb.check(history[1].event == 2 && history[1].reentrancy == 2, "Checking that event is 2 and reentrancy is 2");
  tb.check(history[2].event == 3 && history[2].reentrancy == (short)NO_REENTRANT,
           "Checking that event is 3 and reentrancy is NO_REENTRANT");

  tb.check(strncmp(history[0].location.str(buf, 128), "test_History.cc:51 (RegressionTest_History_test)",
                   strlen("test_History.cc:51 (RegressionTest_History_test)")) == 0,
           "Checking history string");
  tb.check(strncmp(history[1].location.str(buf, 128), "test_History.cc:52 (RegressionTest_History_test)",
                   strlen("test_History.cc:52 (RegressionTest_History_test)")) == 0,
           "Checking history string");

  SM<HISTORY_DEFAULT_SIZE> *sm = new SM<HISTORY_DEFAULT_SIZE>;
  SM_REMEMBER(sm, 1, 1);
  SM_REMEMBER(sm, 2, 2);
  SM_REMEMBER(sm, 3, NO_REENTRANT);

  tb.check(strncmp(sm->history[0].location.str(buf, 128), "test_History.cc:68 (RegressionTest_History_test)",
                   strlen("test_History.cc:68 (RegressionTest_History_test)")) == 0,
           "Checking SM's history string");
  tb.check(strncmp(sm->history[1].location.str(buf, 128), "test_History.cc:69 (RegressionTest_History_test)",
                   strlen("test_History.cc:69 (RegressionTest_History_test)")) == 0,
           "Checking SM's history string");

  tb.check(sm->history[0].event == 1 && sm->history[0].reentrancy == 1, "Checking that SM's event is 1 and reentrancy is 1");
  tb.check(sm->history[1].event == 2 && sm->history[1].reentrancy == 2, "Checking that SM's event is 2 and reentrancy is 2");
  tb.check(sm->history[2].event == 3 && sm->history[2].reentrancy == (short)NO_REENTRANT,
           "Checking that SM's event is 3 and reentrancy is NO_REENTRANT");

  SM<2> *sm2 = new SM<2>;

  tb.check(sm2->history.size() == 0, "Checking that history size is 0");
  tb.check(sm2->history.overflowed() == false, "Checking that history overflowed 1");
  SM_REMEMBER(sm2, 1, 1);
  tb.check(sm2->history.size() == 1, "Checking that history size is 1");
  tb.check(sm2->history.overflowed() == false, "Checking that history overflowed 2");
  SM_REMEMBER(sm2, 2, 2);
  tb.check(sm2->history.size() == 2, "Checking that history size is 2");
  tb.check(sm2->history.overflowed() == true, "Checking that history overflowed 3");
  SM_REMEMBER(sm2, 3, NO_REENTRANT);
  tb.check(sm2->history.size() == 2, "Checking that history size is 2");
  tb.check(sm2->history.overflowed() == true, "Checking that history overflowed 4");

  tb.check(strncmp(sm2->history[0].location.str(buf, 128), "test_History.cc:94 (RegressionTest_History_test)",
                   strlen("test_History.cc:88 (RegressionTest_History_test)")) == 0,
           "Checking history string");

  tb.check(strncmp(sm2->history[1].location.str(buf, 128), "test_History.cc:91 (RegressionTest_History_test)",
                   strlen("test_History.cc:91 (RegressionTest_History_test)")) == 0,
           "Checking history string");

  sm2->history.clear();
  tb.check(sm2->history.size() == 0, "Checking that history pos is 0 after clear");

  delete sm;
  delete sm2;
}
