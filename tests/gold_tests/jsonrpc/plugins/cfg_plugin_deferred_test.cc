/** @file

  Test plugin for deferred TSCfgLoadCtx completion - handler stores the context
  and completes/fails from a two-stage scheduled continuation on ET_TASK.

  Stage 0: fires 3 seconds after handler returns ("rescheduled work")
  Stage 1: fires 2 seconds after stage 0 ("heavy work simulation")
  Total deferred time: ~5 seconds

  Registration (TSPluginInit):
    TSCfgRegister - registers "cfg_plugin_deferred_test" with FILE_AND_RPC source

  Handler (config_reload):
    Stores TSCfgLoadCtx + behavior flag, schedules stage 0 at 3s, returns
    WITHOUT calling Complete/Fail.

  Behavior selection:
    RPC mode:  {defer_fail: ...} → deferred fail, anything else → deferred success
    File mode: file contains "defer_fail" → deferred fail, otherwise → deferred success

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

#define PLUGIN_NAME "cfg_plugin_deferred_test"

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};

struct DeferredWork {
  TSCfgLoadCtx ctx;
  bool         should_fail{false};
  int          stage{0};
};

int
deferred_handler(TSCont contp, TSEvent /* event */, void * /* edata */)
{
  auto *work = static_cast<DeferredWork *>(TSContDataGet(contp));

  if (work->stage == 0) {
    Dbg(dbg_ctl, "Stage 0: deferred work starting after 3s wait");
    TSCfgLoadCtxAddLog(work->ctx, DL_Note, "cfg_plugin_deferred_test: stage 0 - deferred work starting, simulating heavy work");
    work->stage = 1;
    TSContScheduleOnPool(contp, 2000, TS_THREAD_POOL_TASK);
    return 0;
  }

  Dbg(dbg_ctl, "Stage 1: heavy work done after 2s, completing");
  if (work->should_fail) {
    TSCfgLoadCtxAddLog(work->ctx, DL_Error, "cfg_plugin_deferred_test: stage 1 - heavy work failed");
    TSCfgLoadCtxFail(work->ctx, "cfg_plugin_deferred_test: deferred fail after heavy work");
  } else {
    TSCfgLoadCtxAddLog(work->ctx, DL_Note, "cfg_plugin_deferred_test: stage 1 - heavy work succeeded");
    TSCfgLoadCtxComplete(work->ctx, "cfg_plugin_deferred_test: deferred complete after heavy work");
  }

  delete work;
  TSContDestroy(contp);
  return 0;
}

void
config_reload(TSCfgLoadCtx ctx, void * /* data */)
{
  TSCfgLoadCtxInProgress(ctx, "cfg_plugin_deferred_test: deferring work, will reschedule in 3s");
  TSCfgLoadCtxAddLog(ctx, DL_Note, "cfg_plugin_deferred_test: scheduling two-stage deferred completion (3s + 2s)");

  bool should_fail = false;

  TSYaml yaml = TSCfgLoadCtxGetSuppliedYaml(ctx);
  if (yaml != nullptr) {
    auto *node = reinterpret_cast<YAML::Node *>(yaml);
    if ((*node)["defer_fail"]) {
      should_fail = true;
    }
  } else {
    std::string_view filename = TSCfgLoadCtxGetFilename(ctx);
    if (!filename.empty()) {
      std::ifstream file(std::string{filename});
      if (file.is_open()) {
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();
        if (content.find("defer_fail") != std::string::npos) {
          should_fail = true;
        }
      }
    }
  }

  // NOTE: If ATS shuts down between this schedule and the deferred fire,
  // `work` and `contp` leak. Acceptable for a test plugin (process is
  // exiting) and intentional - tracking shutdown signals would add noise
  // unrelated to what this test exercises.
  auto  *work  = new DeferredWork{ctx, should_fail, 0};
  TSCont contp = TSContCreate(deferred_handler, TSMutexCreate());
  TSContDataSet(contp, work);
  TSContScheduleOnPool(contp, 3000, TS_THREAD_POOL_TASK);

  Dbg(dbg_ctl, "Handler returning without Complete/Fail - stage 0 fires in 3s, stage 1 in 5s total (fail=%d)", should_fail);
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

  const char *config_path = (argc >= 2) ? argv[1] : "cfg_plugin_deferred_test.conf";

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
