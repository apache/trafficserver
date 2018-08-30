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

static int
status_get(unsigned argc, const char **argv)
{
  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments < 1) {
    return CtrlCommandUsage("host status HOST  [HOST  ...]", nullptr, 0);
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    CtrlMgmtRecord record;
    TSMgmtError error;
    std::string str = stat_prefix + file_arguments[i];

    for (const char *_reason_tag : Reasons::reasons) {
      std::string _stat = str + "_" + _reason_tag;
      error             = record.fetch(_stat.c_str());
      if (error != TS_ERR_OKAY) {
        CtrlMgmtError(error, "failed to fetch %s", file_arguments[i]);
        return CTRL_EX_ERROR;
      }

      if (REC_TYPE_IS_STAT(record.rclass())) {
        printf("%s %s\n", record.name(), CtrlMgmtRecordValue(record).c_str());
      }
    }
  }

  return CTRL_EX_OK;
}

static int
status_down(unsigned argc, const char **argv)
{
  int down_time     = 0;
  char *reason      = nullptr;
  const char *usage = "host down HOST [OPTIONS]";

  const ArgumentDescription opts[] = {
    {"time", 'I', "number of seconds that a host is marked down", "I", &down_time, nullptr, nullptr},
    // memory is allocated for 'reason', if this option is used
    {"reason", '-', "reason for marking the host down, one of 'manual|active|local'", "S*", &reason, nullptr, nullptr},
  };

  if (!CtrlProcessArguments(argc, argv, opts, countof(opts)) || n_file_arguments < 1) {
    return CtrlCommandUsage(usage, opts, countof(opts));
  }

  // if reason is not set, set it to manual (default)
  if (reason == nullptr) {
    reason = ats_strdup(Reasons::MANUAL);
  }

  if (!Reasons::validReason(reason)) {
    fprintf(stderr, "\nInvalid reason: '%s'\n\n", reason);
    return CtrlCommandUsage(usage, opts, countof(opts));
  }

  TSMgmtError error = TS_ERR_OKAY;
  for (unsigned i = 0; i < n_file_arguments; ++i) {
    error = TSHostStatusSetDown(file_arguments[i], down_time, reason);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to set %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }
  }
  ats_free(reason);

  return CTRL_EX_OK;
}
static int
status_up(unsigned argc, const char **argv)
{
  char *reason      = nullptr;
  const char *usage = "host down HOST [OPTIONS]";

  const ArgumentDescription opts[] = {
    // memory is allocated for 'reason', if this option is used
    {"reason", '-', "reason for marking the host down, one of 'manual|active|local'", "S*", &reason, nullptr, nullptr},
  };

  if (!CtrlProcessArguments(argc, argv, opts, countof(opts)) || n_file_arguments < 1) {
    return CtrlCommandUsage(usage, nullptr, 0);
  }

  // if reason is not set, set it to manual (default)
  if (reason == nullptr) {
    reason = ats_strdup(Reasons::MANUAL);
  }

  if (!Reasons::validReason(reason)) {
    fprintf(stderr, "\nInvalid reason: '%s'\n\n", reason);
    return CtrlCommandUsage(usage, opts, countof(opts));
  }

  TSMgmtError error;
  for (unsigned i = 0; i < n_file_arguments; ++i) {
    error = TSHostStatusSetUp(file_arguments[i], 0, reason);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to set %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }
  }
  ats_free(reason);

  return CTRL_EX_OK;
}

int
subcommand_host(unsigned argc, const char **argv)
{
  const subcommand commands[] = {
    {status_get, "status", "Get one or more host statuses"},
    {status_down, "down", "Set down one or more host(s) "},
    {status_up, "up", "Set up one or more host(s) "},

  };

  return CtrlGenericSubcommand("host", commands, countof(commands), argc, argv);
}
