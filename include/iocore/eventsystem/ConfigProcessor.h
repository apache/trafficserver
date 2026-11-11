/** @file

  A brief file description

  @par License

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
#include <memory>

#include "iocore/eventsystem/EventProcessor.h"
#include "iocore/eventsystem/Tasks.h"
#include "records/RecCore.h"

class ProxyMutex;

#define MAX_CONFIGS 100

using ConfigInfo = RefCountObjInHeap;

class ConfigProcessor;
extern ConfigProcessor configProcessor;

class ConfigProcessor
{
public:
  ConfigProcessor();

  enum {
    // The number of seconds to wait before garbage collecting stale ConfigInfo objects. There's
    // no good reason to tune this, outside of regression tests, so don't.
    CONFIG_PROCESSOR_RELEASE_SECS = 60
  };

  /** The current configuration of @a ClassType for the life of one scope.

      On a thread with pins this takes no reference, so it must stay on the stack of the event
      handler that created it; see get_pinned().
   */
  template <typename ClassType, typename ConfigType> struct scoped_config {
    scoped_config()
      : pinned(thread_pins_enabled()),
        ptr(pinned ? static_cast<ConfigType *>(configProcessor.get_pinned(ClassType::config_id())) : ClassType::acquire())
    {
    }
    ~scoped_config()
    {
      if (!pinned) {
        ClassType::release(ptr);
      }
    }
    scoped_config(const scoped_config &)            = delete;
    scoped_config &operator=(const scoped_config &) = delete;
    operator bool() const { return ptr != nullptr; }
    operator const ConfigType *() const { return ptr; }
    const ConfigType *
    operator->() const
    {
      return ptr;
    }

  private:
    bool        pinned;
    ConfigType *ptr;
  };

  unsigned int set(unsigned int id, ConfigInfo *info, unsigned timeout_secs = CONFIG_PROCESSOR_RELEASE_SECS);
  ConfigInfo  *get(unsigned int id);
  void         release(unsigned int id, ConfigInfo *data);

  /** The current configuration in slot @a id, kept alive by the returned handle on any thread.

      On a thread with pins, copies of the handle count against that thread's pin rather than the
      configuration's refcount, which every thread shares.
   */
  std::shared_ptr<ConfigInfo> get_leased(unsigned int id);

  static bool
  thread_pins_enabled()
  {
    return _thread_pins != nullptr;
  }
  /// Let the calling thread pin configurations. It must then call release_stale_pins() between events.
  static void enable_thread_pins();
  /// Release the calling thread's pins on configurations that set() has replaced since they were pinned.
  static void release_stale_pins();
  /// Release all of the calling thread's pins and stop pinning.
  static void disable_thread_pins();

public:
  std::atomic<ConfigInfo *> infos[MAX_CONFIGS] = {nullptr};
  std::atomic<int>          ninfos{0};

private:
  struct ThreadPins;

  /** The current configuration in slot @a id, without taking a reference.

      The calling thread keeps it alive until the thread next runs release_stale_pins(), which the
      event loop does only between events, so the result must not be kept past the current event.
      Requires thread_pins_enabled(), which scoped_config checks before calling it.
   */
  ConfigInfo *get_pinned(unsigned int id);

  /// The calling thread's pin on slot @a id, refreshed if set() replaced the configuration.
  std::shared_ptr<ConfigInfo> &_pin(unsigned int id);

  // Bumped by set() and read by every pinned or leased get, so each slot's counter has a cache line to itself.
  struct alignas(64) Generation {
    std::atomic<uint64_t> value{0};
  };

  Generation _generations[MAX_CONFIGS];

  static inline thread_local ThreadPins *_thread_pins = nullptr;
};
