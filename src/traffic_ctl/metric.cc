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
#include "records/P_RecUtils.h"

void
CtrlEngine::metric_get()
{
  for (const auto &it : arguments.get("get")) {
    CtrlMgmtRecord record;
    TSMgmtError error;

    error = record.fetch(it.c_str());
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", it.c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }

    if (REC_TYPE_IS_STAT(record.rclass())) {
      std::cout << record.name() << ' ' << CtrlMgmtRecordValue(record).c_str() << std::endl;
    }
  }
}

void
CtrlEngine::metric_match()
{
  for (const auto &it : arguments.get("match")) {
    CtrlMgmtRecordList reclist;
    TSMgmtError error;

    error = reclist.match(it.c_str());
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", it.c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }

    while (!reclist.empty()) {
      CtrlMgmtRecord record(reclist.next());
      if (REC_TYPE_IS_STAT(record.rclass())) {
        std::cout << record.name() << ' ' << CtrlMgmtRecordValue(record).c_str() << std::endl;
      }
    }
  }
}

void
CtrlEngine::metric_clear()
{
  TSMgmtError error;

  error = TSStatsReset(nullptr);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to clear metrics");
    status_code = CTRL_EX_ERROR;
    return;
  }
}

void
CtrlEngine::metric_zero()
{
  TSMgmtError error;

  for (const auto &it : arguments.get("zero")) {
    error = TSStatsReset(it.c_str());
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to clear %s", it.c_str());
      status_code = CTRL_EX_ERROR;
    }
  }
}
