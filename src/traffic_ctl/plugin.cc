/** @file

 Plugin related sub commands.

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
plugin_msg(unsigned argc, const char **argv)
{
  if (!CtrlProcessArguments(argc, argv, nullptr, 0) || n_file_arguments != 2) {
    return CtrlCommandUsage("plugin msg TAG DATA");
  }

  TSMgmtError error;

  error = TSLifecycleMessage(file_arguments[0], file_arguments[1], strlen(file_arguments[1]) + 1);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "message '%s' not sent", file_arguments[0]);
    return CTRL_EX_ERROR;
  }

  return CTRL_EX_OK;
}

int
subcommand_plugin(unsigned argc, const char **argv)
{
  const subcommand commands[] = {
    {plugin_msg, "msg", "Send message to plugins - a TAG and the message DATA"},
  };

  return CtrlGenericSubcommand("plugin", commands, countof(commands), argc, argv);
}
