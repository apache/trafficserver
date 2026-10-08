/** @file

  Closing a cache read while the read of the next fragment is in flight.

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

#include "main.h"

#include <cstddef>
#include <memory>

int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

constexpr std::size_t LARGE_FILE   = 10 * 1024 * 1024;
constexpr char        OBJECT_URL[] = "http://www.close-during-read.com/";

class WriteObject : public CacheTestHandler
{
public:
  WriteObject(std::size_t size, const char *url) : CacheTestHandler()
  {
    this->_wt        = new CacheWriteTest(size, this, url);
    this->_wt->mutex = this->mutex;

    SET_HANDLER(&WriteObject::start_test);
  }

  int
  start_test(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
  {
    this_ethread()->schedule_imm(this->_wt);
    return 0;
  }

  void
  handle_cache_event(int event, CacheTestBase *base) override
  {
    switch (event) {
    case CACHE_EVENT_OPEN_WRITE:
      base->do_io_write();
      break;
    case VC_EVENT_WRITE_READY:
      base->reenable();
      break;
    case VC_EVENT_WRITE_COMPLETE:
      base->close();
      delete this;
      break;
    default:
      FAIL("unexpected event " << event << " while writing the object");
      break;
    }
  }
};

class CloseReadDuringIO : public CacheTestHandler
{
public:
  CloseReadDuringIO(std::size_t size, const char *url) : CacheTestHandler(), _size(size), _url(url)
  {
    this->_rt        = new CacheReadTest(size, this, url);
    this->_rt->mutex = this->mutex;

    SET_HANDLER(&CloseReadDuringIO::start_test);
  }

  ~CloseReadDuringIO() override { delete this->_closed_reader; }

  int
  start_test(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
  {
    this_ethread()->schedule_imm(this->_rt);
    return 0;
  }

  // The cache starts reading the next fragment after the READ_READY callback
  // returns, so closing from a separate event closes during that read.
  int
  close_reader(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
  {
    this->_rt->vc->do_io_close();
    this->_rt->vc        = nullptr;
    this->_rt->vio       = nullptr;
    this->_closed_reader = this->_rt;

    this->_rt        = new CacheReadTest(this->_size, this, this->_url);
    this->_rt->mutex = this->mutex;
    this_ethread()->schedule_imm(this->_rt);
    return 0;
  }

  void
  handle_cache_event(int event, CacheTestBase *base) override
  {
    if (base == this->_closed_reader) {
      ++this->events_after_close;
      return;
    }

    switch (event) {
    case CACHE_EVENT_OPEN_READ_RWW:
      break;
    case CACHE_EVENT_OPEN_READ:
      base->do_io_read();
      break;
    case VC_EVENT_READ_READY:
      on_read_ready(base);
      break;
    case VC_EVENT_READ_COMPLETE:
      this->reread_completed = true;
      base->close();
      finish();
      break;
    default:
      FAIL("unexpected event " << event << " while reading the object");
      break;
    }
  }

  int  events_after_close = 0;
  bool reread_completed   = false;

private:
  // The test case owns the spy, so hand off to the next test without deleting.
  void
  finish()
  {
    this->next_test();
    this->next = nullptr;
  }

  void
  on_read_ready(CacheTestBase *base)
  {
    if (this->_closed_reader != nullptr) {
      base->reenable();
    } else if (!this->_close_scheduled) {
      this->_close_scheduled = true;
      SET_HANDLER(&CloseReadDuringIO::close_reader);
      this_ethread()->schedule_imm(this);
    }
  }

  std::size_t    _size;
  const char    *_url;
  bool           _close_scheduled = false;
  CacheTestBase *_closed_reader   = nullptr;
};

class CloseDuringReadInit : public CacheInit
{
public:
  explicit CloseDuringReadInit(CloseReadDuringIO *spy) : _spy(spy) {}

  int
  cache_init_success_callback(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */) override
  {
    auto *write = new WriteObject(LARGE_FILE, OBJECT_URL);

    write->add(this->_spy);
    write->add(new TerminalTest);
    this_ethread()->schedule_imm(write);
    delete this;
    return 0;
  }

private:
  CloseReadDuringIO *_spy;
};

} // namespace

TEST_CASE("Closing a cache read while a fragment read is in flight delivers no further events and leaves the object readable",
          "cache")
{
  init_cache(256 * 1024 * 1024);

  auto spy = std::make_unique<CloseReadDuringIO>(LARGE_FILE, OBJECT_URL);

  this_ethread()->schedule_imm(new CloseDuringReadInit{spy.get()});
  this_thread()->execute();

  CHECK(spy->events_after_close == 0);
  CHECK(spy->reread_completed);
}
