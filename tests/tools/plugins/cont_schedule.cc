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
static const char DEBUG_TAG_INIT[] = "TSContSchedule_test.init";
static const char DEBUG_TAG_SCHD[] = "TSContSchedule_test.schedule";
static const char DEBUG_TAG_HDL[]  = "TSContSchedule_test.handler";
static const char DEBUG_TAG_CHK[]  = "TSContSchedule_test.check";

// plugin registration info
static char plugin_name[]   = "TSContSchedule_test";
static char vendor_name[]   = "apache";
static char support_email[] = "duke8253@apache.org";

static int test_flag = 0;

static TSEventThread test_thread = nullptr;
static TSThread check_thread     = nullptr;

static int TSContSchedule_handler_1(TSCont contp, TSEvent event, void *edata);
static int TSContSchedule_handler_2(TSCont contp, TSEvent event, void *edata);
static int TSContScheduleOnPool_handler_1(TSCont contp, TSEvent event, void *edata);
static int TSContScheduleOnPool_handler_2(TSCont contp, TSEvent event, void *edata);
static int TSContScheduleOnThread_handler_1(TSCont contp, TSEvent event, void *edata);
static int TSContScheduleOnThread_handler_2(TSCont contp, TSEvent event, void *edata);
static int TSContThreadAffinity_handler(TSCont contp, TSEvent event, void *edata);

static int
TSContSchedule_handler_1(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG_HDL, "TSContSchedule handler 1 thread [%p]", TSThreadSelf());
  if (test_thread == nullptr) {
    test_thread = TSEventThreadSelf();

    TSCont contp_new = TSContCreate(TSContSchedule_handler_2, TSMutexCreate());

    if (contp_new == nullptr) {
      TSDebug(DEBUG_TAG_HDL, "[%s] could not create continuation", plugin_name);
      abort();
    } else {
      TSDebug(DEBUG_TAG_HDL, "[%s] scheduling continuation", plugin_name);
      TSContThreadAffinitySet(contp_new, test_thread);
      TSContSchedule(contp_new, 0);
      TSContSchedule(contp_new, 100);
    }
  } else if (check_thread == nullptr) {
    TSDebug(DEBUG_TAG_CHK, "fail [schedule delay not applied]");
  } else {
    if (check_thread != TSThreadSelf()) {
      TSDebug(DEBUG_TAG_CHK, "pass [should not be the same thread]");
    } else {
      TSDebug(DEBUG_TAG_CHK, "fail [on the same thread]");
    }
  }
  return 0;
}

static int
TSContSchedule_handler_2(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG_HDL, "TSContSchedule handler 2 thread [%p]", TSThreadSelf());
  if (check_thread == nullptr) {
    check_thread = TSThreadSelf();
  } else if (check_thread == TSThreadSelf()) {
    TSDebug(DEBUG_TAG_CHK, "pass [should be the same thread]");
  } else {
    TSDebug(DEBUG_TAG_CHK, "fail [not the same thread]");
  }
  return 0;
}

static int
TSContScheduleOnPool_handler_1(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG_HDL, "TSContScheduleOnPool handler 1 thread [%p]", TSThreadSelf());
  if (check_thread == nullptr) {
    check_thread = TSThreadSelf();
  } else {
    if (check_thread != TSThreadSelf()) {
      TSDebug(DEBUG_TAG_CHK, "pass [should not be the same thread]");
    } else {
      TSDebug(DEBUG_TAG_CHK, "fail [on the same thread]");
    }
    check_thread = TSThreadSelf();
  }
  return 0;
}

static int
TSContScheduleOnPool_handler_2(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG_HDL, "TSContScheduleOnPool handler 2 thread [%p]", TSThreadSelf());
  if (check_thread == nullptr) {
    check_thread = TSThreadSelf();
  } else {
    if (check_thread != TSThreadSelf()) {
      TSDebug(DEBUG_TAG_CHK, "pass [should not be the same thread]");
    } else {
      TSDebug(DEBUG_TAG_CHK, "fail [on the same thread]");
    }
    check_thread = TSThreadSelf();
  }
  return 0;
}

static int
TSContScheduleOnThread_handler_1(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG_HDL, "TSContScheduleOnThread handler 1 thread [%p]", TSThreadSelf());
  if (test_thread == nullptr) {
    test_thread = TSEventThreadSelf();

    TSCont contp_new = TSContCreate(TSContScheduleOnThread_handler_2, TSMutexCreate());

    if (contp_new == nullptr) {
      TSDebug(DEBUG_TAG_HDL, "[%s] could not create continuation", plugin_name);
      abort();
    } else {
      TSDebug(DEBUG_TAG_HDL, "[%s] scheduling continuation", plugin_name);
      TSContScheduleOnThread(contp_new, 0, test_thread);
      TSContScheduleOnThread(contp_new, 100, test_thread);
    }
  } else if (check_thread == nullptr) {
    TSDebug(DEBUG_TAG_CHK, "fail [schedule delay not applied]");
  } else {
    if (check_thread != TSThreadSelf()) {
      TSDebug(DEBUG_TAG_CHK, "pass [should not be the same thread]");
    } else {
      TSDebug(DEBUG_TAG_CHK, "fail [on the same thread]");
    }
  }
  return 0;
}

static int
TSContScheduleOnThread_handler_2(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG_HDL, "TSContScheduleOnThread handler 2 thread [%p]", TSThreadSelf());
  if (check_thread == nullptr) {
    check_thread = TSThreadSelf();
  } else if (check_thread == TSThreadSelf()) {
    TSDebug(DEBUG_TAG_CHK, "pass [should be the same thread]");
  } else {
    TSDebug(DEBUG_TAG_CHK, "fail [not the same thread]");
  }
  return 0;
}

static int
TSContThreadAffinity_handler(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG_HDL, "TSContThreadAffinity handler thread [%p]", TSThreadSelf());

  test_thread = TSEventThreadSelf();

  if (TSContThreadAffinityGet(contp) != nullptr) {
    TSDebug(DEBUG_TAG_CHK, "pass [affinity thread is not null]");
    TSContThreadAffinityClear(contp);
    if (TSContThreadAffinityGet(contp) == nullptr) {
      TSDebug(DEBUG_TAG_CHK, "pass [affinity thread is cleared]");
      TSContThreadAffinitySet(contp, TSEventThreadSelf());
      if (TSContThreadAffinityGet(contp) == test_thread) {
        TSDebug(DEBUG_TAG_CHK, "pass [affinity thread is set]");
      } else {
        TSDebug(DEBUG_TAG_CHK, "fail [affinity thread is not set]");
      }
    } else {
      TSDebug(DEBUG_TAG_CHK, "fail [affinity thread is not cleared]");
    }
  } else {
    TSDebug(DEBUG_TAG_CHK, "fail [affinity thread is null]");
  }

  return 0;
}

void
TSContSchedule_test()
{
  TSCont contp = TSContCreate(TSContSchedule_handler_1, TSMutexCreate());

  if (contp == nullptr) {
    TSDebug(DEBUG_TAG_SCHD, "[%s] could not create continuation", plugin_name);
    abort();
  } else {
    TSDebug(DEBUG_TAG_SCHD, "[%s] scheduling continuation", plugin_name);
    TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_NET);
    TSContThreadAffinityClear(contp);
    TSContScheduleOnPool(contp, 200, TS_THREAD_POOL_NET);
  }
}

void
TSContScheduleOnPool_test()
{
  TSCont contp_1 = TSContCreate(TSContScheduleOnPool_handler_1, TSMutexCreate());
  TSCont contp_2 = TSContCreate(TSContScheduleOnPool_handler_2, TSMutexCreate());

  if (contp_1 == nullptr || contp_2 == nullptr) {
    TSDebug(DEBUG_TAG_SCHD, "[%s] could not create continuation", plugin_name);
    abort();
  } else {
    TSDebug(DEBUG_TAG_SCHD, "[%s] scheduling continuation", plugin_name);
    TSContScheduleOnPool(contp_1, 0, TS_THREAD_POOL_NET);
    TSContThreadAffinityClear(contp_1);
    TSContScheduleOnPool(contp_1, 100, TS_THREAD_POOL_NET);
    TSContScheduleOnPool(contp_2, 200, TS_THREAD_POOL_TASK);
    TSContThreadAffinityClear(contp_2);
    TSContScheduleOnPool(contp_2, 300, TS_THREAD_POOL_TASK);
  }
}

void
TSContScheduleOnThread_test()
{
  TSCont contp = TSContCreate(TSContScheduleOnThread_handler_1, TSMutexCreate());

  if (contp == nullptr) {
    TSDebug(DEBUG_TAG_SCHD, "[%s] could not create continuation", plugin_name);
    abort();
  } else {
    TSDebug(DEBUG_TAG_SCHD, "[%s] scheduling continuation", plugin_name);
    TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_NET);
    TSContThreadAffinityClear(contp);
    TSContScheduleOnPool(contp, 200, TS_THREAD_POOL_NET);
  }
}

void
TSContThreadAffinity_test()
{
  TSCont contp = TSContCreate(TSContThreadAffinity_handler, TSMutexCreate());

  if (contp == nullptr) {
    TSDebug(DEBUG_TAG_SCHD, "[%s] could not create continuation", plugin_name);
    abort();
  } else {
    TSDebug(DEBUG_TAG_SCHD, "[%s] scheduling continuation", plugin_name);
    TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_NET);
  }
}

static int
LifecycleHookTracer(TSCont contp, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_TASK_THREADS_READY) {
    switch (test_flag) {
    case 1:
      TSContSchedule_test();
      break;
    case 2:
      TSContScheduleOnPool_test();
      break;
    case 3:
      TSContScheduleOnThread_test();
      break;
    case 4:
      TSContThreadAffinity_test();
      break;
    default:
      break;
    }
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  if (argc == 1) {
    TSDebug(DEBUG_TAG_INIT, "initializing plugin for testing TSContSchedule");
    test_flag = 1;
  } else if (argc == 2) {
    int len = strlen(argv[1]);
    if (len == 4 && strncmp(argv[1], "pool", 4) == 0) {
      TSDebug(DEBUG_TAG_INIT, "initializing plugin for testing TSContScheduleOnPool");
      test_flag = 2;
    } else if (len == 6 && strncmp(argv[1], "thread", 6) == 0) {
      TSDebug(DEBUG_TAG_INIT, "initializing plugin for testing TSContScheduleOnThread");
      test_flag = 3;
    } else if (len == 8 && strncmp(argv[1], "affinity", 8) == 0) {
      TSDebug(DEBUG_TAG_INIT, "initializing plugin for testing TSContThreadAffinity");
      test_flag = 4;
    } else {
      goto Lerror;
    }
  } else {
    goto Lerror;
  }

  TSPluginRegistrationInfo info;

  info.plugin_name   = plugin_name;
  info.vendor_name   = vendor_name;
  info.support_email = support_email;

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSDebug(DEBUG_TAG_INIT, "[%s] plugin registration failed", plugin_name);
    abort();
  }

  TSLifecycleHookAdd(TS_LIFECYCLE_TASK_THREADS_READY_HOOK, TSContCreate(LifecycleHookTracer, TSMutexCreate()));

  return;

Lerror:
  TSDebug(DEBUG_TAG_INIT, "[%s] plugin invalid argument", plugin_name);
  abort();
}
