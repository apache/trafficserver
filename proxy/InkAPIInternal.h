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

#ifndef __INK_API_INTERNAL_H__
#define __INK_API_INTERNAL_H__

#include "P_EventSystem.h"
#include "URL.h"
#include "StatSystem.h"
#include "P_Net.h"
#include "api/ts/ts.h"
#include "api/ts/ts_private_frozen.h"
#include "api/ts/InkAPIPrivateIOCore.h"
#include "HTTP.h"
#include "List.h"
#include "ProxyConfig.h"
#include "P_Cache.h"
class CacheAPIHooks;
extern CacheAPIHooks *cache_global_hooks;



/* ****** Cache Structure ********* */

// For memory corruption detection
enum CacheInfoMagic
{
  CACHE_INFO_MAGIC_ALIVE = 0xfeedbabe,
  CACHE_INFO_MAGIC_DEAD = 0xdeadbeef
};

struct CacheInfo
{
  INK_MD5 cache_key;
  CacheFragType frag_type;
  char *hostname;
  int len;
  time_t pin_in_cache;
  CacheInfoMagic magic;

    CacheInfo()
  {
    frag_type = CACHE_FRAG_TYPE_NONE;
    hostname = NULL;
    len = 0;
    pin_in_cache = 0;
    magic = CACHE_INFO_MAGIC_ALIVE;
  }
};

class FileImpl
{
  enum
  {
    CLOSED = 0,
    READ = 1,
    WRITE = 2
  };

public:
    FileImpl();
   ~FileImpl();

  int fopen(const char *filename, const char *mode);
  void fclose();
  int fread(void *buf, int length);
  int fwrite(const void *buf, int length);
  int fflush();
  char *fgets(char *buf, int length);

public:
  int m_fd;
  int m_mode;
  char *m_buf;
  int m_bufsize;
  int m_bufpos;
};


struct INKConfigImpl:public ConfigInfo
{
  void *mdata;
  INKConfigDestroyFunc m_destroy_func;

    virtual ~ INKConfigImpl()
  {
    m_destroy_func(mdata);
  }
};



struct HttpAltInfo
{
  HTTPHdr m_client_req;
  HTTPHdr m_cached_req;
  HTTPHdr m_cached_resp;
  float m_qvalue;
};

class APIHook
{
public:
  INKContInternal * m_cont;
  int invoke(int event, void *edata);
  APIHook *next() const;
  LINK(APIHook, m_link);
};


class APIHooks
{
public:
  void prepend(INKContInternal * cont);
  void append(INKContInternal * cont);
  APIHook *get();

private:
  Que(APIHook, m_link) m_hooks;
};


class HttpAPIHooks
{
public:
  HttpAPIHooks();
  ~HttpAPIHooks();

  void clear();
  void prepend(INKHttpHookID id, INKContInternal * cont);
  void append(INKHttpHookID id, INKContInternal * cont);
  APIHook *get(INKHttpHookID id);

  // A boolean value to quickly see if
  //   any hooks are set
  int hooks_set;

private:
  APIHooks m_hooks[INK_HTTP_LAST_HOOK];
};


class CacheAPIHooks
{
public:
  CacheAPIHooks();
  ~CacheAPIHooks();

  void clear();
  void prepend(INKCacheHookID id, INKContInternal * cont);
  void append(INKCacheHookID id, INKContInternal * cont);
  APIHook *get(INKCacheHookID id);

  // A boolean value to quickly see if
  //   any hooks are set
  int hooks_set;

private:
    APIHooks m_hooks[INK_HTTP_LAST_HOOK];
};


class ConfigUpdateCallback:public Continuation
{
public:
  ConfigUpdateCallback(INKContInternal * contp)
  :Continuation(contp->mutex), m_cont(contp)
  {
    SET_HANDLER(&ConfigUpdateCallback::event_handler);
  }

  int event_handler(int, void *)
  {
    if (m_cont->mutex != NULL) {
      MUTEX_TRY_LOCK(trylock, m_cont->mutex, this_ethread());
      if (!trylock) {
        eventProcessor.schedule_in(this, HRTIME_MSECONDS(10), ET_NET);
      } else {
        m_cont->handleEvent(INK_EVENT_MGMT_UPDATE, NULL);
        delete this;
      }
    } else {
      m_cont->handleEvent(INK_EVENT_MGMT_UPDATE, NULL);
      delete this;
    }

    return 0;
  }

private:
  INKContInternal * m_cont;
};

class ConfigUpdateCbTable
{
public:
  ConfigUpdateCbTable();
  ~ConfigUpdateCbTable();

  void insert(INKContInternal * contp, const char *name, const char *config_path);
  void invoke(const char *name);
  void invoke(INKContInternal * contp);

private:
    InkHashTable * cb_table;
};

void api_init();

extern HttpAPIHooks *http_global_hooks;
extern ConfigUpdateCbTable *global_config_cbs;

//extern inkcoreapi INKMCOPreload_fp MCOPreload_fp;

#endif /* __INK_API_INTERNAL_H__ */
