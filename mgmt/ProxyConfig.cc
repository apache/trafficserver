/** @file

  A brief file description

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

#include "libts.h"
#include "ProxyConfig.h"
#include "P_EventSystem.h"
#include "ts/TestBox.h"

ConfigProcessor configProcessor;


void *
config_int_cb(void *data, void *value)
{
  *(int *) data = *(int64_t *) value;
  return NULL;
}

void *
config_float_cb(void *data, void *value)
{
  *(float *) data = *(float *) value;
  return NULL;
}

void *
config_long_long_cb(void *data, void *value)
{
  *(int64_t *) data = *(int64_t *) value;
  return NULL;
}

/////////////////////////////////////////////////////////////
//
//  config_string_alloc_cb()
//
//  configuration callback function. The function is called
//  by the manager when a string configuration variable
//  changed. It allocates new memory for the new data.
//  the old variable is scheduled to be freed using
//  ConfigFreerContinuation which will free the memory
//  used for this variable after long time, assuming that
//  during all this time all the users of this memory will
//  disappear.
/////////////////////////////////////////////////////////////
void *
config_string_alloc_cb(void *data, void *value)
{
  char *_ss = (char *) value;
  char *_new_value = 0;

//#define DEBUG_CONFIG_STRING_UPDATE
#if defined (DEBUG_CONFIG_STRING_UPDATE)
  printf("config callback [new, old] = [%s : %s]\n",
         (_ss) ? (_ss) : (""), (*(char **) data) ? (*(char **) data) : (""));
#endif
  int len = -1;
  if (_ss) {
    len = strlen(_ss);
    _new_value = (char *)ats_malloc(len + 1);
    memcpy(_new_value, _ss, len + 1);
  }

  char *_temp2 = *(char **) data;
  *(char **) data = _new_value;

  // free old data
  if (_temp2 != 0)
    new_Freer(_temp2, HRTIME_DAY);

  return NULL;
}


class ConfigInfoReleaser:public Continuation
{
public:
  ConfigInfoReleaser(unsigned int id, ConfigInfo * info)
    : Continuation(new_ProxyMutex()), m_id(id), m_info(info)
  {
    SET_HANDLER(&ConfigInfoReleaser::handle_event);
  }

  int handle_event(int /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
  {
    configProcessor.release(m_id, m_info);
    delete this;
    return EVENT_DONE;
  }

public:
  unsigned int m_id;
  ConfigInfo *m_info;
};


ConfigProcessor::ConfigProcessor()
  : ninfos(0)
{
  int i;

  for (i = 0; i < MAX_CONFIGS; i++) {
    infos[i] = NULL;
  }
}

unsigned int
ConfigProcessor::set(unsigned int id, ConfigInfo * info, unsigned timeout_secs)
{
  ConfigInfo *old_info;
  int idx;

  if (id == 0) {
    id = ink_atomic_increment((int *) &ninfos, 1) + 1;
    ink_assert(id != 0);
    ink_assert(id <= MAX_CONFIGS);
  }

  // Don't be an idiot and use a zero timeout ...
  ink_assert(timeout_secs > 0);

  // New objects *must* start with a zero refcount. The config
  // processor holds it's own refcount. We should be the only
  // refcount holder at this point.
  ink_release_assert(info->refcount_inc() == 1);

  if (id > MAX_CONFIGS) {
    // invalid index
    Error("[ConfigProcessor::set] invalid index");
    return 0;
  }

  idx = id - 1;

  do {
    old_info = infos[idx];
  } while (!ink_atomic_cas(&infos[idx], old_info, info));

  if (old_info) {
    // The ConfigInfoReleaser now takes our refcount, but
    // someother thread might also have one ...
    ink_assert(old_info->refcount() > 0);
    eventProcessor.schedule_in(new ConfigInfoReleaser(id, old_info), HRTIME_SECONDS(timeout_secs));
  }

  return id;
}

ConfigInfo *
ConfigProcessor::get(unsigned int id)
{
  ConfigInfo *info;
  int idx;

  ink_assert(id != 0);
  ink_assert(id <= MAX_CONFIGS);

  if (id == 0 || id > MAX_CONFIGS) {
    // return NULL, because we of an invalid index
    return NULL;
  }

  idx = id - 1;
  info = infos[idx];

  // Hand out a refcount to the caller. We should still have out
  // own refcount, so it should be at least 2.
  ink_release_assert(info->refcount_inc() > 1);
  return info;
}

void
ConfigProcessor::release(unsigned int id, ConfigInfo * info)
{
  int idx;

  ink_assert(id != 0);
  ink_assert(id <= MAX_CONFIGS);

  if (id == 0 || id > MAX_CONFIGS) {
    // nothing to delete since we have an invalid index
    return;
  }

  idx = id - 1;

  if (info->refcount_dec() == 0) {
    // When we release, we should already have replaced this object in the index.
    ink_release_assert(info != this->infos[idx]);
    delete info;
  }
}

#if TS_HAS_TESTS

enum {
  REGRESSION_CONFIG_FIRST   = 1,   // last config in a sequence
  REGRESSION_CONFIG_LAST    = 2,   // last config in a sequence
  REGRESSION_CONFIG_SINGLE  = 4, // single-owner config
};

struct RegressionConfig : public ConfigInfo
{
  static volatile int nobjects; // count of outstanding RegressionConfig objects (not-reentrant)

  // DeferredCall is a simple function call wrapper that defers itself until the RegressionConfig
  // object count drops below the specified count.
  template <typename CallType>
  struct DeferredCall : public Continuation
  {
    DeferredCall(int _r, CallType _c) : remain(_r), call(_c) {
      SET_HANDLER(&DeferredCall::handleEvent);
    }

    int handleEvent(int event ATS_UNUSED, Event * e) {
      if (RegressionConfig::nobjects > this->remain) {
        e->schedule_in(HRTIME_MSECONDS(500));
        return EVENT_CONT;
      }

      call();
      delete this;
      return EVENT_DONE;
    }

    int       remain; // Number of remaining RegressionConfig objects to wait for.
    CallType  call;
  };

  template <typename CallType>
  static void defer(int count, CallType call) {
      eventProcessor.schedule_in(new RegressionConfig::DeferredCall<CallType>(count, call), HRTIME_MSECONDS(500));
  }

  RegressionConfig(RegressionTest * r, int * ps, unsigned f)
      : test(r), pstatus(ps), flags(f) {
    if (this->flags & REGRESSION_CONFIG_SINGLE) {
      TestBox box(this->test, this->pstatus);
      box.check(this->refcount() == 1, "invalid refcount %d (should be 1)", this->refcount());
    }

    ink_atomic_increment(&nobjects, 1);
  }

  ~RegressionConfig() {
    TestBox box(this->test, this->pstatus);

    box.check(this->refcount() == 0, "invalid refcount %d (should be 0)", this->refcount());

    // If we are the last config to be scheduled, pass the test.
    // Otherwise, verify that the test is still running ...
    if (REGRESSION_CONFIG_LAST & flags) {
      *this->pstatus = REGRESSION_TEST_PASSED;
    } else {
      box.check(*this->pstatus == REGRESSION_TEST_INPROGRESS, "intermediate config out of sequence, *pstatus is %d", *pstatus);
    }

    ink_atomic_increment(&nobjects, -1);
  }

  RegressionTest * test;
  int *     pstatus;
  unsigned  flags;
};

volatile int RegressionConfig::nobjects = 0;

struct ProxyConfig_Set_Completion
{
  ProxyConfig_Set_Completion(int _id, RegressionConfig * _c)
    : configid(_id), config(_c) {
  }

  void operator() (void) const {
    // Push one more RegressionConfig to force the LAST-tagged one to get destroyed.
    rprintf(config->test, "setting LAST config object %p\n", config);
    configProcessor.set(configid, config, 1);
  }

  int configid;
  RegressionConfig * config;
};

// Test that ConfigProcessor::set() correctly releases the old ConfigInfo after a timeout.
EXCLUSIVE_REGRESSION_TEST(ProxyConfig_Set)(RegressionTest * test, int /* atype ATS_UNUSED */, int * pstatus)
{
  int configid = 0;

  *pstatus = REGRESSION_TEST_INPROGRESS;
  RegressionConfig::nobjects = 0;

  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_LAST), 1);

  // Wait until there's only 2 objects remaining, the one in ConfigProcessor, and the one we make here.
  RegressionConfig::defer(2, ProxyConfig_Set_Completion(configid, new RegressionConfig(test, pstatus, 0)));
}

struct ProxyConfig_Release_Completion
{
  ProxyConfig_Release_Completion(int _id, RegressionConfig * _c)
    : configid(_id), config(_c) {
  }

  void operator() (void) const {
    // Release the reference count. Since we were keeping this alive, it should be the last to die.
    configProcessor.release(configid, config);
  }

  int configid;
  RegressionConfig * config;
};

// Test that ConfigProcessor::release() correctly releases the old ConfigInfo across an implicit
// release timeout.
EXCLUSIVE_REGRESSION_TEST(ProxyConfig_Release)(RegressionTest * test, int /* atype ATS_UNUSED */, int * pstatus)
{
  int configid = 0;
  RegressionConfig * config;

  *pstatus = REGRESSION_TEST_INPROGRESS;
  RegressionConfig::nobjects = 0;

  // Set an initial config, then get it back to hold a reference count.
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_LAST), 1);
  config = (RegressionConfig *)configProcessor.get(configid);

  // Now update the config a few times.
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);
  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, REGRESSION_CONFIG_FIRST), 1);

  configid = configProcessor.set(configid, new RegressionConfig(test, pstatus, 0), 1);

  // Defer the release of the object that we held back until there are only 2 left. The one we are holding
  // and the one in the ConfigProcessor. Then releasing the one we hold will trigger the LAST check
  RegressionConfig::defer(2, ProxyConfig_Release_Completion(configid, config));
}

#endif /* TS_HAS_TESTS */
