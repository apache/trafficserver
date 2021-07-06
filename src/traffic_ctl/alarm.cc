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

struct AlarmListPolicy {
  using entry_type = char *;

  static void
  free(entry_type e)
  {
    TSfree(e);
  }

  static entry_type
  cast(void *ptr)
  {
    return static_cast<entry_type>(ptr);
  }
};

using CtrlAlarmList = CtrlMgmtList<AlarmListPolicy>;

void
CtrlEngine::alarm_list()
{
  TSMgmtError error;
  CtrlAlarmList alarms;

  error = TSActiveEventGetMlt(alarms.list);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to fetch active alarms");
    status_code = CTRL_EX_ERROR;
    return;
  }

  while (!alarms.empty()) {
    char *a = alarms.next();
    std::cout << a << std::endl;
    TSfree(a);
  }
}

void
CtrlEngine::alarm_clear()
{
  TSMgmtError error;
  CtrlAlarmList alarms;

  // First get the active alarms ...
  error = TSActiveEventGetMlt(alarms.list);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to fetch active alarms");
    status_code = CTRL_EX_ERROR;
    return;
  }

  // Now resolve them all ...
  while (!alarms.empty()) {
    char *a = alarms.next();

    error = TSEventResolve(a);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to resolve %s", a);
      TSfree(a);
      status_code = CTRL_EX_ERROR;
      return;
    }

    TSfree(a);
  }
}

void
CtrlEngine::alarm_resolve()
{
  TSMgmtError error;
  CtrlAlarmList alarms;

  for (const auto &it : arguments.get("resolve")) {
    error = TSEventResolve(it.c_str());
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to resolve %s", it.c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }
  }
}
