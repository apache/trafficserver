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

static int
storage_offline(unsigned argc, const char **argv)
{
  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments == 0) {
    return CtrlCommandUsage("storage offline DEVICE [DEVICE ...]");
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    TSMgmtError error;

    error = TSStorageDeviceCmdOffline(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to take %s offline", file_arguments[0]);
      return CTRL_EX_ERROR;
    }
  }

  return CTRL_EX_OK;
}

int
subcommand_storage(unsigned argc, const char **argv)
{
  const subcommand commands[] = {
    {storage_offline, "offline", "Take one or more storage volumes offline"},
    {CtrlUnimplementedCommand, "status", "Show the storage configuration"},
  };

  return CtrlGenericSubcommand("storage", commands, countof(commands), argc, argv);
}
