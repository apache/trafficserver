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

static int
metric_get(unsigned argc, const char **argv)
{
  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments < 1) {
    return CtrlCommandUsage("metric get METRIC [METRIC ...]", nullptr, 0);
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    CtrlMgmtRecord record;
    TSMgmtError error;

    error = record.fetch(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }

    if (REC_TYPE_IS_STAT(record.rclass())) {
      printf("%s %s\n", record.name(), CtrlMgmtRecordValue(record).c_str());
    }
  }

  return CTRL_EX_OK;
}

static int
metric_match(unsigned argc, const char **argv)
{
  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments < 1) {
    return CtrlCommandUsage("metric match [OPTIONS] REGEX [REGEX ...]", nullptr, 0);
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    CtrlMgmtRecordList reclist;
    TSMgmtError error;

    error = reclist.match(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }

    while (!reclist.empty()) {
      CtrlMgmtRecord record(reclist.next());
      if (REC_TYPE_IS_STAT(record.rclass())) {
        printf("%s %s\n", record.name(), CtrlMgmtRecordValue(record).c_str());
      }
    }
  }

  return CTRL_EX_OK;
}

static int
metric_clear(unsigned argc, const char **argv)
{
  TSMgmtError error;

  error = TSStatsReset(nullptr);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to clear metrics");
    return CTRL_EX_ERROR;
  }

  return CTRL_EX_OK;
}

static int
metric_zero(unsigned argc, const char **argv)
{
  TSMgmtError error;

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    error = TSStatsReset(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to clear %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }
  }

  return CTRL_EX_OK;
}

int
subcommand_metric(unsigned argc, const char **argv)
{
  const subcommand commands[] = {
    {metric_get, "get", "Get one or more metric values"},
    {metric_clear, "clear", "Clear all metric values"},
    {CtrlUnimplementedCommand, "describe", "Show detailed information about one or more metric values"},
    {metric_match, "match", "Get metrics matching a regular expression"},
    {CtrlUnimplementedCommand, "monitor", "Display the value of a metric over time"},

    // We could allow clearing all the metrics in the "clear" subcommand, but that seems error-prone. It
    // would be too easy to just expect a help message and accidentally nuke all the metrics.
    {metric_zero, "zero", "Clear one or more metric values"},
  };

  return CtrlGenericSubcommand("metric", commands, countof(commands), argc, argv);
}
