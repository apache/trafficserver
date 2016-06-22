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

const ArgumentDescription opts[] = {
  {"drain", '-', "Wait for client connections to drain before restarting", "F", &drain, NULL, NULL},
  {"manager", '-', "Restart traffic_manager as well as traffic_server", "F", &manager, NULL, NULL},
};

static int
restart(unsigned argc, const char **argv, unsigned flags)
{
  TSMgmtError error;
  const char *usage = (flags & TS_RESTART_OPT_CLUSTER) ? "cluster restart [OPTIONS]" : "server restart [OPTIONS]";

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
    CtrlMgmtError(error, "%s restart failed", (flags & TS_RESTART_OPT_CLUSTER) ? "cluster" : "server");
    return CTRL_EX_ERROR;
  }

  return CTRL_EX_OK;
}

static int
cluster_restart(unsigned argc, const char **argv)
{
  return restart(argc, argv, TS_RESTART_OPT_CLUSTER);
}

static int
server_restart(unsigned argc, const char **argv)
{
  return restart(argc, argv, TS_RESTART_OPT_NONE);
}

static int
server_backtrace(unsigned argc, const char **argv)
{
  TSMgmtError error;
  TSString trace = NULL;

  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments != 0) {
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
  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments != 0) {
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

int
subcommand_cluster(unsigned argc, const char **argv)
{
  const subcommand commands[] = {
    {cluster_restart, "restart", "Restart the Traffic Server cluster"},
    {CtrlUnimplementedCommand, "status", "Show the cluster status"},
  };

  return CtrlGenericSubcommand("cluster", commands, countof(commands), argc, argv);
}

int
subcommand_server(unsigned argc, const char **argv)
{
  const subcommand commands[] = {
    {server_restart, "restart", "Restart Traffic Server"},
    {server_backtrace, "backtrace", "Show a full stack trace of the traffic_server process"},
    {server_status, "status", "Show the proxy status"},

    /* XXX do the 'shutdown' and 'startup' commands make sense? */
  };

  return CtrlGenericSubcommand("server", commands, countof(commands), argc, argv);
}
