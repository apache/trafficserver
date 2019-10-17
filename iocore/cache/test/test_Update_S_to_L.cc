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

#define LARGE_FILE 10 * 1024 * 1024
#define SMALL_FILE 10 * 1024

#include "main.h"

class CacheUpdateReadAgain : public CacheTestHandler
{
public:
  CacheUpdateReadAgain(size_t size, const char *url) : CacheTestHandler()
  {
    this->_rt        = new CacheReadTest(size, this, url);
    this->_rt->mutex = this->mutex;

    SET_HANDLER(&CacheUpdateReadAgain::start_test);
  }

  int
  start_test(int event, void *e)
  {
    REQUIRE(event == EVENT_IMMEDIATE);
    this_ethread()->schedule_imm(this->_rt);
    return 0;
  }

  void
  handle_cache_event(int event, CacheTestBase *base) override
  {
    switch (event) {
    case CACHE_EVENT_OPEN_READ:
      base->do_io_read();
      break;
    case VC_EVENT_READ_READY:
      base->reenable();
      break;
    case VC_EVENT_READ_COMPLETE:
      base->close();
      delete this;
      break;
    default:
      REQUIRE(false);
      break;
    }
  }
};

class CacheUpdate_S_to_L : public CacheTestHandler
{
public:
  CacheUpdate_S_to_L(size_t read_size, size_t write_size, const char *url)
  {
    this->_rt = new CacheReadTest(read_size, this, url);
    this->_wt = new CacheWriteTest(write_size, this, url);

    this->_rt->mutex = this->mutex;
    this->_wt->mutex = this->mutex;

    SET_HANDLER(&CacheUpdate_S_to_L::start_test);
  }

  int
  start_test(int event, void *e)
  {
    REQUIRE(event == EVENT_IMMEDIATE);
    this_ethread()->schedule_imm(this->_rt);
    return 0;
  }

  void
  handle_cache_event(int event, CacheTestBase *base) override
  {
    CacheWriteTest *wt = static_cast<CacheWriteTest *>(this->_wt);
    switch (event) {
    case CACHE_EVENT_OPEN_WRITE:
      base->do_io_write();
      break;
    case VC_EVENT_WRITE_READY:
      base->reenable();
      break;
    case VC_EVENT_WRITE_COMPLETE:
      this->_wt->close();
      this->_wt = nullptr;
      delete this;
      break;
    case CACHE_EVENT_OPEN_READ:
      base->do_io_read();
      wt->old_info.copy(static_cast<HTTPInfo *>(&base->vc->alternate));
      break;
    case VC_EVENT_READ_READY:
      base->reenable();
      break;
    case VC_EVENT_READ_COMPLETE:
      this->_rt->close();
      this->_rt = nullptr;
      this_ethread()->schedule_imm(this->_wt);
      break;
    default:
      REQUIRE(false);
      break;
    }
  }
};

class CacheUpdateInit : public CacheInit
{
public:
  CacheUpdateInit() {}
  int
  cache_init_success_callback(int event, void *e) override
  {
    CacheTestHandler *h        = new CacheTestHandler(SMALL_FILE, "http://www.scw11.com");
    CacheUpdate_S_to_L *update = new CacheUpdate_S_to_L(SMALL_FILE, LARGE_FILE, "http://www.scw11.com");
    CacheUpdateReadAgain *read = new CacheUpdateReadAgain(LARGE_FILE, "http://www.scw11.com");
    TerminalTest *tt           = new TerminalTest;

    h->add(update);
    h->add(read); // read again
    h->add(tt);
    this_ethread()->schedule_imm(h);
    delete this;
    return 0;
  }
};

TEST_CASE("cache write -> read", "cache")
{
  init_cache(256 * 1024 * 1024);
  // large write test
  CacheUpdateInit *init = new CacheUpdateInit;

  this_ethread()->schedule_imm(init);
  this_thread()->execute();
}
