/** @file

  A brief file description

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

#include "I_RecCore.h"

//-------------------------------------------------------------------------
// RecordsConfigRegister
//-------------------------------------------------------------------------

void
RecordsConfigRegister()
{
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.parse_test_2a", NULL, RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.parse_test_2b", NULL, RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.parse_test_3a", NULL, RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.parse_test_3b", NULL, RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.parse_test_4a", NULL, RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.parse_test_4b", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.cb_test_1", "cb_test_1__original", RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.cb_test_2", "cb_test_2__original", RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.local.cb_test_1", "cb_test_1__original", RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.local.cb_test_2", "cb_test_2__original", RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.local.cb_test_3", "cb_test_3__original", RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.link_test_1", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigFloat(RECT_CONFIG, "proxy.config.link_test_2", 0.0f, RECU_DYNAMIC, RECC_NULL, NULL);
  RecRegisterConfigCounter(RECT_CONFIG, "proxy.config.link_test_3", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  // NODE
  RecRegisterStatString(RECT_NODE, "proxy.node.cb_test_1", "cb_test_1__original", RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_NODE, "proxy.node.cb_test_2", "cb_test_2__original", RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_NODE, "proxy.node.cb_test_int", 0, RECP_NON_PERSISTENT);
  RecRegisterStatFloat(RECT_NODE, "proxy.node.cb_test_float", 0.0f, RECP_NON_PERSISTENT);
  RecRegisterStatCounter(RECT_NODE, "proxy.node.cb_test_count", 0, RECP_NON_PERSISTENT);
}
