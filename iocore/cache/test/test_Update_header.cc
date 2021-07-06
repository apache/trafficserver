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
    auto rt   = new CacheReadTest(size, this, url);
    rt->mutex = this->mutex;

    rt->info.destroy();
    rt->info.create();
    build_hdrs(rt->info, url, "application/x-javascript");

    this->_rt = rt;

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
      this->validate_content_type(base);
      this->check_fragment_table(base);
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

  void
  validate_content_type(CacheTestBase *base)
  {
    auto rt = dynamic_cast<CacheReadTest *>(base);
    REQUIRE(rt);
    MIMEField *field = rt->read_http_info->m_alt->m_response_hdr.field_find(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    REQUIRE(field);
    int len;
    const char *value = field->value_get(&len);
    REQUIRE(memcmp(value, "application/x-javascript", len) == 0);
  }

  void
  check_fragment_table(CacheTestBase *base)
  {
    REQUIRE(base->vc->alternate.get_frag_table() != nullptr);
    REQUIRE(base->vc->alternate.get_frag_offset_count() != 0);
  }
};

class CacheUpdateHeader : public CacheTestHandler
{
public:
  CacheUpdateHeader(size_t read_size, const char *url)
  {
    auto rt = new CacheReadTest(read_size, this, url);
    auto wt = new CacheWriteTest(read_size, this, url);

    wt->info.destroy();
    wt->info.create();
    build_hdrs(wt->info, url, "application/x-javascript");

    this->_rt = rt;
    this->_wt = wt;

    this->_rt->mutex = this->mutex;
    this->_wt->mutex = this->mutex;

    SET_HANDLER(&CacheUpdateHeader::start_test);
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
      // commit the header change
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
    CacheTestHandler *h        = new CacheTestHandler(LARGE_FILE, "http://www.scw11.com");
    CacheUpdateHeader *update  = new CacheUpdateHeader(LARGE_FILE, "http://www.scw11.com");
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
