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

#define DEFAULT_URL "http://www.scw00.com/"

#include "main.h"

class CacheRWWTest;

struct SimpleCont : public Continuation {
  SimpleCont(CacheTestBase *base) : base(base)
  {
    REQUIRE(base != nullptr);
    SET_HANDLER(&SimpleCont::handle_event);
    this->mutex = base->mutex;
  }

  int
  handle_event(int event, void *data)
  {
    Debug("cache_rww_test", "cache write reenable");
    base->reenable();
    delete this;
    return 0;
  }

  CacheTestBase *base = nullptr;
};

class CacheRWWTest : public CacheTestHandler
{
public:
  CacheRWWTest(size_t size, const char *url = DEFAULT_URL) : CacheTestHandler(), _size(size)
  {
    if (size != LARGE_FILE && size != SMALL_FILE) {
      REQUIRE(!"size should be LARGE_FILE or SMALL_FILE");
    }

    this->_rt = new CacheReadTest(size, this, url);
    this->_wt = new CacheWriteTest(size, this, url);

    this->_rt->mutex = this->mutex;
    this->_wt->mutex = this->mutex;

    SET_HANDLER(&CacheRWWTest::start_test);
  }

  void handle_cache_event(int event, CacheTestBase *e) override;
  int start_test(int event, void *e);

  virtual void process_read_event(int event, CacheTestBase *base);
  virtual void process_write_event(int event, CacheTestBase *base);

  void
  close_write(int error = -1)
  {
    if (!this->_wt) {
      return;
    }

    this->_wt->close(error);
    this->_wt = nullptr;
  }

  void
  close_read(int error = -1)
  {
    if (!this->_rt) {
      return;
    }

    this->_rt->close(error);
    this->_rt = nullptr;
  }

protected:
  size_t _size        = 0;
  Event *_read_event  = nullptr;
  bool _is_read_start = false;
  CacheTestBase *_rt  = nullptr;
  CacheTestBase *_wt  = nullptr;
};

int
CacheRWWTest::start_test(int event, void *e)
{
  REQUIRE(event == EVENT_IMMEDIATE);
  this_ethread()->schedule_imm(this->_wt);
  return 0;
}

void
CacheRWWTest::process_write_event(int event, CacheTestBase *base)
{
  switch (event) {
  case CACHE_EVENT_OPEN_WRITE:
    base->do_io_write();
    break;
  case VC_EVENT_WRITE_READY:
    // schedule read test imm
    if (this->_size != SMALL_FILE && !this->_wt->vc->fragment) {
      Debug("cache_rww_test", "cache write reenable");
      base->reenable();
      return;
    }

    if (!this->_is_read_start) {
      if (!this->_read_event) {
        this->_read_event = this_ethread()->schedule_imm(this->_rt);
      }
      return;
    }

    // stop writing for a while and wait for reading
    // data->vio->reenable();
    this_ethread()->schedule_imm(new SimpleCont(base));
    break;
  case VC_EVENT_WRITE_COMPLETE:
    this->close_write();

    break;
  default:
    REQUIRE(event == 0);
    REQUIRE(false);
    this->close_write();
    this->close_read();
    return;
  }

  if (this->_rt) {
    this->_rt->reenable();
  }
}

void
CacheRWWTest::process_read_event(int event, CacheTestBase *base)
{
  switch (event) {
  case CACHE_EVENT_OPEN_READ:
    base->do_io_read();
    break;
  case VC_EVENT_READ_READY:
    Debug("cache_rww_test", "cache read reenable");
    this->_read_event    = nullptr;
    this->_is_read_start = true;
    base->reenable();
    break;
  case VC_EVENT_READ_COMPLETE:
    this->close_read();
    return;

  default:
    REQUIRE(false);
    this->close_write();
    this->close_read();
    return;
  }

  if (this->_wt) {
    this->_wt->reenable();
  }
}

void
CacheRWWTest::handle_cache_event(int event, CacheTestBase *base)
{
  REQUIRE(base != nullptr);

  switch (event) {
  case CACHE_EVENT_OPEN_WRITE_FAILED:
  case CACHE_EVENT_OPEN_WRITE:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    this->process_write_event(event, base);
    break;
  case CACHE_EVENT_OPEN_READ:
  case CACHE_EVENT_OPEN_READ_FAILED:
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    this->process_read_event(event, base);
    break;
  default:
    REQUIRE(false);
    this->close_write();
    this->close_read();
    break;
  }

  if (this->_wt == nullptr && this->_rt == nullptr) {
    delete this;
  }

  return;
}

class CacheRWWErrorTest : public CacheRWWTest
{
public:
  CacheRWWErrorTest(size_t size, const char *url = DEFAULT_URL) : CacheRWWTest(size, url) {}
  void
  process_write_event(int event, CacheTestBase *base) override
  {
    switch (event) {
    case CACHE_EVENT_OPEN_WRITE:
      base->do_io_write();
      break;
    case VC_EVENT_WRITE_READY:
      if (this->_size != SMALL_FILE && !this->_wt->vc->fragment) {
        Debug("cache_rww_test", "cache write reenable");
        base->reenable();
        return;
      }

      if (!this->_is_read_start) {
        if (!this->_read_event) {
          this->_read_event = this_ethread()->schedule_imm(this->_rt);
        }
        return;
      } else {
        this->close_write(100);
        return;
      }
      this_ethread()->schedule_imm(new SimpleCont(base));
      break;

    case VC_EVENT_WRITE_COMPLETE:
      REQUIRE(!"should not happen because the writter aborted");
      this->close_read();
      this->close_write();
      break;
    default:
      REQUIRE(false);
      delete this;
      return;
    }
  }

  void
  process_read_event(int event, CacheTestBase *base) override
  {
    switch (event) {
    case CACHE_EVENT_OPEN_READ:
      this->_read_event    = nullptr;
      this->_is_read_start = true;
      base->do_io_read();
      break;
    case CACHE_EVENT_OPEN_READ_FAILED:
      REQUIRE(this->_size == SMALL_FILE);
      this->close_read();
      return;
    case VC_EVENT_READ_READY:
      base->reenable();
      if (this->_wt) {
        this->_wt->reenable();
      }
      return;

    case VC_EVENT_READ_COMPLETE:
      REQUIRE(!"should not happen because the writter aborted");
      this->close_read();
      this->close_write();
      break;
    case VC_EVENT_ERROR:
    case VC_EVENT_EOS:
      if (this->_size == LARGE_FILE) {
        REQUIRE(base->vio->ndone >= 1 * 1024 * 1024 - sizeof(Doc));
      } else {
        REQUIRE(base->vio->ndone == 0);
      }
      this->close_read();
      break;
    default:
      REQUIRE(event == 0);
      this->close_read();
      this->close_write();
      break;
    }
  }

private:
  bool _is_read_start = false;
};

class CacheRWWEOSTest : public CacheRWWTest
{
public:
  CacheRWWEOSTest(size_t size, const char *url = DEFAULT_URL) : CacheRWWTest(size, url) {}
  /*
   * test this code in openReadMain
   * if (writer_done()) {
      last_collision = nullptr;
      while (dir_probe(&earliest_key, vol, &dir, &last_collision)) {
        // write complete. this could be reached the size we set in do_io_write or someone call do_io_close with -1 (-1 means write
   success) flag if (dir_offset(&dir) == dir_offset(&earliest_dir)) { DDebug("cache_read_agg", "%p: key: %X ReadMain complete: %d",
   this, first_key.slice32(1), (int)vio.ndone); doc_len = vio.ndone; goto Leos;
        }
      }
      // writer abort. server crash. someone call do_io_close() with error flag
      DDebug("cache_read_agg", "%p: key: %X ReadMain writer aborted: %d", this, first_key.slice32(1), (int)vio.ndone);
      goto Lerror;
    }
   *
   */

  void
  process_write_event(int event, CacheTestBase *base) override
  {
    switch (event) {
    case CACHE_EVENT_OPEN_WRITE:
      base->do_io_write();
      break;
    case VC_EVENT_WRITE_READY:
      if (this->_size != SMALL_FILE && !this->_wt->vc->fragment) {
        Debug("cache_rww_test", "cache write reenable");
        base->reenable();
        return;
      }

      if (!this->_is_read_start) {
        if (!this->_read_event) {
          this->_read_event = this_ethread()->schedule_imm(this->_rt);
        }
        return;
      }
      this_ethread()->schedule_imm(new SimpleCont(base));
      break;

    case VC_EVENT_WRITE_COMPLETE:
      this->close_write();
      break;
    default:
      REQUIRE(false);
      delete this;
      return;
    }
  }

  void
  process_read_event(int event, CacheTestBase *base) override
  {
    switch (event) {
    case CACHE_EVENT_OPEN_READ:
      this->_read_event    = nullptr;
      this->_is_read_start = true;
      base->do_io_read(UINT32_MAX);
      break;
    case VC_EVENT_READ_READY:
      base->reenable();
      if (this->_wt) {
        this->_wt->reenable();
      }
      return;

    case VC_EVENT_READ_COMPLETE:
      REQUIRE(!"should not happen because the writter aborted");
      this->close_read();
      this->close_write();
      break;
    case VC_EVENT_EOS:
      this->close_write();
      this->close_read();
      break;
    default:
      REQUIRE(event == 0);
      this->close_read();
      this->close_write();
      break;
    }
  }

private:
  bool _is_read_start = false;
};

class CacheRWWCacheInit : public CacheInit
{
public:
  CacheRWWCacheInit() {}
  int
  cache_init_success_callback(int event, void *e) override
  {
    CacheRWWTest *crww        = new CacheRWWTest(LARGE_FILE);
    CacheRWWErrorTest *crww_l = new CacheRWWErrorTest(LARGE_FILE, "http://www.scw22.com/");
    CacheRWWEOSTest *crww_eos = new CacheRWWEOSTest(LARGE_FILE, "ttp://www.scw44.com/");
    TerminalTest *tt          = new TerminalTest();

    crww->add(crww_l);
    crww->add(crww_eos);
    crww->add(tt);
    this_ethread()->schedule_imm(crww);
    delete this;
    return 0;
  }
};

TEST_CASE("cache rww", "cache")
{
  init_cache(256 * 1024 * 1024);
  cache_config_target_fragment_size = 1 * 1024 * 1024;
  CacheRWWCacheInit *init           = new CacheRWWCacheInit();

  this_ethread()->schedule_imm(init);
  this_ethread()->execute();
}
