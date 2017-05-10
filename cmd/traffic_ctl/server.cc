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

static int drain   = 0;
static int manager = 0;

static int
restart(unsigned argc, const char **argv)
{
  TSMgmtError error;
  const char *usage = "server restart [OPTIONS]";
  unsigned flags    = TS_RESTART_OPT_NONE;

  const ArgumentDescription opts[] = {
    {"drain", '-', "Wait for client connections to drain before restarting", "F", &drain, nullptr, nullptr},
    {"manager", '-', "Restart traffic_manager as well as traffic_server", "F", &manager, nullptr, nullptr},
  };

  if (!CtrlProcessArguments(argc, argv, opts, countof(opts)) || n_file_arguments != 0) {
    return CtrlCommandUsage(usage, opts, countof(opts));
  }

  if (drain) {
    flags |= TS_RESTART_OPT_DRAIN;
  }

  if (manager) {
    error = TSRestart(flags);
  } else {
    error = TSBounce(flags);
  }

  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server restart failed");
    return CTRL_EX_ERROR;
  }

  return CTRL_EX_OK;
}

static int
server_restart(unsigned argc, const char **argv)
{
  return restart(argc, argv);
}

static int
server_backtrace(unsigned argc, const char **argv)
{
  TSMgmtError error;
  TSString trace = nullptr;

  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("server backtrace");
  }

  error = TSProxyBacktraceGet(0, &trace);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server backtrace failed");
    return CTRL_EX_ERROR;
  }

  printf("%s\n", trace);
  TSfree(trace);
  return CTRL_EX_OK;
}

static int
server_status(unsigned argc, const char **argv)
{
  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("server status");
  }

  switch (TSProxyStateGet()) {
  case TS_PROXY_ON:
    printf("Proxy -- on\n");
    break;
  case TS_PROXY_OFF:
    printf("Proxy -- off\n");
    break;
  case TS_PROXY_UNDEFINED:
    printf("Proxy status undefined\n");
    break;
  }

  // XXX Surely we can report more useful status that this !?!!

  return CTRL_EX_OK;
}

static int
server_stop(unsigned argc, const char **argv)
{
  TSMgmtError error;

  // I am not sure whether it really makes sense to add the --drain option here.
  // TSProxyStateSet() is a synchronous API, returning only after the proxy has
  // been shut down. However, draining can take a long time and we don't want
  // to wait for it. Maybe the right approach is to make the stop async.
  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("server stop");
  }

  error = TSProxyStateSet(TS_PROXY_OFF, TS_CACHE_CLEAR_NONE);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server stop failed");
    return CTRL_EX_ERROR;
  }

  return CTRL_EX_OK;
}

static int
server_start(unsigned argc, const char **argv)
{
  TSMgmtError error;
  int cache      = 0;
  int hostdb     = 0;
  unsigned clear = TS_CACHE_CLEAR_NONE;

  const ArgumentDescription opts[] = {
    {"clear-cache", '-', "Clear the disk cache on startup", "F", &cache, nullptr, nullptr},
    {"clear-hostdb", '-', "Clear the DNS cache on startup", "F", &hostdb, nullptr, nullptr},
  };

  if (!CtrlProcessArguments(argc, argv, opts, countof(opts)) || n_file_arguments != 0) {
    return CtrlCommandUsage("server start [OPTIONS]", opts, countof(opts));
  }

  clear |= cache ? TS_CACHE_CLEAR_CACHE : TS_CACHE_CLEAR_NONE;
  clear |= hostdb ? TS_CACHE_CLEAR_HOSTDB : TS_CACHE_CLEAR_NONE;

  error = TSProxyStateSet(TS_PROXY_ON, clear);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "server start failed");
    return CTRL_EX_ERROR;
  }

  return CTRL_EX_OK;
}

int
subcommand_server(unsigned argc, const char **argv)
{
  const subcommand commands[] = {
    {server_backtrace, "backtrace", "Show a full stack trace of the traffic_server process"},
    {server_restart, "restart", "Restart Traffic Server"},
    {server_start, "start", "Start the proxy"},
    {server_status, "status", "Show the proxy status"},
    {server_stop, "stop", "Stop the proxy"},
  };

  return CtrlGenericSubcommand("server", commands, countof(commands), argc, argv);
}
