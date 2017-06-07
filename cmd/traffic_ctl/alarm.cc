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
    return (entry_type)ptr;
  }
};

using CtrlAlarmList = CtrlMgmtList<AlarmListPolicy>;

static int
alarm_list(unsigned argc, const char **argv)
{
  TSMgmtError error;
  CtrlAlarmList alarms;

  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("alarm list", nullptr, 0);
  }

  error = TSActiveEventGetMlt(alarms.list);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to fetch active alarms");
    return CTRL_EX_ERROR;
  }

  while (!alarms.empty()) {
    char *a = alarms.next();
    printf("%s\n", a);
    TSfree(a);
  }

  return CTRL_EX_OK;
}

static int
alarm_clear(unsigned argc, const char **argv)
{
  TSMgmtError error;
  CtrlAlarmList alarms;

  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("alarm clear", nullptr, 0);
  }

  // First get the active alarms ...
  error = TSActiveEventGetMlt(alarms.list);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to fetch active alarms");
    return CTRL_EX_ERROR;
  }

  // Now resolve them all ...
  while (!alarms.empty()) {
    char *a = alarms.next();

    error = TSEventResolve(a);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to resolve %s", a);
      TSfree(a);
      return CTRL_EX_ERROR;
    }

    TSfree(a);
  }

  return CTRL_EX_OK;
}

static int
alarm_resolve(unsigned argc, const char **argv)
{
  TSMgmtError error;
  CtrlAlarmList alarms;

  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments == 0) {
    return CtrlCommandUsage("alarm resolve ALARM [ALARM ...]", nullptr, 0);
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    error = TSEventResolve(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to resolve %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }
  }

  return CTRL_EX_OK;
}

int
subcommand_alarm(unsigned argc, const char **argv)
{
  const subcommand commands[] = {
    {alarm_clear, "clear", "Clear all current alarms"},
    {alarm_list, "list", "List all current alarms"},

    // Note that we separate resolve one from resolve all for the same reasons that
    // we have "metric zero" and "metric clear".
    {alarm_resolve, "resolve", "Resolve the listed alarms"},
    /* XXX describe a specific alarm? */
    /* XXX raise an alarm? */
  };

  return CtrlGenericSubcommand("alarm", commands, countof(commands), argc, argv);
}
