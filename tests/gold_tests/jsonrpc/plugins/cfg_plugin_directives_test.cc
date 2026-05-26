/** @file

  Test plugin for TSCfgLoadCtxGetReloadDirectives: verifies that _reload
  directives are extracted by the framework and delivered separately from the
  supplied YAML config content.

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

#include <ts/ts.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <yaml-cpp/yaml.h>

#define PLUGIN_NAME "cfg_plugin_directives_test"

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};

void
config_reload(TSCfgLoadCtx ctx, void * /* data */)
{
  TSCfgLoadCtxInProgress(ctx, PLUGIN_NAME ": processing started");

  TSYaml directives_yaml = TSCfgLoadCtxGetReloadDirectives(ctx);

  if (directives_yaml != nullptr) {
    auto       *directives = reinterpret_cast<YAML::Node *>(directives_yaml);
    std::string version    = "none";

    if ((*directives)["version"]) {
      version = (*directives)["version"].as<std::string>();
    }

    TSCfgLoadCtxAddLog(ctx, TS_CFG_LOG_NOTE, std::string{PLUGIN_NAME ": directive_version="} + version);
    Dbg(dbg_ctl, "Directives present: version=%s", version.c_str());
  } else {
    TSCfgLoadCtxAddLog(ctx, TS_CFG_LOG_NOTE, PLUGIN_NAME ": no_directives");
    Dbg(dbg_ctl, "No directives present");
  }

  TSYaml yaml = TSCfgLoadCtxGetSuppliedYaml(ctx);

  if (yaml != nullptr) {
    auto *node = reinterpret_cast<YAML::Node *>(yaml);

    std::string greeting = "none";
    if ((*node)["greeting"]) {
      greeting = (*node)["greeting"].as<std::string>();
    }

    TSCfgLoadCtxAddLog(ctx, TS_CFG_LOG_NOTE, std::string{PLUGIN_NAME ": content_greeting="} + greeting);
    TSCfgLoadCtxComplete(ctx, PLUGIN_NAME ": RPC reload OK");
    return;
  }

  std::string_view filename = TSCfgLoadCtxGetFilename(ctx);
  if (!filename.empty()) {
    std::ifstream file(std::string{filename});
    if (file.is_open()) {
      std::ostringstream ss;
      ss << file.rdbuf();
      std::string content = ss.str();
      std::string msg     = std::string{PLUGIN_NAME ": file_mode ("} + std::to_string(content.size()) + " bytes)";
      TSCfgLoadCtxAddLog(ctx, TS_CFG_LOG_NOTE, msg);
      TSCfgLoadCtxComplete(ctx, msg);
      return;
    }
  }

  TSCfgLoadCtxComplete(ctx, PLUGIN_NAME ": reload OK (no content)");
}

} // anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  const char *config_path = (argc >= 2) ? argv[1] : "cfg_plugin_directives_test.conf";

  std::string full_path;
  if (config_path[0] == '/') {
    full_path = config_path;
  } else {
    full_path = std::string(TSConfigDirGet()) + "/" + config_path;
  }

  TSCfgRegistrationInfo cfg_info{};
  cfg_info.key         = PLUGIN_NAME;
  cfg_info.config_path = full_path;
  cfg_info.handler     = config_reload;
  cfg_info.data        = nullptr;
  cfg_info.source      = TS_CFG_SOURCE_FILE_AND_RPC;
  cfg_info.is_required = false;
  TSReturnCode ret     = TSCfgRegister(&cfg_info);
  if (ret == TS_SUCCESS) {
    Dbg(dbg_ctl, "TSCfgRegister OK");
  } else {
    TSError("[%s] TSCfgRegister FAILED", PLUGIN_NAME);
  }
}
