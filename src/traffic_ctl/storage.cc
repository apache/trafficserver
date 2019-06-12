/** @file

  traffic_ctl

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

#include "traffic_ctl.h"

void
CtrlEngine::storage_offline()
{
  auto offline_data = arguments.get("offline");
  for (const auto &it : offline_data) {
    TSMgmtError error;

    error = TSStorageDeviceCmdOffline(it.c_str());
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to take %s offline", offline_data[0].c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }
  }
}
