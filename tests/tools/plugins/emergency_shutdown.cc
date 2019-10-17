/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstring>
#include <stdlib.h> // for abort
#include <ts/ts.h>  // for debug

// debug messages viewable by setting 'proxy.config.diags.debug.tags'
// in 'records.config'

// debug messages during one-time initialization
static const char DEBUG_TAG[] = "TSEmergency_test";

// plugin registration info
static char plugin_name[]   = "TSEmergency_test";
static char vendor_name[]   = "apache";
static char support_email[] = "duke8253@apache.org";

static int
test_handler(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG, "failed to shutdown");
  return 0;
}

static int
LifecycleHookTracer(TSCont contp, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_TASK_THREADS_READY) {
    TSCont contp = TSContCreate(test_handler, TSMutexCreate());
    TSContScheduleOnPool(contp, 500, TS_THREAD_POOL_NET);
    TSEmergency("testing emergency shutdown");
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = plugin_name;
  info.vendor_name   = vendor_name;
  info.support_email = support_email;

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSEmergency("[%s] plugin registration failed", plugin_name);
  }

  TSLifecycleHookAdd(TS_LIFECYCLE_TASK_THREADS_READY_HOOK, TSContCreate(LifecycleHookTracer, TSMutexCreate()));
}
