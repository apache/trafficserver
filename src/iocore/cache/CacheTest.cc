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

#include "P_Cache.h"
#include "P_CacheTest.h"
#include "tscore/Random.h"
#include <vector>
#include <cmath>
#include <cstdlib>

CacheTestSM::CacheTestSM(RegressionTest *t, const char *name) : RegressionSM(t), cache_test_name(name)
{
  SET_HANDLER(&CacheTestSM::event_handler);
}

CacheTestSM::CacheTestSM(const CacheTestSM &ao) : RegressionSM(ao)
{
  int o = static_cast<int>((reinterpret_cast<char *>(&start_memcpy_on_clone)) - (reinterpret_cast<char *>(this)));
  int s = static_cast<int>((reinterpret_cast<char *>(&end_memcpy_on_clone)) - (reinterpret_cast<char *>(&start_memcpy_on_clone)));
  memcpy((reinterpret_cast<char *>(this)) + o, ((char *)&ao) + o, s);
  SET_HANDLER(&CacheTestSM::event_handler);
}

CacheTestSM::~CacheTestSM()
{
  ink_assert(!cache_action);
  ink_assert(!cache_vc);
  if (buffer_reader) {
    buffer->dealloc_reader(buffer_reader);
  }
  if (buffer) {
    free_MIOBuffer(buffer);
  }
}

int
CacheTestSM::open_read_callout()
{
  cvio = cache_vc->do_io_read(this, nbytes, buffer);
  return 1;
}

int
CacheTestSM::open_write_callout()
{
  cvio = cache_vc->do_io_write(this, nbytes, buffer_reader);
  return 1;
}

int
CacheTestSM::event_handler(int event, void *data)
{
  switch (event) {
  case EVENT_INTERVAL:
  case EVENT_IMMEDIATE:
    cancel_timeout();
    if (cache_action) {
      cache_action->cancel();
      cache_action = nullptr;
    }
    if (cache_vc) {
      cache_vc->do_io_close();
      cache_vc = nullptr;
    }
    cvio = nullptr;
    make_request();
    return EVENT_DONE;

  case CACHE_EVENT_LOOKUP_FAILED:
  case CACHE_EVENT_LOOKUP:
    goto Lcancel_next;

  case CACHE_EVENT_OPEN_READ:
    initial_event = event;
    cancel_timeout();
    cache_action  = nullptr;
    cache_vc      = static_cast<CacheVConnection *>(data);
    buffer        = new_empty_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    buffer_reader = buffer->alloc_reader();
    if (open_read_callout() < 0) {
      goto Lclose_error_next;
    } else {
      return EVENT_DONE;
    }

  case CACHE_EVENT_OPEN_READ_FAILED:
    goto Lcancel_next;

  case VC_EVENT_READ_READY:
    if (!check_buffer()) {
      goto Lclose_error_next;
    }
    buffer_reader->consume(buffer_reader->read_avail());
    (static_cast<VIO *>(data))->reenable();
    return EVENT_CONT;

  case VC_EVENT_READ_COMPLETE:
    if (!check_buffer()) {
      goto Lclose_error_next;
    }
    goto Lclose_next;

  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    goto Lclose_error_next;

  case CACHE_EVENT_OPEN_WRITE:
    initial_event = event;
    cancel_timeout();
    cache_action  = nullptr;
    cache_vc      = static_cast<CacheVConnection *>(data);
    buffer        = new_empty_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    buffer_reader = buffer->alloc_reader();
    if (open_write_callout() < 0) {
      goto Lclose_error_next;
    } else {
      return EVENT_DONE;
    }

  case CACHE_EVENT_OPEN_WRITE_FAILED:
    goto Lcancel_next;

  case VC_EVENT_WRITE_READY:
    fill_buffer();
    cvio->reenable();
    return EVENT_CONT;

  case VC_EVENT_WRITE_COMPLETE:
    if (nbytes != cvio->ndone) {
      goto Lclose_error_next;
    }
    goto Lclose_next;

  case CACHE_EVENT_REMOVE:
  case CACHE_EVENT_REMOVE_FAILED:
    goto Lcancel_next;

  case CACHE_EVENT_SCAN:
    initial_event = event;
    cache_vc      = static_cast<CacheVConnection *>(data);
    return EVENT_CONT;

  case CACHE_EVENT_SCAN_OBJECT:
    return CACHE_SCAN_RESULT_CONTINUE;

  case CACHE_EVENT_SCAN_OPERATION_FAILED:
    return CACHE_SCAN_RESULT_CONTINUE;

  case CACHE_EVENT_SCAN_OPERATION_BLOCKED:
    return CACHE_SCAN_RESULT_CONTINUE;

  case CACHE_EVENT_SCAN_DONE:
    return EVENT_CONT;

  case CACHE_EVENT_SCAN_FAILED:
    return EVENT_CONT;

  case AIO_EVENT_DONE:
    goto Lnext;

  default:
    ink_assert(!"case");
    break;
  }
  return EVENT_DONE;

Lcancel_next:
  cancel_timeout();
  cache_action = nullptr;
  goto Lnext;
Lclose_error_next:
  cache_vc->do_io_close(1);
  goto Lclose_next_internal;
Lclose_next:
  cache_vc->do_io_close();
Lclose_next_internal:
  cache_vc = nullptr;
  if (buffer_reader) {
    buffer->dealloc_reader(buffer_reader);
    buffer_reader = nullptr;
  }
  if (buffer) {
    free_MIOBuffer(buffer);
    buffer = nullptr;
  }
Lnext:
  if (check_result(event) && repeat_count) {
    repeat_count--;
    timeout = eventProcessor.schedule_imm(this);
    return EVENT_DONE;
  } else {
    return complete(event);
  }
}

void
CacheTestSM::fill_buffer()
{
  int64_t avail  = buffer->write_avail();
  CacheKey k     = key;
  k.b[1]        += content_salt;
  int64_t sk     = static_cast<int64_t>(sizeof(key));
  while (avail > 0) {
    int64_t l = avail;
    if (l > sk) {
      l = sk;
    }

    int64_t pos = cvio->ndone + buffer_reader->read_avail();
    int64_t o   = pos % sk;

    if (l > sk - o) {
      l = sk - o;
    }
    k.b[0]  = pos / sk;
    char *x = (reinterpret_cast<char *>(&k)) + o;
    buffer->write(x, l);
    buffer->fill(l);
    avail -= l;
  }
}

int
CacheTestSM::check_buffer()
{
  int64_t avail  = buffer_reader->read_avail();
  CacheKey k     = key;
  k.b[1]        += content_salt;
  char b[sizeof(key)];
  int64_t sk  = static_cast<int64_t>(sizeof(key));
  int64_t pos = cvio->ndone - buffer_reader->read_avail();
  while (avail > 0) {
    int64_t l = avail;
    if (l > sk) {
      l = sk;
    }
    int64_t o = pos % sk;
    if (l > sk - o) {
      l = sk - o;
    }
    k.b[0]  = pos / sk;
    char *x = (reinterpret_cast<char *>(&k)) + o;
    buffer_reader->read(&b[0], l);
    if (::memcmp(b, x, l)) {
      return 0;
    }
    buffer_reader->consume(l);
    pos   += l;
    avail -= l;
  }
  return 1;
}

int
CacheTestSM::check_result(int event)
{
  return initial_event == expect_initial_event && event == expect_event;
}

int
CacheTestSM::complete(int event)
{
  if (!check_result(event)) {
    done(REGRESSION_TEST_FAILED);
  } else {
    done(REGRESSION_TEST_PASSED);
  }
  delete this;
  return EVENT_DONE;
}

EXCLUSIVE_REGRESSION_TEST(cache)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  if (cacheProcessor.IsCacheEnabled() != CACHE_INITIALIZED) {
    rprintf(t, "cache not initialized");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  EThread *thread = this_ethread();

  CACHE_SM(t, write_test, { cacheProcessor.open_write(this, &key, CACHE_FRAG_TYPE_NONE, 100, CACHE_WRITE_OPT_SYNC); });
  write_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  write_test.expect_event         = VC_EVENT_WRITE_COMPLETE;
  write_test.nbytes               = 100;
  rand_CacheKey(&write_test.key, thread->mutex);

  CACHE_SM(t, lookup_test, { cacheProcessor.lookup(this, &key); });
  lookup_test.expect_event = CACHE_EVENT_LOOKUP;
  lookup_test.key          = write_test.key;

  CACHE_SM(t, read_test, { cacheProcessor.open_read(this, &key); });
  read_test.expect_initial_event = CACHE_EVENT_OPEN_READ;
  read_test.expect_event         = VC_EVENT_READ_COMPLETE;
  read_test.nbytes               = 100;
  read_test.key                  = write_test.key;

  CACHE_SM(t, remove_test, { cacheProcessor.remove(this, &key); });
  remove_test.expect_event = CACHE_EVENT_REMOVE;
  remove_test.key          = write_test.key;

  CACHE_SM(t, lookup_fail_test, { cacheProcessor.lookup(this, &key); });
  lookup_fail_test.expect_event = CACHE_EVENT_LOOKUP_FAILED;
  lookup_fail_test.key          = write_test.key;

  CACHE_SM(t, read_fail_test, { cacheProcessor.open_read(this, &key); });
  read_fail_test.expect_event = CACHE_EVENT_OPEN_READ_FAILED;
  read_fail_test.key          = write_test.key;

  CACHE_SM(t, remove_fail_test, { cacheProcessor.remove(this, &key); });
  remove_fail_test.expect_event = CACHE_EVENT_REMOVE_FAILED;
  rand_CacheKey(&remove_fail_test.key, thread->mutex);

  CACHE_SM(
    t, replace_write_test,
    { cacheProcessor.open_write(this, &key, CACHE_FRAG_TYPE_NONE, 100, CACHE_WRITE_OPT_SYNC); } int open_write_callout() override {
      header.serial = 10;
      cache_vc->set_header(&header, sizeof(header));
      cvio = cache_vc->do_io_write(this, nbytes, buffer_reader);
      return 1;
    });
  replace_write_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  replace_write_test.expect_event         = VC_EVENT_WRITE_COMPLETE;
  replace_write_test.nbytes               = 100;
  rand_CacheKey(&replace_write_test.key, thread->mutex);

  CACHE_SM(
    t, replace_test,
    { cacheProcessor.open_write(this, &key, CACHE_FRAG_TYPE_NONE, 100, CACHE_WRITE_OPT_OVERWRITE_SYNC); } int open_write_callout()
      override {
        CacheTestHeader *h = nullptr;
        int hlen           = 0;
        if (cache_vc->get_header((void **)&h, &hlen) < 0)
          return -1;
        if (h->serial != 10)
          return -1;
        header.serial = 11;
        cache_vc->set_header(&header, sizeof(header));
        cvio = cache_vc->do_io_write(this, nbytes, buffer_reader);
        return 1;
      });
  replace_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  replace_test.expect_event         = VC_EVENT_WRITE_COMPLETE;
  replace_test.nbytes               = 100;
  replace_test.key                  = replace_write_test.key;
  replace_test.content_salt         = 1;

  CACHE_SM(
    t, replace_read_test, { cacheProcessor.open_read(this, &key); } int open_read_callout() override {
      CacheTestHeader *h = nullptr;
      int hlen           = 0;
      if (cache_vc->get_header((void **)&h, &hlen) < 0)
        return -1;
      if (h->serial != 11)
        return -1;
      cvio = cache_vc->do_io_read(this, nbytes, buffer);
      return 1;
    });
  replace_read_test.expect_initial_event = CACHE_EVENT_OPEN_READ;
  replace_read_test.expect_event         = VC_EVENT_READ_COMPLETE;
  replace_read_test.nbytes               = 100;
  replace_read_test.key                  = replace_test.key;
  replace_read_test.content_salt         = 1;

  CACHE_SM(t, large_write_test, { cacheProcessor.open_write(this, &key, CACHE_FRAG_TYPE_NONE, 100, CACHE_WRITE_OPT_SYNC); });
  large_write_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  large_write_test.expect_event         = VC_EVENT_WRITE_COMPLETE;
  large_write_test.nbytes               = 10000000;
  rand_CacheKey(&large_write_test.key, thread->mutex);

  CACHE_SM(
    t, pread_test, { cacheProcessor.open_read(this, &key); } int open_read_callout() override {
      cvio = cache_vc->do_io_pread(this, nbytes, buffer, 7000000);
      return 1;
    });
  pread_test.expect_initial_event = CACHE_EVENT_OPEN_READ;
  pread_test.expect_event         = VC_EVENT_READ_COMPLETE;
  pread_test.nbytes               = 100;
  pread_test.key                  = large_write_test.key;

  // clang-format off
  r_sequential(t,
      write_test.clone(),
      lookup_test.clone(),
      r_sequential(t, 10, read_test.clone()) /* run read_test 10 times */,
      remove_test.clone(),
      lookup_fail_test.clone(),
      read_fail_test.clone(),
      remove_fail_test.clone(),
      replace_write_test.clone(),
      replace_test.clone(),
      replace_read_test.clone(),
      large_write_test.clone(),
      pread_test.clone(),
      nullptr)
  ->run(pstatus);
  // clang-format on

  return;
}

void
force_link_CacheTest()
{
}
