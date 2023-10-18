/** @file

  Internal SDK stuff

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

#include "EventSystem.h"
#include "URL.h"
#include "Net.h"
#include "HTTP.h"
#include "tscore/List.h"
#include "ConfigProcessor.h"
#include "Cache.h"
#include "Tasks.h"
#include "Plugin.h"

#include "api/APIHook.h"
#include "api/APIHooks.h"
#include "api/FeatureAPIHooks.h"

#include "ts/InkAPIPrivateIOCore.h"
#include "ts/experimental.h"

#include <typeinfo>
#include <filesystem>

/* Some defines that might be candidates for configurable settings later.
 */
using TSMgmtByte = int8_t; // Not for external use

/* ****** Cache Structure ********* */

// For memory corruption detection
enum CacheInfoMagic {
  CACHE_INFO_MAGIC_ALIVE = 0xfeedbabe,
  CACHE_INFO_MAGIC_DEAD  = 0xdeadbeef,
};

struct CacheInfo {
  CryptoHash cache_key;
  CacheFragType frag_type = CACHE_FRAG_TYPE_NONE;
  int len                 = 0;
  char *hostname          = nullptr;
  time_t pin_in_cache     = 0;
  CacheInfoMagic magic    = CACHE_INFO_MAGIC_ALIVE;

  CacheInfo() {}
};

class FileImpl
{
  enum {
    CLOSED = 0,
    READ   = 1,
    WRITE  = 2,
  };

public:
  FileImpl();
  ~FileImpl();

  int fopen(const char *filename, const char *mode);
  void fclose();
  ssize_t fread(void *buf, size_t length);
  ssize_t fwrite(const void *buf, size_t length);
  ssize_t fflush();
  char *fgets(char *buf, size_t length);

public:
  int m_fd;
  int m_mode;
  char *m_buf;
  size_t m_bufsize;
  size_t m_bufpos;
};

struct INKConfigImpl : public ConfigInfo {
  void *mdata;
  TSConfigDestroyFunc m_destroy_func;

  ~INKConfigImpl() override { m_destroy_func(mdata); }
};

struct HttpAltInfo {
  HTTPHdr m_client_req;
  HTTPHdr m_cached_req;
  HTTPHdr m_cached_resp;
  float m_qvalue;
};

class ConfigUpdateCallback : public Continuation
{
public:
  explicit ConfigUpdateCallback(INKContInternal *contp) : Continuation(contp->mutex.get()), m_cont(contp)
  {
    SET_HANDLER(&ConfigUpdateCallback::event_handler);
  }

  int
  event_handler(int, void *)
  {
    if (m_cont->mutex) {
      MUTEX_TRY_LOCK(trylock, m_cont->mutex, this_ethread());
      if (!trylock.is_locked()) {
        eventProcessor.schedule_in(this, HRTIME_MSECONDS(10), ET_TASK);
      } else {
        m_cont->handleEvent(TS_EVENT_MGMT_UPDATE, nullptr);
        delete this;
      }
    } else {
      m_cont->handleEvent(TS_EVENT_MGMT_UPDATE, nullptr);
      delete this;
    }

    return 0;
  }

private:
  INKContInternal *m_cont;
};

class ConfigUpdateCbTable
{
public:
  ConfigUpdateCbTable();
  ~ConfigUpdateCbTable();

  void insert(INKContInternal *contp, const char *name, const char *file_name = nullptr);
  void invoke();
  void invoke(INKContInternal *contp);

private:
  std::unordered_map<std::string, std::tuple<INKContInternal *, std::string, std::filesystem::file_time_type>> cb_table;
};

#include "HttpAPIHooks.h"

class HttpHookState
{
public:
  /// Scope tags for interacting with a live instance.
  enum ScopeTag { GLOBAL, SSN, TXN };

  /// Default Constructor
  HttpHookState() = default;

  /// Initialize the hook state to track up to 3 sources of hooks.
  /// The argument order to this method is used to break priority ties (callbacks from earlier args are invoked earlier)
  /// The order in terms of @a ScopeTag is GLOBAL, SESSION, TRANSACTION.
  void init(TSHttpHookID id, HttpAPIHooks const *global, HttpAPIHooks const *ssn = nullptr, HttpAPIHooks const *txn = nullptr);

  /// Select a hook for invocation and advance the state to the next valid hook
  /// @return nullptr if no current hook.
  APIHook const *getNext();

  /// Get the hook ID
  TSHttpHookID id() const;

protected:
  /// Track the state of one scope of hooks.
  struct Scope {
    APIHook const *_c      = nullptr; ///< Current hook (candidate for invocation).
    APIHook const *_p      = nullptr; ///< Previous hook (already invoked).
    APIHooks const *_hooks = nullptr; ///< Reference to the real hook list

    /// Initialize the scope.
    void init(HttpAPIHooks const *scope, TSHttpHookID id);
    /// Clear the scope.
    void clear();
    /// Return the current candidate.
    APIHook const *candidate();
    /// Advance state to the next hook.
    void operator++();
  };

private:
  TSHttpHookID _id = TS_HTTP_LAST_HOOK; ///< Hook ID.
  Scope _global;                        ///< Chain from global hooks.
  Scope _ssn;                           ///< Chain from session hooks.
  Scope _txn;                           ///< Chain from transaction hooks.
};

inline TSHttpHookID
HttpHookState::id() const
{
  return _id;
}

void api_init();

extern ConfigUpdateCbTable *global_config_cbs;
