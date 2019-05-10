/** @file

  host.cc

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
#include "HostStatus.h"
#include "records/P_RecUtils.h"

void
CtrlEngine::status_get()
{
  for (const auto &it : arguments.get("status")) {
    CtrlMgmtRecord record;
    TSMgmtError error;
    std::string str = stat_prefix + it;

    error = record.fetch(str.c_str());
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
CtrlEngine::status_down()
{
  const char *usage      = "traffic_ctl host down --reason 'active | local | manual' --time seconds host ....";
  unsigned int down_time = 0;
  std::string reason     = arguments.get("reason").value();
  std::string down       = arguments.get("time").value();

  // if reason is not set, set it to manual (default)
  if (reason.empty()) {
    reason = Reason::MANUAL;
  }
  if (!down.empty()) {
    down_time = atoi(down.c_str());
  }

  if (!Reason::validReason(reason.c_str())) {
    fprintf(stderr, "\nInvalid reason: '%s'\n\n", reason.c_str());
    fprintf(stderr, "Usage: %s\n\n", usage);
    status_code = CTRL_EX_ERROR;
    return;
  }

  TSMgmtError error = TS_ERR_OKAY;
  for (const auto &it : arguments.get("down")) {
    if (strncmp(it.c_str(), "--", 2) == 0) {
      fprintf(stderr, "\nInvalid option: %s\n", it.c_str());
      fprintf(stderr, "Usage: %s\n\n", usage);
      status_code = CTRL_EX_ERROR;
      return;
    }
    error = TSHostStatusSetDown(it.c_str(), down_time, reason.c_str());
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to set %s", it.c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }
  }
}
void
CtrlEngine::status_up()
{
  const char *usage  = "traffic_ctl host up --reason 'active | local | manual' host ....";
  std::string reason = arguments.get("reason").value();

  // if reason is not set, set it to manual (default)
  if (reason.empty()) {
    reason = Reason::MANUAL;
  }

  if (!Reason::validReason(reason.c_str())) {
    fprintf(stderr, "\nInvalid reason: '%s'\n\n", reason.c_str());
    fprintf(stderr, "Usage: %s\n\n", usage);
    status_code = CTRL_EX_ERROR;
    return;
  }

  TSMgmtError error;
  for (const auto &it : arguments.get("up")) {
    error = TSHostStatusSetUp(it.c_str(), 0, reason.c_str());
    if (strncmp("--", it.c_str(), 2) == 0) {
      fprintf(stderr, "\nInvalid option: %s\n", it.c_str());
      fprintf(stderr, "Usage: %s\n\n", usage);
      status_code = CTRL_EX_ERROR;
    }
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to set %s", it.c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }
  }
}
