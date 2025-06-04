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

#include "../P_CacheInternal.h"
#include "api/HttpAPIHooks.h"
#include "iocore/net/NetProcessor.h"
#include "records/RecordsConfig.h"
#include "tscore/ink_config.h"
#include "tscore/Layout.h"

#define CATCH_CONFIG_MAIN
#include "main.h"
#include "swoc/swoc_file.h"

#include <unistd.h>
#include <string_view>

#define THREADS        1
#define DIAGS_LOG_FILE "diags.log"

namespace
{

DbgCtl dbg_ctl_cache_test{"cache test"};

} // end anonymous namespace

// Create a new temp directory and return it
std::string
temp_prefix()
{
  char            buffer[PATH_MAX];
  std::error_code err;
  const char     *tmpdir = getenv("TMPDIR");
  if (tmpdir == nullptr) {
    tmpdir = "/tmp";
  }
  snprintf(buffer, sizeof(buffer), "%s/cachetest.XXXXXX", tmpdir);
  ink_assert(cache_vols == 1 || cache_vols == 2);
  auto prefix = swoc::file::path(mkdtemp(buffer));
  bool result = swoc::file::create_directories(prefix / "var" / "trafficserver", err, 0755);
  if (!result) {
    Dbg(dbg_ctl_cache_test, "Failed to create directories for test: %s(%s)", prefix.c_str(), err.message().c_str());
  }
  ink_assert(result);
  if (cache_vols == 2) {
    result = swoc::file::create_directories(prefix / "var" / "trafficserver2", err, 0755);
    if (!result) {
      Dbg(dbg_ctl_cache_test, "Failed to create directories for test: %s(%s)", prefix.c_str(), err.message().c_str());
    }
  }
  ink_assert(result);

  return prefix.string();
}

// Populate the temporary directory with pre-made cache files
static void
populate_cache(const swoc::file::path &prefix)
{
  swoc::file::path src_path{TS_ABS_TOP_SRCDIR};
  std::error_code  ec;
  ink_assert(cache_vols == 2);
  swoc::file::copy(src_path / "src/iocore/cache/unit_tests/var/trafficserver/cache.db", prefix / "var/trafficserver/", ec);
  swoc::file::copy(src_path / "src/iocore/cache/unit_tests/var/trafficserver2/cache.db", prefix / "var/trafficserver2/", ec);
}

void
test_done()
{
  TSSystemState::shut_down_event_system();
}

const char *GLOBAL_DATA = static_cast<char *>(ats_malloc(10 * 1024 * 1024 + 3)); // 10M

#if TS_USE_LINUX_IO_URING

class IOUringLoopTailHandler : public EThread::LoopTailHandler
{
public:
  int
  waitForActivity(ink_hrtime timeout) override
  {
    IOUringContext::local_context()->submit_and_wait(timeout);

    return 0;
  }
  /** Unblock.

  This is required to unblock (wake up) the block created by calling @a cb.
      */
  void
  signalActivity() override
  {
  }

  ~IOUringLoopTailHandler() override {}
} uring_handler;

#endif

struct EventProcessorListener : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase; // inherit constructor

  void
  testRunStarting(Catch::TestRunInfo const &testRunInfo) override
  {
    BaseLogFile *base_log_file = new BaseLogFile("stderr");
    DiagsPtr::set(new Diags(testRunInfo.name, "*" /* tags */, "" /* actions */, base_log_file));
    diags()->activate_taglist("cache.*|agg.*|locks", DiagsTagType_Debug);
    diags()->config.enabled(DiagsTagType_Debug, 1);
    diags()->show_location = SHOW_LOCATION_DEBUG;

    mime_init();
    swoc::file::path prefix = temp_prefix();
    Layout::create(prefix.view());
    if (reuse_existing_cache) {
      populate_cache(prefix);
    }
    RecProcessInit();
    LibRecordsConfigInit();
    ink_net_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
    ink_assert(GLOBAL_DATA != nullptr);

    init_global_http_hooks();

    netProcessor.init();
    eventProcessor.start(THREADS);

    ink_aio_init(AIO_MODULE_PUBLIC_VERSION);

    EThread *thread = new EThread();
    thread->set_specific();
    init_buffer_allocators(0);

#if TS_USE_LINUX_IO_URING
    thread->set_tail_handler(&uring_handler);
#endif

    std::string src_dir       = std::string(TS_ABS_TOP_SRCDIR) + "/src/iocore/cache/unit_tests";
    Layout::get()->sysconfdir = std::move(src_dir);
  }
};
CATCH_REGISTER_LISTENER(EventProcessorListener);

void
init_cache(size_t /* size ATS_UNUSED */, const char * /* name ATS_UNUSED */)
{
  ink_cache_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  cacheProcessor.start();
}

void
build_hdrs(HTTPInfo &info, const char *url, const char *content_type)
{
  HTTPHdr     req;
  HTTPHdr     resp;
  HTTPParser  parser;
  ParseResult err       = ParseResult::ERROR;
  char        buf[1024] = {0};
  const char *start     = buf;
  char       *p         = buf;

  REQUIRE(url != nullptr);

  p += snprintf(p, sizeof(buf) - (p - buf), "GET %s HTTP/1.1\n", url);
  p += snprintf(p, sizeof(buf) - (p - buf), "User-Agent: curl/7.47.0\n");
  p += snprintf(p, sizeof(buf) - (p - buf), "Accept: %s\n", content_type);
  p += snprintf(p, sizeof(buf) - (p - buf), "Vary: Content-type\n");
  p += snprintf(p, sizeof(buf) - (p - buf), "Proxy-Connection: Keep-Alive\n\n");

  req.create(HTTPType::REQUEST);
  http_parser_init(&parser);

  while (true) {
    err = req.parse_req(&parser, &start, p, true);
    if (err != ParseResult::CONT) {
      break;
    }
  }

  ink_assert(err == ParseResult::DONE);

  memset(buf, 0, sizeof(buf));
  p = buf;

  if (content_type == nullptr) {
    content_type = "application/octet-stream";
  }

  p  = buf;
  p += snprintf(p, sizeof(buf) - (p - buf), "HTTP/1.1 200 OK\n");
  p += snprintf(p, sizeof(buf) - (p - buf), "Content-Type: %s\n", content_type);
  p += snprintf(p, sizeof(buf) - (p - buf), "Expires: Fri, 15 Mar 2219 08:55:45 GMT\n");
  p += snprintf(p, sizeof(buf) - (p - buf), "Last-Modified: Thu, 14 Mar 2019 08:47:40 GMT\n\n");

  resp.create(HTTPType::RESPONSE);
  http_parser_init(&parser);
  start = buf;

  while (true) {
    err = resp.parse_resp(&parser, &start, p, true);
    if (err != ParseResult::CONT) {
      break;
    }
  }
  ink_assert(err == ParseResult::DONE);

  info.request_set(&req);
  info.response_set(&resp);

  req.destroy();
  resp.destroy();
}

HttpCacheKey
generate_key(HTTPInfo &info)
{
  HttpCacheKey key;
  Cache::generate_key(&key, info.request_get()->url_get(), 1);
  return key;
}

void
CacheWriteTest::fill_data()
{
  size_t size    = std::min(WRITE_LIMIT, this->_size);
  auto   n       = this->_write_buffer->write(this->_cursor, size);
  this->_size   -= n;
  this->_cursor += n;
}

int
CacheWriteTest::write_event(int event, void *e)
{
  switch (event) {
  case CACHE_EVENT_OPEN_WRITE:
    this->vc = static_cast<CacheVC *>(e);
    /* fall through */
  case CACHE_EVENT_OPEN_WRITE_FAILED:
    this->process_event(event);
    break;
  case VC_EVENT_WRITE_READY:
    this->process_event(event);
    this->fill_data();
    break;
  case VC_EVENT_WRITE_COMPLETE:
    this->process_event(event);
    break;
  default:
    this->close();
    CHECK(false);
    break;
  }
  return 0;
}

void
CacheWriteTest::do_io_write(size_t size)
{
  if (size == 0) {
    size = this->_size;
  }
  this->vc->set_http_info(&this->info);
  this->vio = this->vc->do_io_write(this, size, this->_write_buffer->alloc_reader());
}

int
CacheWriteTest::start_test(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
{
  Dbg(dbg_ctl_cache_test, "start write test");

  HttpCacheKey key;
  key = generate_key(this->info);

  HTTPInfo *old_info = &this->old_info;
  if (!old_info->valid()) {
    old_info = nullptr;
  }

  SET_HANDLER(&CacheWriteTest::write_event);
  cacheProcessor.open_write(this, &key, old_info);
  return 0;
}

int
CacheReadTest::read_event(int event, void *e)
{
  switch (event) {
  case CACHE_EVENT_OPEN_READ:
    this->vc = static_cast<CacheVC *>(e);
    /* fall through */
  case CACHE_EVENT_OPEN_READ_FAILED:
    this->process_event(event);
    break;
  case CACHE_EVENT_OPEN_READ_RWW:
    this->process_event(event);
    break;
  case VC_EVENT_READ_READY: {
    while (this->_reader->block_read_avail()) {
      auto str = this->_reader->block_read_view();
      if (memcmp(str.data(), this->_cursor, str.size()) == 0) {
        this->_reader->consume(str.size());
        this->_cursor += str.size();
        this->process_event(event);
      } else {
        CHECK(false);
        this->close();
        TEST_DONE();
        break;
      }
    }
    break;
  }
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
  case VC_EVENT_READ_COMPLETE:
    this->process_event(event);
    break;
  default:
    CHECK(false);
    this->close();
    break;
  }
  return 0;
}

void
CacheReadTest::do_io_read(size_t size)
{
  if (size == 0) {
    size = this->_size;
  }
  this->vc->get_http_info(&this->read_http_info);
  this->vio = this->vc->do_io_read(this, size, this->_read_buffer);
}

int
CacheReadTest::start_test(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
{
  Dbg(dbg_ctl_cache_test, "start read test");
  HttpCacheKey key;
  key = generate_key(this->info);

  SET_HANDLER(&CacheReadTest::read_event);
  cacheProcessor.open_read(this, &key, static_cast<CacheHTTPHdr *>(this->info.request_get()), &this->params);
  return 0;
}

constexpr size_t WRITE_LIMIT = 1024 * 3;
