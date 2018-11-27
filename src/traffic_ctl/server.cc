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
CtrlEngine::server_restart()
{
  TSMgmtError error;
  unsigned flags = TS_RESTART_OPT_NONE;

  if (arguments.get("drain")) {
    flags |= TS_RESTART_OPT_DRAIN;
  }

  if (arguments.get("manager")) {
    error = TSRestart(flags);
  } else {
    error = TSBounce(flags);
  }

  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server restart failed");
    status_code = CTRL_EX_ERROR;
    return;
  }
}

void
CtrlEngine::server_backtrace()
{
  TSMgmtError error;
  TSString trace = nullptr;

  error = TSProxyBacktraceGet(0, &trace);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server backtrace failed");
    status_code = CTRL_EX_ERROR;
    return;
  }

  std::cout << trace << std::endl;
  TSfree(trace);
}

void
CtrlEngine::server_status()
{
  switch (TSProxyStateGet()) {
  case TS_PROXY_ON:
    std::cout << "Proxy -- on" << std::endl;
    break;
  case TS_PROXY_OFF:
    std::cout << "Proxy -- off" << std::endl;
    break;
  case TS_PROXY_UNDEFINED:
    std::cout << "Proxy status undefined" << std::endl;
    break;
  }
}

void
CtrlEngine::server_stop()
{
  TSMgmtError error;
  unsigned flags = TS_RESTART_OPT_NONE;

  if (arguments.get("drain")) {
    flags |= TS_STOP_OPT_DRAIN;
  }

  error = TSStop(flags);

  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server stop failed");
    status_code = CTRL_EX_ERROR;
    return;
  }
}

void
CtrlEngine::server_start()
{
  TSMgmtError error;
  unsigned clear = TS_CACHE_CLEAR_NONE;

  clear |= arguments.get("clear-cache") ? TS_CACHE_CLEAR_CACHE : TS_CACHE_CLEAR_NONE;
  clear |= arguments.get("clear-hostdb") ? TS_CACHE_CLEAR_HOSTDB : TS_CACHE_CLEAR_NONE;

  error = TSProxyStateSet(TS_PROXY_ON, clear);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server start failed");
    status_code = CTRL_EX_ERROR;
    return;
  }
}

void
CtrlEngine::server_drain()
{
  TSMgmtError error;

  if (arguments.get("undo")) {
    error = TSDrain(TS_DRAIN_OPT_UNDO);
  } else if (arguments.get("no-new-connection")) {
    error = TSDrain(TS_DRAIN_OPT_IDLE);
  } else {
    error = TSDrain(TS_DRAIN_OPT_NONE);
  }

  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server drain failed");
    status_code = CTRL_EX_ERROR;
    return;
  }
}
