/** @file

  Config reload execution logic - schedules async reload work on ET_TASK.

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

#include "mgmt/config/ConfigReloadExecutor.h"
#include "mgmt/config/FileManager.h"
#include "mgmt/config/ReloadCoordinator.h"

#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/Tasks.h"
#include "iocore/eventsystem/EventProcessor.h"

#include "tscore/Diags.h"

namespace
{
DbgCtl dbg_ctl_config{"config.reload"};

/**
 * Continuation that executes the actual config reload work.
 * This runs on ET_TASK thread to avoid blocking the main RPC thread.
 */
struct ReloadWorkContinuation : public Continuation {
  int
  handleEvent(int /* etype */, void * /* data */)
  {
    bool failed{false};
    auto current_task = ReloadCoordinator::Get_Instance().get_current_task();

    Dbg(dbg_ctl_config, "Executing config reload work");

    if (current_task) {
      Dbg(dbg_ctl_config, "Reload task token: %.*s", static_cast<int>(current_task->get_token().size()),
          current_task->get_token().data());
    }

    // This will tell each changed file to reread itself. If some module is waiting
    // for a record to be reloaded, it will be notified and the file update will happen
    // at each module's logic.
    // Each module will get a ConfigContext object which will be used to track the reload progress.
    if (auto err = FileManager::instance().rereadConfig(); !err.empty()) {
      Dbg(dbg_ctl_config, "rereadConfig failed");
      failed = true;
    }

    Dbg(dbg_ctl_config, "Invoking plugin callbacks");
    // If any callback was registered (TSMgmtUpdateRegister) for config notifications,
    // then it will eventually be notified.
    FileManager::instance().invokeConfigPluginCallbacks();

    Dbg(dbg_ctl_config, "Reload work completed, failed=%s", failed ? "true" : "false");

    delete this;
    return failed ? EVENT_ERROR : EVENT_DONE;
  }

  ReloadWorkContinuation() : Continuation(new_ProxyMutex()) { SET_HANDLER(&ReloadWorkContinuation::handleEvent); }
};

} // namespace

namespace config
{

void
schedule_reload_work(std::chrono::milliseconds delay)
{
  Dbg(dbg_ctl_config, "Scheduling reload work with %lldms delay", static_cast<long long>(delay.count()));
  eventProcessor.schedule_in(new ReloadWorkContinuation(), HRTIME_MSECONDS(delay.count()), ET_TASK);
}

} // namespace config
