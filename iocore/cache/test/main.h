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

#include <cstddef>

#include "catch.hpp"

#include "tscore/I_Layout.h"
#include "tscore/Diags.h"
#include "tscore/TSSystemState.h"

#include "RecordsConfig.h"
#include "records/I_RecProcess.h"
#include "P_AIO.h"
#include "P_CacheDisk.h"
#include "P_Net.h"
#include "test/CacheTestHandler.h"
#include "P_Cache.h"

#include <queue>

// redefine BUILD PREFIX
#ifdef TS_BUILD_PREFIX
#undef TS_BUILD_PREFIX
#endif
#define TS_BUILD_PREFIX "./test"

#ifdef TS_BUILD_EXEC_PREFIX
#undef TS_BUILD_EXEC_PREFIX
#endif
#define TS_BUILD_EXEC_PREFIX "./test"

#ifdef TS_BUILD_SYSCONFDIR
#undef TS_BUILD_SYSCONFDIR
#endif
#define TS_BUILD_SYSCONFDIR "./test"

#define SLEEP_TIME 20000

void init_cache(size_t size, const char *name = "cache.db");
void build_hdrs(HTTPInfo &info, const char *url, const char *content_type = "text/html;charset=utf-8");

HttpCacheKey generate_key(HTTPInfo &info);

extern const char *GLOBAL_DATA;
extern size_t const WRITE_LIMIT;

class CacheInit : public Continuation
{
public:
  CacheInit() : Continuation(new_ProxyMutex()) { SET_HANDLER(&CacheInit::init_event); }

  int
  start_event(int event, void *e)
  {
    Debug("cache_test", "cache init successfully");
    this->cache_init_success_callback(event, e);
    return 0;
  }

  int
  init_event(int event, void *e)
  {
    switch (event) {
    case EVENT_INTERVAL:
    case EVENT_IMMEDIATE:
      if (!CacheProcessor::IsCacheReady(CACHE_FRAG_TYPE_HTTP)) {
        this_ethread()->schedule_in(this, SLEEP_TIME);
      } else {
        SET_HANDLER(&CacheInit::start_event);
        this->handleEvent(event, e);
      }
      return 0;
    default:
      CHECK(false);
      TEST_DONE();
      return 0;
    }

    return 0;
  }

  virtual int cache_init_success_callback(int event, void *e) = 0;

  virtual ~CacheInit() {}
};

class CacheTestBase : public Continuation
{
public:
  CacheTestBase(CacheTestHandler *test_handler) : Continuation(new_ProxyMutex()), test_handler(test_handler)
  {
    SET_HANDLER(&CacheTestBase::init_handler);
  }

  int
  init_handler(int event, void *e)
  {
    this->start_test(event, e);
    return 0;
  }

  // test entrance
  virtual int start_test(int event, void *e) = 0;

  void
  process_event(int event)
  {
    this->test_handler->handle_cache_event(event, this);
  }

  virtual void
  reenable()
  {
    if (this->vio) {
      this->vio->reenable();
    }
  }

  int
  terminal_event(int event, void *e)
  {
    delete this;
    return 0;
  }

  void
  close(int error = -1)
  {
    if (this->vc) {
      this->vc->do_io_close(error);
      this->vc  = nullptr;
      this->vio = nullptr;
    }

    SET_HANDLER(&CacheTestBase::terminal_event);
    if (!this->terminal) {
      this->terminal = this_ethread()->schedule_imm(this);
    }
  }

  virtual void
  do_io_read(size_t size = 0)
  {
    REQUIRE(!"should not be called");
  }

  virtual void
  do_io_write(size_t size = 0)
  {
    REQUIRE(!"should not be called");
  }

  Event *terminal                = nullptr;
  CacheVC *vc                    = nullptr;
  VIO *vio                       = nullptr;
  CacheTestHandler *test_handler = nullptr;
};

class CacheWriteTest : public CacheTestBase
{
public:
  CacheWriteTest(size_t size, CacheTestHandler *cont, const char *url = "http://www.scw00.com/") : CacheTestBase(cont), _size(size)
  {
    this->_cursor       = (char *)GLOBAL_DATA;
    this->_write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);

    this->info.create();
    build_hdrs(this->info, url);
  }

  ~CacheWriteTest() override
  {
    if (this->_write_buffer) {
      free_MIOBuffer(this->_write_buffer);
      this->_write_buffer = nullptr;
    }
    info.destroy();
    old_info.destroy();
  }

  int start_test(int event, void *e) override;
  int write_event(int event, void *e);
  void fill_data();
  void do_io_write(size_t size = 0) override;

  HTTPInfo info;
  HTTPInfo old_info;

private:
  size_t _size             = 0;
  char *_cursor            = nullptr;
  MIOBuffer *_write_buffer = nullptr;
};

class CacheReadTest : public CacheTestBase
{
public:
  CacheReadTest(size_t size, CacheTestHandler *cont, const char *url = "http://www.scw00.com/") : CacheTestBase(cont), _size(size)
  {
    this->_cursor      = (char *)GLOBAL_DATA;
    this->_read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    this->_reader      = this->_read_buffer->alloc_reader();

    this->info.create();
    build_hdrs(this->info, url);
  }

  ~CacheReadTest() override
  {
    if (this->_read_buffer) {
      free_MIOBuffer(this->_read_buffer);
      this->_read_buffer = nullptr;
    }
    info.destroy();
  }

  int start_test(int event, void *e) override;
  int read_event(int event, void *e);
  void do_io_read(size_t size = 0) override;

  HTTPInfo info;
  HTTPInfo *read_http_info = nullptr;

private:
  size_t _size            = 0;
  char *_cursor           = nullptr;
  MIOBuffer *_read_buffer = nullptr;
  IOBufferReader *_reader = nullptr;
  OverridableHttpConfigParams params;
};
