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
#include <time.h>

static std::string
timestr(time_t tm)
{
  char buf[32];
  return std::string(ctime_r(&tm, buf));
}

static void
format_record(const CtrlMgmtRecord& record, bool recfmt)
{
  const char * typestr[] = {
    "INT", "COUNTER", "FLOAT", "STRING", "UNDEFINED"
  };

  if (recfmt) {
    // XXX Detect CONFIG or LOCAL ...
    printf("CONFIG %s %s %s\n", record.name(), typestr[record.type()], record.c_str());
  } else {
    printf("%s: %s\n", record.name(), record.c_str());
  }
}

static int
config_get(unsigned argc, const char ** argv)
{
  int recfmt = 0;
  const ArgumentDescription opts[] = {
    { "records", '-', "Emit output in records.config format", "F", &recfmt, NULL, NULL },
  };

  if (!CtrlProcessArguments(argc, argv, opts, countof(opts)) || n_file_arguments < 1) {
    return CtrlCommandUsage("config get [OPTIONS] RECORD [RECORD ...]", opts, countof(opts));
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    CtrlMgmtRecord record;
    TSMgmtError error;

    error = record.fetch(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }

    format_record(record, recfmt);
  }

  return CTRL_EX_OK;
}

static int
config_set(unsigned argc, const char ** argv)
{
  TSMgmtError error;
  TSActionNeedT action;

  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments != 2) {
    return CtrlCommandUsage("config set RECORD VALUE");
  }

  error = TSRecordSet(file_arguments[0], file_arguments[1], &action);
  if (error  != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to set %s", file_arguments[0]);
    return CTRL_EX_ERROR;
  }

  switch (action) {
  case TS_ACTION_SHUTDOWN:
    printf("set %s, full shutdown required\n", file_arguments[0]);
    break;
  case TS_ACTION_RESTART:
    printf("set %s, restart required\n", file_arguments[0]);
    break;
  case TS_ACTION_RECONFIGURE:
    // printf("Set %s, reconfiguration required\n", file_arguments[0]);
    break;
  case TS_ACTION_DYNAMIC:
  default:
    printf("set %s\n", file_arguments[0]);
    break;
  }

  return CTRL_EX_OK;
}

static int
config_match(unsigned argc, const char ** argv)
{
  int recfmt = 0;
  const ArgumentDescription opts[] = {
    { "records", '-', "Emit output in records.config format", "F", &recfmt, NULL, NULL },
  };

  if (!CtrlProcessArguments(argc, argv, opts, countof(opts)) || n_file_arguments < 1) {
    return CtrlCommandUsage("config match [OPTIONS] REGEX [REGEX ...]", opts, countof(opts));
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    CtrlMgmtRecordList reclist;
    TSMgmtError error;

    // XXX filter the results to only match configuration records.

    error = reclist.match(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }

    while (!reclist.empty()) {
      CtrlMgmtRecord record(reclist.next());
      format_record(record, recfmt);
    }
  }

  return CTRL_EX_OK;
}

static int
config_reload(unsigned argc, const char ** argv)
{
  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("config reload");
  }

  TSMgmtError error = TSReconfigure();
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "configuration reload request failed");
    return CTRL_EX_ERROR;
  }

  return CTRL_EX_OK;
}

static int
config_status(unsigned argc, const char ** argv)
{
  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("config status");
  }

  CtrlMgmtRecord version;
  CtrlMgmtRecord configtime;
  CtrlMgmtRecord starttime;
  CtrlMgmtRecord reconfig;
  CtrlMgmtRecord proxy;
  CtrlMgmtRecord manager;
  CtrlMgmtRecord cop;

  CTRL_MGMT_CHECK(version.fetch("proxy.process.version.server.long"));
  CTRL_MGMT_CHECK(starttime.fetch("proxy.node.restarts.proxy.start_time"));
  CTRL_MGMT_CHECK(configtime.fetch("proxy.node.config.reconfigure_time"));
  CTRL_MGMT_CHECK(reconfig.fetch("proxy.node.config.reconfigure_required"));
  CTRL_MGMT_CHECK(proxy.fetch("proxy.node.config.restart_required.proxy"));
  CTRL_MGMT_CHECK(manager.fetch("proxy.node.config.restart_required.manager"));
  CTRL_MGMT_CHECK(cop.fetch("proxy.node.config.restart_required.cop"));

  printf("%s\n", version.c_str());
  printf("Started at %s", timestr((time_t)starttime.as_int()).c_str());
  printf("Last reconfiguration at %s", timestr((time_t)configtime.as_int()).c_str());
  printf("%s\n", reconfig.as_int() ? "Reconfiguration required" : "Configuration is current");

  if (proxy.as_int()) {
    printf("traffic_server requires restarting\n");
  }
  if (manager.as_int()) {
    printf("traffic_manager requires restarting\n");
  }
  if (cop.as_int()) {
    printf("traffic_cop requires restarting\n");
  }

  return CTRL_EX_OK;
}

int
subcommand_config(unsigned argc, const char ** argv)
{
  const subcommand commands[] =
  {
    { CtrlUnimplementedCommand, "describe", "Show detailed information about configuration values" },
    { config_get, "get", "Get one or more configuration values" },
    { config_match, "match", "Get configuration matching a regular expression" },
    { config_reload, "reload", "Request a configuration reload" },
    { config_set, "set", "Set a configuration value" },
    { config_status, "status", "Check the configuration status" },
  };

  return CtrlGenericSubcommand("config", commands, countof(commands), argc, argv);
}
