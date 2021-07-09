/*
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

#include "Plugins.h"
#include "rpc/handlers/common/ErrorUtils.h"

#include "InkAPIInternal.h"

namespace
{
static constexpr auto logTag{"rpc.plugins"};
struct PluginMsgInfo {
  std::string data;
  std::string tag;
};
} // namespace
namespace YAML
{
template <> struct convert<PluginMsgInfo> {
  static bool
  decode(Node const &node, PluginMsgInfo &msg)
  {
    if (!node["tag"] || !node["data"]) {
      return false;
    }
    msg.tag  = node["tag"].as<std::string>();
    msg.data = node["data"].as<std::string>();

    return true;
  }
};
} // namespace YAML

namespace rpc::handlers::plugins
{
namespace err = rpc::handlers::errors;

ts::Rv<YAML::Node>
plugin_send_basic_msg(std::string_view const &id, YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;
  try {
    // keep the data.
    PluginMsgInfo info = params.as<PluginMsgInfo>();

    TSPluginMsg msg;
    msg.tag       = info.tag.c_str();
    msg.data      = info.data.data();
    msg.data_size = info.data.size();

    APIHook *hook = lifecycle_hooks->get(TS_LIFECYCLE_MSG_HOOK);

    while (hook) {
      TSPluginMsg tmp(msg); // Just to make sure plugins don't mess this up for others.
      hook->invoke(TS_EVENT_LIFECYCLE_MSG, &tmp);
      hook = hook->next();
    }
  } catch (std::exception const &ex) {
    Debug(logTag, "Invalid params %s", ex.what());
    resp = err::make_errata(err::Codes::PLUGIN, "Error parsing the incoming data: {}", ex.what());
  }

  return resp;
}
} // namespace rpc::handlers::plugins
