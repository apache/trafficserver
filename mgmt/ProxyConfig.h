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

#pragma once

#include <atomic>

#include "ProcessManager.h"
#include "I_Tasks.h"

class ProxyMutex;

void *config_int_cb(void *data, void *value);
void *config_long_long_cb(void *data, void *value);
void *config_float_cb(void *data, void *value);
void *config_string511_cb(void *data, void *value);
void *config_string_alloc_cb(void *data, void *value);

//
// Macros that spin waiting for the data to be bound
//
#define SignalManager(_n, _d) pmgmt->signalManager(_n, (char *)_d)
#define SignalWarning(_n, _s) \
  {                           \
    Warning("%s", _s);        \
    SignalManager(_n, _s);    \
  }

#define RegisterMgmtCallback(_signal, _fn, _data) pmgmt->registerMgmtCallback(_signal, _fn, _data)

#define MAX_CONFIGS 100

typedef RefCountObj ConfigInfo;

class ConfigProcessor
{
public:
  ConfigProcessor() = default;

  enum {
    // The number of seconds to wait before garbage collecting stale ConfigInfo objects. There's
    // no good reason to tune this, outside of regression tests, so don't.
    CONFIG_PROCESSOR_RELEASE_SECS = 60
  };

  template <typename ClassType, typename ConfigType> struct scoped_config {
    scoped_config() : ptr(ClassType::acquire()) {}
    ~scoped_config() { ClassType::release(ptr); }
    operator bool() const { return ptr != nullptr; }
    operator const ConfigType *() const { return ptr; }
    const ConfigType *
    operator->() const
    {
      return ptr;
    }

  private:
    ConfigType *ptr;
  };

  unsigned int set(unsigned int id, ConfigInfo *info, unsigned timeout_secs = CONFIG_PROCESSOR_RELEASE_SECS);
  ConfigInfo *get(unsigned int id);
  void release(unsigned int id, ConfigInfo *data);

public:
  std::atomic<ConfigInfo *> infos[MAX_CONFIGS] = {nullptr};
  std::atomic<int> ninfos{0};
};

// A Continuation wrapper that calls the static reconfigure() method of the given class.
template <typename UpdateClass> struct ConfigUpdateContinuation : public Continuation {
  int
  update(int /* etype */, void * /* data */)
  {
    UpdateClass::reconfigure();
    delete this;
    return EVENT_DONE;
  }

  ConfigUpdateContinuation(Ptr<ProxyMutex> &m) : Continuation(m.get()) { SET_HANDLER(&ConfigUpdateContinuation::update); }
};

template <typename UpdateClass>
int
ConfigScheduleUpdate(Ptr<ProxyMutex> &mutex)
{
  eventProcessor.schedule_imm(new ConfigUpdateContinuation<UpdateClass>(mutex), ET_TASK);
  return 0;
}

template <typename UpdateClass> struct ConfigUpdateHandler {
  ConfigUpdateHandler() : mutex(new_ProxyMutex()) {}
  // The mutex member is ref-counted so should not explicitly free it
  ~ConfigUpdateHandler() {}
  int
  attach(const char *name)
  {
    return REC_RegisterConfigUpdateFunc(name, ConfigUpdateHandler::update, this);
  }

private:
  static int
  update(const char *name, RecDataT /* data_type ATS_UNUSED */, RecData /* data ATS_UNUSED */, void *cookie)
  {
    ConfigUpdateHandler *self = static_cast<ConfigUpdateHandler *>(cookie);

    Debug("config", "%s(%s)", __PRETTY_FUNCTION__, name);
    return ConfigScheduleUpdate<UpdateClass>(self->mutex);
  }

  Ptr<ProxyMutex> mutex;
};

extern ConfigProcessor configProcessor;
