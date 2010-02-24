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
#include "api/include/ts.h"


void verify_cache_api() {
  ink_assert((int)INK_EVENT_CACHE_OPEN_READ == (int)CACHE_EVENT_OPEN_READ);
  ink_assert((int)INK_EVENT_CACHE_OPEN_READ_FAILED == (int)CACHE_EVENT_OPEN_READ_FAILED);
  ink_assert((int)INK_EVENT_CACHE_OPEN_WRITE == (int)CACHE_EVENT_OPEN_WRITE);
  ink_assert((int)INK_EVENT_CACHE_OPEN_WRITE_FAILED == (int)CACHE_EVENT_OPEN_WRITE_FAILED);
  ink_assert((int)INK_EVENT_CACHE_REMOVE == (int)CACHE_EVENT_REMOVE);
  ink_assert((int)INK_EVENT_CACHE_REMOVE_FAILED == (int)CACHE_EVENT_REMOVE_FAILED);
  ink_assert((int)INK_EVENT_CACHE_SCAN == (int)CACHE_EVENT_SCAN);
  ink_assert((int)INK_EVENT_CACHE_SCAN_FAILED == (int)CACHE_EVENT_SCAN_FAILED);
  ink_assert((int)INK_EVENT_CACHE_SCAN_OBJECT == (int)CACHE_EVENT_SCAN_OBJECT);
  ink_assert((int)INK_EVENT_CACHE_SCAN_OPERATION_BLOCKED == (int)CACHE_EVENT_SCAN_OPERATION_BLOCKED);
  ink_assert((int)INK_EVENT_CACHE_SCAN_OPERATION_FAILED == (int)CACHE_EVENT_SCAN_OPERATION_FAILED);
  ink_assert((int)INK_EVENT_CACHE_SCAN_DONE == (int)CACHE_EVENT_SCAN_DONE);
}

CacheTestSM::CacheTestSM(RegressionTest *t) :
  RegressionSM(t),
  timeout(0),
  cache_action(0),
  start_time(0),
  cache_vc(0),
  cvio(0),
  buffer(0),
  buffer_reader(0),
  total_size(-1),
  repeat_count(0),
  expect_event(EVENT_NONE),
  expect_initial_event(EVENT_NONE),
  initial_event(EVENT_NONE),
  flags(0)
{
  SET_HANDLER(&CacheTestSM::event_handler);
}

CacheTestSM::~CacheTestSM() {
  ink_assert(!cache_action);
  ink_assert(!cache_vc);
  if (buffer_reader)
    buffer->dealloc_reader(buffer_reader);
  if (buffer)
    free_MIOBuffer(buffer);
}

int CacheTestSM::event_handler(int event, void *data) {

  switch (event) {

    case EVENT_INTERVAL:
    case EVENT_IMMEDIATE:
      cancel_timeout();
      if (cache_action) {
        cache_action->cancel();
        cache_action = 0;
      }
      if (cache_vc) {
        cache_vc->do_io_close();
        cache_vc = 0;
      }
      cvio = 0;
      make_request();
      return EVENT_DONE;

    case CACHE_EVENT_LOOKUP_FAILED:
    case CACHE_EVENT_LOOKUP:
      goto Lcancel_next;

    case CACHE_EVENT_OPEN_READ:
      initial_event = event;
      cancel_timeout();
      cache_action = 0;
      cache_vc = (CacheVConnection*)data;
      buffer = new_empty_MIOBuffer();
      buffer_reader = buffer->alloc_reader();
      cvio = cache_vc->do_io_read(this, total_size, buffer);
      return EVENT_DONE;
   
    case CACHE_EVENT_OPEN_READ_FAILED:
      goto Lcancel_next;

    case VC_EVENT_READ_READY:
      if (!check_buffer())
        goto Lclose_error_next;
      buffer_reader->consume(buffer_reader->read_avail());
      ((VIO*)data)->reenable();
      return EVENT_CONT;

    case VC_EVENT_READ_COMPLETE:
      if (!check_buffer())
        goto Lclose_error_next;
      goto Lclose_next;

    case VC_EVENT_ERROR:
    case VC_EVENT_EOS:
      goto Lclose_error_next;

    case CACHE_EVENT_OPEN_WRITE:
      initial_event = event;
      cancel_timeout();
      cache_action = 0;
      cache_vc = (CacheVConnection*)data;
      buffer = new_empty_MIOBuffer();
      buffer_reader = buffer->alloc_reader();
      cvio = cache_vc->do_io_write(this, total_size, buffer_reader);
      return EVENT_DONE;
      
    case CACHE_EVENT_OPEN_WRITE_FAILED:
      goto Lcancel_next;

    case VC_EVENT_WRITE_READY:
      fill_buffer();
      cvio->reenable();
      return EVENT_CONT;

    case VC_EVENT_WRITE_COMPLETE:
      if (total_size != cvio->ndone)
        goto Lclose_error_next;
      goto Lclose_next;

    case CACHE_EVENT_REMOVE:
    case CACHE_EVENT_REMOVE_FAILED:
      goto Lcancel_next;

    case CACHE_EVENT_SCAN:
      initial_event = event;
      cache_vc = (CacheVConnection*)data;
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
  cache_action = 0;
  goto Lnext;
Lclose_error_next:
  cache_vc->do_io_close(1);
  goto Lclose_next_internal;
Lclose_next:
  cache_vc->do_io_close();
Lclose_next_internal:
  cache_vc = 0;
  if (buffer_reader) {
    buffer->dealloc_reader(buffer_reader);
    buffer_reader = 0;
  }
  if (buffer) {
    free_MIOBuffer(buffer);
    buffer = 0;
  }
Lnext:
  if (check_result(event) && repeat_count) {
    repeat_count--;
    timeout = eventProcessor.schedule_imm(this);
    return EVENT_DONE;
  } else
    return complete(event);
}

void CacheTestSM::fill_buffer() {
  ink64 avail = buffer->write_avail();
  CacheKey k = key;
  ink64 sk = (ink64)sizeof(key);
  while (avail > 0) {
    ink64 l = avail;
    if (l > sk)
      l = sk;
    ink64 pos = cvio->ndone +  buffer_reader->read_avail();
    int o = pos % sk;
    if (l > sk - o)
      l = sk - o;
    k.b[0] = pos / sk;
    char *x = ((char*)&k) + o;
    buffer->write(x, l);
    buffer->fill(l);
    avail -= l;
  }
}

int CacheTestSM::check_buffer() { 
  ink64 avail = buffer_reader->read_avail();
  CacheKey k = key;
  char b[sizeof(key)];
  ink64 sk = (ink64)sizeof(key);
  ink64 pos = cvio->ndone -  buffer_reader->read_avail();
  while (avail > 0) {
    ink64 l = avail;
    if (l > sk)
      l = sk;
    int o = pos % sk;
    if (l > sk - o)
      l = sk - o;
    k.b[0] = pos / sk;
    char *x = ((char*)&k) + o;
    buffer_reader->read(&b[0], l);
    if (::memcmp(b, x, l))
      return 0;
    buffer_reader->consume(l);
    pos += l;
    avail -= l;
  }
  return 1; 
}

int CacheTestSM::check_result(int event) {
  return 
    initial_event == expect_initial_event &&
    event == expect_event;
}

int CacheTestSM::complete(int event) {
  if (!check_result(event))
    done(REGRESSION_TEST_FAILED);
  else
    done(REGRESSION_TEST_PASSED);
  delete this;
  return EVENT_DONE;
}

CacheTestSM::CacheTestSM(const CacheTestSM &ao) : RegressionSM(ao) {
  timeout = ao.timeout;
  cache_action = ao.cache_action; 
  start_time = ao.start_time; 
  cache_vc = ao.cache_vc;
  cvio = ao.cvio;
  buffer = ao.buffer;
  buffer_reader = ao.buffer_reader;
#ifdef HTTP_CACHE
  params = ao.params;
  info = ao.info;
#endif
  total_size = ao.total_size;
  memcpy(urlstr, ao.urlstr, 1024);
  key = ao.key;
  repeat_count = ao.repeat_count;
  expect_event = ao.expect_event;
  expect_initial_event = ao.expect_initial_event;
  initial_event = ao.initial_event;
  flags = ao.flags;
  SET_HANDLER(&CacheTestSM::event_handler);
}

EXCLUSIVE_REGRESSION_TEST(cache)(RegressionTest *t, int atype, int *pstatus) {
  if (cacheProcessor.IsCacheEnabled() != CACHE_INITIALIZED) {
    rprintf(t, "cache not initialized");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  EThread *thread = this_ethread();

  CACHE_SM(t, write_test, { cacheProcessor.open_write(this, &key); } );
  write_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  write_test.expect_event = VC_EVENT_WRITE_COMPLETE;
  write_test.total_size = 100;
  rand_CacheKey(&write_test.key, thread->mutex);

  CACHE_SM(t, lookup_test, { cacheProcessor.lookup(this, &key); } );
  lookup_test.expect_event = CACHE_EVENT_LOOKUP;
  lookup_test.key = write_test.key;
  
  CACHE_SM(t, read_test, { cacheProcessor.open_read(this, &key); } );
  read_test.expect_initial_event = CACHE_EVENT_OPEN_READ;
  read_test.expect_event = VC_EVENT_READ_COMPLETE;
  read_test.total_size = 100;
  read_test.key = write_test.key;

  CACHE_SM(t, remove_test, { cacheProcessor.remove(this, &key); } );
  remove_test.expect_event = CACHE_EVENT_REMOVE;
  remove_test.key = write_test.key;

  CACHE_SM(t, lookup_fail_test, { cacheProcessor.lookup(this, &key); } );
  lookup_fail_test.expect_event = CACHE_EVENT_LOOKUP_FAILED;
  lookup_fail_test.key = write_test.key;

  CACHE_SM(t, read_fail_test, { cacheProcessor.open_read(this, &key); } );
  read_fail_test.expect_event = CACHE_EVENT_OPEN_READ_FAILED;
  read_fail_test.key = write_test.key;
  
  CACHE_SM(t, remove_fail_test, { cacheProcessor.remove(this, &key); } );
  remove_fail_test.expect_event = CACHE_EVENT_REMOVE_FAILED;
  rand_CacheKey(&remove_fail_test.key, thread->mutex);

  r_sequential(
    t,
    write_test.clone(),
    lookup_test.clone(),
    r_sequential(t, 10, read_test.clone()),
    remove_test.clone(),
    lookup_fail_test.clone(),
    read_fail_test.clone(),
    remove_fail_test.clone(),
    NULL
    )->run(pstatus);
  return;
}
