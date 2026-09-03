/** @file

  Test plugin exercising every public TSCfg* API call.

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

#define PLUGIN_NAME "cfg_plugin_test"

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};

struct PluginState {
  std::string config_path;
};

void
config_reload(TSCfgLoadCtx ctx, void *data)
{
  auto *state = static_cast<PluginState *>(data);

  TSCfgLoadCtxInProgress(ctx, "cfg_plugin_test: processing started");
  TSCfgLoadCtxAddLog(ctx, TS_CFG_LOG_NOTE, "cfg_plugin_test: handler entered");

  TSYaml yaml = TSCfgLoadCtxGetSuppliedYaml(ctx);

  if (yaml != nullptr) {
    auto *node = reinterpret_cast<YAML::Node *>(yaml);

    if ((*node)["fail_on_purpose"]) {
      TSCfgLoadCtxAddLog(ctx, TS_CFG_LOG_ERROR, "cfg_plugin_test: fail requested");
      TSCfgLoadCtxFail(ctx, "cfg_plugin_test: fail_on_purpose via RPC");
      return;
    }

    if ((*node)["with_subtask"]) {
      TSCfgLoadCtx child = TSCfgLoadCtxAddSubtask(ctx, "cfg_plugin_test_subtask");
      TSCfgLoadCtxInProgress(child, "subtask working");
      TSCfgLoadCtxAddLog(child, TS_CFG_LOG_NOTE, "cfg_plugin_test: subtask log entry");
      TSCfgLoadCtxComplete(child, "cfg_plugin_test: subtask done");
      TSCfgLoadCtxComplete(ctx, "cfg_plugin_test: parent with subtask done");
      return;
    }

    if ((*node)["subtask_fail"]) {
      TSCfgLoadCtx child = TSCfgLoadCtxAddSubtask(ctx, "cfg_plugin_test_failing_subtask");
      TSCfgLoadCtxInProgress(child, "subtask starting");
      TSCfgLoadCtxFail(child, "cfg_plugin_test: subtask failed on purpose");
      TSCfgLoadCtxComplete(ctx, "cfg_plugin_test: parent ok but subtask failed");
      return;
    }

    if ((*node)["greet"]) {
      std::string greet = (*node)["greet"].as<std::string>();
      TSCfgLoadCtxComplete(ctx, std::string{"cfg_plugin_test: RPC reload OK, greet="} + greet);
      return;
    }

    TSCfgLoadCtxComplete(ctx, "cfg_plugin_test: RPC reload OK (no special keys)");
    return;
  }

  std::string_view filename = TSCfgLoadCtxGetFilename(ctx);
  std::string      filename_str{filename.empty() ? state->config_path : std::string{filename}};

  TSCfgLoadCtxAddLog(ctx, TS_CFG_LOG_NOTE, "cfg_plugin_test: reading file " + filename_str);

  std::string   content;
  std::ifstream file(filename_str);
  if (file.is_open()) {
    std::ostringstream ss;
    ss << file.rdbuf();
    content = ss.str();
  } else {
    TSCfgLoadCtxFail(ctx, "cfg_plugin_test: cannot open config file: " + filename_str);
    return;
  }

  if (content.find("fail_on_purpose") != std::string::npos) {
    TSCfgLoadCtxFail(ctx, "cfg_plugin_test: fail_on_purpose in file");
    return;
  }

  TSCfgLoadCtxComplete(ctx, "cfg_plugin_test: file reload OK (" + std::to_string(content.size()) + " bytes)");
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

  if (argc < 2) {
    TSError("[%s] Usage: cfg_plugin_test.so <config_file> [companion_file]", PLUGIN_NAME);
    return;
  }

  static PluginState state;

  if (argv[1][0] == '/') {
    state.config_path = argv[1];
  } else {
    state.config_path = std::string(TSConfigDirGet()) + "/" + argv[1];
  }

  TSCfgRegistrationInfo cfg_info{};
  cfg_info.key         = PLUGIN_NAME;
  cfg_info.config_path = state.config_path;
  cfg_info.handler     = config_reload;
  cfg_info.data        = &state;
  cfg_info.source      = TS_CFG_SOURCE_FILE_AND_RPC;
  cfg_info.is_required = false;
  TSReturnCode ret     = TSCfgRegister(&cfg_info);
  if (ret == TS_SUCCESS) {
    Dbg(dbg_ctl, "TSCfgRegister OK for '%s'", state.config_path.c_str());
  } else {
    TSError("[%s] TSCfgRegister FAILED for '%s'", PLUGIN_NAME, state.config_path.c_str());
    return;
  }

  ret = TSCfgAttachReloadTrigger(PLUGIN_NAME, "proxy.config.http.insert_age_in_response");
  if (ret == TS_SUCCESS) {
    Dbg(dbg_ctl, "TSCfgAttachReloadTrigger OK");
  } else {
    TSError("[%s] TSCfgAttachReloadTrigger FAILED", PLUGIN_NAME);
  }

  if (argc >= 3) {
    std::string companion;
    if (argv[2][0] == '/') {
      companion = argv[2];
    } else {
      companion = std::string(TSConfigDirGet()) + "/" + argv[2];
    }
    TSCfgFileDependencyInfo dep{};
    dep.key         = PLUGIN_NAME;
    dep.config_path = companion;
    ret             = TSCfgAddFileDependency(&dep);
    if (ret == TS_SUCCESS) {
      Dbg(dbg_ctl, "TSCfgAddFileDependency OK for '%s'", companion.c_str());
    } else {
      TSError("[%s] TSCfgAddFileDependency FAILED for '%s'", PLUGIN_NAME, companion.c_str());
    }
  }
}
