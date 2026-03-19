/** @file

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

#include "plugin.h"
#include "config.h"
#include "userarg.h"

void
reserve_user_arg(PluginConfig &config)
{
  char name[strlen(PLUGIN_NAME) + strlen(config.method.name) + 1];
  name[0] = '\0';
  strcat(name, PLUGIN_NAME);
  strcat(name, config.method.name);

  TSUserArgType type;
  if (config.method.type == Method::Type::CONNECTION_BASED) {
    type = TS_USER_ARGS_VCONN;
  } else {
    type = TS_USER_ARGS_TXN;
  }
  TSUserArgIndexReserve(type, name, "used to pass JAx context between hooks", &config.user_arg_index);
  Dbg(dbg_ctl, "user_arg_name: %s, user_arg_index: %d", name, config.user_arg_index);
}

void
fill_user_arg_index(PluginConfig &config)
{
  char name[strlen(PLUGIN_NAME) + strlen(config.method.name) + 1];
  name[0] = '\0';
  strcat(name, PLUGIN_NAME);
  strcat(name, config.method.name);

  TSUserArgType type;
  if (config.method.type == Method::Type::CONNECTION_BASED) {
    type = TS_USER_ARGS_VCONN;
  } else {
    type = TS_USER_ARGS_TXN;
  }
  TSUserArgIndexNameLookup(type, name, &config.user_arg_index, nullptr);
}

void
set_user_arg(void *container, PluginConfig &config, JAxContext *ctx)
{
  TSUserArgSet(container, config.user_arg_index, static_cast<void *>(ctx));
}

JAxContext *
get_user_arg(void *container, PluginConfig &config)
{
  return static_cast<JAxContext *>(TSUserArgGet(container, config.user_arg_index));
}
