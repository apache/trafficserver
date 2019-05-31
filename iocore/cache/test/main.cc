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

#define CATCH_CONFIG_MAIN
#include "main.h"

#define THREADS 1
#define DIAGS_LOG_FILE "diags.log"

void
test_done()
{
  TSSystemState::shut_down_event_system();
}

const char *GLOBAL_DATA = (char *)ats_malloc(10 * 1024 * 1024 + 3); // 10M

struct EventProcessorListener : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase; // inherit constructor

  virtual void
  testRunStarting(Catch::TestRunInfo const &testRunInfo) override
  {
    BaseLogFile *base_log_file = new BaseLogFile("stderr");
    diags                      = new Diags(testRunInfo.name.c_str(), "*" /* tags */, "" /* actions */, base_log_file);
    diags->activate_taglist("cache.*|agg.*|locks", DiagsTagType_Debug);
    diags->config.enabled[DiagsTagType_Debug] = true;
    diags->show_location                      = SHOW_LOCATION_DEBUG;

    mime_init();
    Layout::create();
    RecProcessInit(RECM_STAND_ALONE);
    LibRecordsConfigInit();
    ink_net_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
    ink_assert(GLOBAL_DATA != nullptr);

    statPagesManager.init(); // mutex needs to be initialized before calling netProcessor.init
    netProcessor.init();
    eventProcessor.start(THREADS);

    ink_aio_init(AIO_MODULE_PUBLIC_VERSION);

    EThread *thread = new EThread();
    thread->set_specific();
    init_buffer_allocators(0);

    std::string src_dir       = std::string(TS_ABS_TOP_SRCDIR) + "/iocore/cache/test";
    Layout::get()->sysconfdir = src_dir;
    Layout::get()->prefix     = src_dir;
    ::remove("./test/var/trafficserver/cache.db");
  }
};
CATCH_REGISTER_LISTENER(EventProcessorListener);

extern Store theCacheStore;

void
init_cache(size_t size, const char *name)
{
  ink_cache_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  cacheProcessor.start();
}

void
build_hdrs(HTTPInfo &info, const char *url, const char *content_type)
{
  HTTPHdr req;
  HTTPHdr resp;
  HTTPParser parser;
  int err           = -1;
  char buf[1024]    = {0};
  const char *start = buf;
  char *p           = buf;

  REQUIRE(url != nullptr);

  p += sprintf(p, "GET %s HTTP/1.1\n", url);
  p += sprintf(p, "User-Agent: curl/7.47.0\n");
  p += sprintf(p, "Accept: %s\n", content_type);
  p += sprintf(p, "Vary: Content-type\n");
  p += sprintf(p, "Proxy-Connection: Keep-Alive\n\n");

  req.create(HTTP_TYPE_REQUEST);
  http_parser_init(&parser);

  while (true) {
    err = req.parse_req(&parser, &start, p, true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }

  ink_assert(err == PARSE_RESULT_DONE);

  memset(buf, 0, sizeof(buf));
  p = buf;

  if (content_type == nullptr) {
    content_type = "application/octet-stream";
  }

  p = buf;
  p += sprintf(p, "HTTP/1.1 200 OK\n");
  p += sprintf(p, "Content-Type: %s\n", content_type);
  p += sprintf(p, "Expires: Fri, 15 Mar 2219 08:55:45 GMT\n");
  p += sprintf(p, "Last-Modified: Thu, 14 Mar 2019 08:47:40 GMT\n\n");

  resp.create(HTTP_TYPE_RESPONSE);
  http_parser_init(&parser);
  start = buf;

  while (true) {
    err = resp.parse_resp(&parser, &start, p, true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }
  ink_assert(err == PARSE_RESULT_DONE);

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
  size_t size = std::min(WRITE_LIMIT, this->_size);
  auto n      = this->_write_buffer->write(this->_cursor, size);
  this->_size -= n;
  this->_cursor += n;
}

int
CacheWriteTest::write_event(int event, void *e)
{
  switch (event) {
  case CACHE_EVENT_OPEN_WRITE:
    this->vc = (CacheVC *)e;
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
CacheWriteTest::start_test(int event, void *e)
{
  Debug("cache test", "start write test");

  HttpCacheKey key;
  key = generate_key(this->info);

  HTTPInfo *old_info = &this->old_info;
  if (!old_info->valid()) {
    old_info = nullptr;
  }

  SET_HANDLER(&CacheWriteTest::write_event);
  cacheProcessor.open_write(this, 0, &key, (CacheHTTPHdr *)this->info.request_get(), old_info);
  return 0;
}

int
CacheReadTest::read_event(int event, void *e)
{
  switch (event) {
  case CACHE_EVENT_OPEN_READ:
    this->vc = (CacheVC *)e;
    /* fall through */
  case CACHE_EVENT_OPEN_READ_FAILED:
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
CacheReadTest::start_test(int event, void *e)
{
  Debug("cache test", "start read test");
  HttpCacheKey key;
  key = generate_key(this->info);

  SET_HANDLER(&CacheReadTest::read_event);
  cacheProcessor.open_read(this, &key, (CacheHTTPHdr *)this->info.request_get(), &this->params);
  return 0;
}

constexpr size_t WRITE_LIMIT = 1024 * 3;
