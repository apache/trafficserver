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
#include "api/ts/ts.h"

CacheTestSM::CacheTestSM(RegressionTest *t) :
  RegressionSM(t),
  timeout(0),
  cache_action(0),
  start_time(0),
  cache_vc(0),
  cvio(0),
  buffer(0),
  buffer_reader(0),
  nbytes(-1),
  repeat_count(0),
  expect_event(EVENT_NONE),
  expect_initial_event(EVENT_NONE),
  initial_event(EVENT_NONE),
  content_salt(0)
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

int CacheTestSM::open_read_callout() {
  cvio = cache_vc->do_io_read(this, nbytes, buffer);
  return 1;
}

int CacheTestSM::open_write_callout() {
  cvio = cache_vc->do_io_write(this, nbytes, buffer_reader);
  return 1;
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
      if (open_read_callout() < 0)
        goto Lclose_error_next;
      else
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
      if (open_write_callout() < 0)
        goto Lclose_error_next;
      else
        return EVENT_DONE;

    case CACHE_EVENT_OPEN_WRITE_FAILED:
      goto Lcancel_next;

    case VC_EVENT_WRITE_READY:
      fill_buffer();
      cvio->reenable();
      return EVENT_CONT;

    case VC_EVENT_WRITE_COMPLETE:
      if (nbytes != cvio->ndone)
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
  int64_t avail = buffer->write_avail();
  CacheKey k = key;
  k.b[1] += content_salt;
  int64_t sk = (int64_t)sizeof(key);
  while (avail > 0) {
    int64_t l = avail;
    if (l > sk)
      l = sk;

    int64_t pos = cvio->ndone +  buffer_reader->read_avail();
    int64_t o = pos % sk;

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
  int64_t avail = buffer_reader->read_avail();
  CacheKey k = key;
  k.b[1] += content_salt;
  char b[sizeof(key)];
  int64_t sk = (int64_t)sizeof(key);
  int64_t pos = cvio->ndone -  buffer_reader->read_avail();
  while (avail > 0) {
    int64_t l = avail;
    if (l > sk)
      l = sk;
    int64_t o = pos % sk;
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
  int o = (int)(((char*)&start_memcpy_on_clone) - ((char*)this));
  int s = (int)(((char*)&end_memcpy_on_clone) - ((char*)&start_memcpy_on_clone));
  memcpy(((char*)this)+o, ((char*)&ao)+o, s);
  SET_HANDLER(&CacheTestSM::event_handler);
}

EXCLUSIVE_REGRESSION_TEST(cache)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus) {
  if (cacheProcessor.IsCacheEnabled() != CACHE_INITIALIZED) {
    rprintf(t, "cache not initialized");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  EThread *thread = this_ethread();

  CACHE_SM(t, write_test, { cacheProcessor.open_write(
        this, &key, false, CACHE_FRAG_TYPE_NONE, 100,
        CACHE_WRITE_OPT_SYNC); } );
  write_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  write_test.expect_event = VC_EVENT_WRITE_COMPLETE;
  write_test.nbytes = 100;
  rand_CacheKey(&write_test.key, thread->mutex);

  CACHE_SM(t, lookup_test, { cacheProcessor.lookup(this, &key, false); } );
  lookup_test.expect_event = CACHE_EVENT_LOOKUP;
  lookup_test.key = write_test.key;

  CACHE_SM(t, read_test, { cacheProcessor.open_read(this, &key, false); } );
  read_test.expect_initial_event = CACHE_EVENT_OPEN_READ;
  read_test.expect_event = VC_EVENT_READ_COMPLETE;
  read_test.nbytes = 100;
  read_test.key = write_test.key;

  CACHE_SM(t, remove_test, { cacheProcessor.remove(this, &key, false); } );
  remove_test.expect_event = CACHE_EVENT_REMOVE;
  remove_test.key = write_test.key;

  CACHE_SM(t, lookup_fail_test, { cacheProcessor.lookup(this, &key, false); } );
  lookup_fail_test.expect_event = CACHE_EVENT_LOOKUP_FAILED;
  lookup_fail_test.key = write_test.key;

  CACHE_SM(t, read_fail_test, { cacheProcessor.open_read(this, &key, false); } );
  read_fail_test.expect_event = CACHE_EVENT_OPEN_READ_FAILED;
  read_fail_test.key = write_test.key;

  CACHE_SM(t, remove_fail_test, { cacheProcessor.remove(this, &key, false); } );
  remove_fail_test.expect_event = CACHE_EVENT_REMOVE_FAILED;
  rand_CacheKey(&remove_fail_test.key, thread->mutex);

  CACHE_SM(t, replace_write_test, {
      cacheProcessor.open_write(this, &key, false, CACHE_FRAG_TYPE_NONE, 100,
                                CACHE_WRITE_OPT_SYNC);
    }
    int open_write_callout() {
      header.serial = 10;
      cache_vc->set_header(&header, sizeof(header));
      cvio = cache_vc->do_io_write(this, nbytes, buffer_reader);
      return 1;
    });
  replace_write_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  replace_write_test.expect_event = VC_EVENT_WRITE_COMPLETE;
  replace_write_test.nbytes = 100;
  rand_CacheKey(&replace_write_test.key, thread->mutex);

  CACHE_SM(t, replace_test, {
      cacheProcessor.open_write(this, &key, false, CACHE_FRAG_TYPE_NONE, 100,
                                CACHE_WRITE_OPT_OVERWRITE_SYNC);
    }
    int open_write_callout() {
      CacheTestHeader *h = 0;
      int hlen = 0;
      if (cache_vc->get_header((void**)&h, &hlen) < 0)
        return -1;
      if (h->serial != 10)
        return -1;
      header.serial = 11;
      cache_vc->set_header(&header, sizeof(header));
      cvio = cache_vc->do_io_write(this, nbytes, buffer_reader);
      return 1;
    });
  replace_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  replace_test.expect_event = VC_EVENT_WRITE_COMPLETE;
  replace_test.nbytes = 100;
  replace_test.key = replace_write_test.key;
  replace_test.content_salt = 1;

  CACHE_SM(t, replace_read_test, {
      cacheProcessor.open_read(this, &key, false);
    }
    int open_read_callout() {
      CacheTestHeader *h = 0;
      int hlen = 0;
      if (cache_vc->get_header((void**)&h, &hlen) < 0)
        return -1;
      if (h->serial != 11)
        return -1;
      cvio = cache_vc->do_io_read(this, nbytes, buffer);
      return 1;
    });
  replace_read_test.expect_initial_event = CACHE_EVENT_OPEN_READ;
  replace_read_test.expect_event = VC_EVENT_READ_COMPLETE;
  replace_read_test.nbytes = 100;
  replace_read_test.key = replace_test.key;
  replace_read_test.content_salt = 1;

  CACHE_SM(t, large_write_test, { cacheProcessor.open_write(
        this, &key, false, CACHE_FRAG_TYPE_NONE, 100,
        CACHE_WRITE_OPT_SYNC); } );
  large_write_test.expect_initial_event = CACHE_EVENT_OPEN_WRITE;
  large_write_test.expect_event = VC_EVENT_WRITE_COMPLETE;
  large_write_test.nbytes = 10000000;
  rand_CacheKey(&large_write_test.key, thread->mutex);

  CACHE_SM(t, pread_test, {
      cacheProcessor.open_read(this, &key, false);
    }
    int open_read_callout() {
      cvio = cache_vc->do_io_pread(this, nbytes, buffer, 7000000);
      return 1;
    });
  pread_test.expect_initial_event = CACHE_EVENT_OPEN_READ;
  pread_test.expect_event = VC_EVENT_READ_COMPLETE;
  pread_test.nbytes = 100;
  pread_test.key = large_write_test.key;

  r_sequential(
    t,
    write_test.clone(),
    lookup_test.clone(),
    r_sequential(t, 10, read_test.clone()),
    remove_test.clone(),
    lookup_fail_test.clone(),
    read_fail_test.clone(),
    remove_fail_test.clone(),
    replace_write_test.clone(),
    replace_test.clone(),
    replace_read_test.clone(),
    large_write_test.clone(),
    pread_test.clone(),
    NULL_PTR
    )->run(pstatus);
  return;
}

void force_link_CacheTest() {
}

// run -R 3 -r cache_disk_replacement_stability

REGRESSION_TEST(cache_disk_replacement_stability)(RegressionTest *t, int level, int *pstatus) {
  static int const MAX_VOLS = 26; // maximum values used in any test.
  static uint64_t DEFAULT_SKIP = 8192;
  static uint64_t DEFAULT_STRIPE_SIZE = 1024ULL * 1024 * 1024 * 911; // 911G
  CacheDisk disk; // Only need one because it's just checked for failure.
  CacheHostRecord hr1, hr2;
  Vol* sample;
  static int const sample_idx = 16;
  Vol vols[MAX_VOLS];
  Vol* vol_ptrs[MAX_VOLS]; // array of pointers.
  char buff[2048];

  // Only run at the highest levels.
  if (REGRESSION_TEST_EXTENDED > level) {
    *pstatus = REGRESSION_TEST_PASSED;
    return;
  }

  *pstatus = REGRESSION_TEST_INPROGRESS;

  disk.num_errors = 0;

  for ( int i = 0 ; i < MAX_VOLS ; ++i ) {
    vol_ptrs[i] = vols + i;
    vols[i].disk = &disk;
    vols[i].len = DEFAULT_STRIPE_SIZE;
    snprintf(buff, sizeof(buff), "/dev/sd%c %" PRIu64 ":%" PRIu64,
             'a' + i, DEFAULT_SKIP, vols[i].len);
    MD5Context().hash_immediate(vols[i].hash_id, buff, strlen(buff));
  }

  hr1.vol_hash_table = 0;
  hr1.vols = vol_ptrs;
  hr1.num_vols = MAX_VOLS;
  build_vol_hash_table(&hr1);

  hr2.vol_hash_table = 0;
  hr2.vols = vol_ptrs;
  hr2.num_vols = MAX_VOLS;

  sample = vols + sample_idx;
  sample->len = 1024ULL * 1024 * 1024 * (1024+128); // 1.1 TB
  snprintf(buff, sizeof(buff), "/dev/sd%c %" PRIu64 ":%" PRIu64,
           'a' + sample_idx, DEFAULT_SKIP, sample->len);
  MD5Context().hash_immediate(sample->hash_id, buff, strlen(buff));
  build_vol_hash_table(&hr2);

  // See what the difference is
  int to = 0, from = 0;
  int then = 0, now = 0;
  for ( int i = 0 ; i < VOL_HASH_TABLE_SIZE ; ++i ) {
    if (hr1.vol_hash_table[i] == sample_idx) ++then;
    if (hr2.vol_hash_table[i] == sample_idx) ++now;
    if (hr1.vol_hash_table[i] != hr2.vol_hash_table[i]) {
      if (hr1.vol_hash_table[i] == sample_idx)
        ++from;
      else
        ++to;
    }
  }
  rprintf(t, "Cache stability difference - "
          "delta = %d of %d : %d to, %d from, originally %d slots, now %d slots (net gain = %d/%d)\n"
          , to+from, VOL_HASH_TABLE_SIZE, to, from, then, now, now-then, to-from
    );
  *pstatus = REGRESSION_TEST_PASSED;

  hr1.vols = 0;
  hr2.vols = 0;
}
