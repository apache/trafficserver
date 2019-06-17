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
#include <vector>
#include <cmath>
#include <cstdlib>

using namespace std;

CacheTestSM::CacheTestSM(RegressionTest *t, const char *name) : RegressionSM(t), cache_test_name(name)
{
  SET_HANDLER(&CacheTestSM::event_handler);
}

CacheTestSM::CacheTestSM(const CacheTestSM &ao) : RegressionSM(ao)
{
  int o = (int)(((char *)&start_memcpy_on_clone) - ((char *)this));
  int s = (int)(((char *)&end_memcpy_on_clone) - ((char *)&start_memcpy_on_clone));
  memcpy(((char *)this) + o, ((char *)&ao) + o, s);
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
    cache_vc      = (CacheVConnection *)data;
    buffer        = new_empty_MIOBuffer();
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
    ((VIO *)data)->reenable();
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
    cache_vc      = (CacheVConnection *)data;
    buffer        = new_empty_MIOBuffer();
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
    cache_vc      = (CacheVConnection *)data;
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
  int64_t avail = buffer->write_avail();
  CacheKey k    = key;
  k.b[1] += content_salt;
  int64_t sk = (int64_t)sizeof(key);
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
    char *x = ((char *)&k) + o;
    buffer->write(x, l);
    buffer->fill(l);
    avail -= l;
  }
}

int
CacheTestSM::check_buffer()
{
  int64_t avail = buffer_reader->read_avail();
  CacheKey k    = key;
  k.b[1] += content_salt;
  char b[sizeof(key)];
  int64_t sk  = (int64_t)sizeof(key);
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
    char *x = ((char *)&k) + o;
    buffer_reader->read(&b[0], l);
    if (::memcmp(b, x, l)) {
      return 0;
    }
    buffer_reader->consume(l);
    pos += l;
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

  CACHE_SM(t, replace_write_test,
           { cacheProcessor.open_write(this, &key, CACHE_FRAG_TYPE_NONE, 100, CACHE_WRITE_OPT_SYNC); } int open_write_callout() {
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
    { cacheProcessor.open_write(this, &key, CACHE_FRAG_TYPE_NONE, 100, CACHE_WRITE_OPT_OVERWRITE_SYNC); } int open_write_callout() {
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

  CACHE_SM(t, replace_read_test, { cacheProcessor.open_read(this, &key); } int open_read_callout() {
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

  CACHE_SM(t, pread_test, { cacheProcessor.open_read(this, &key); } int open_read_callout() {
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

// run -R 3 -r cache_disk_replacement_stability

REGRESSION_TEST(cache_disk_replacement_stability)(RegressionTest *t, int level, int *pstatus)
{
  static int const MAX_VOLS           = 26; // maximum values used in any test.
  static uint64_t DEFAULT_SKIP        = 8192;
  static uint64_t DEFAULT_STRIPE_SIZE = 1024ULL * 1024 * 1024 * 911; // 911G
  CacheDisk disk;                                                    // Only need one because it's just checked for failure.
  CacheHostRecord hr1, hr2;
  Vol *sample;
  static int const sample_idx = 16;
  Vol vols[MAX_VOLS];
  Vol *vol_ptrs[MAX_VOLS]; // array of pointers.
  char buff[2048];

  // Only run at the highest levels.
  if (REGRESSION_TEST_EXTENDED > level) {
    *pstatus = REGRESSION_TEST_PASSED;
    return;
  }

  *pstatus = REGRESSION_TEST_INPROGRESS;

  disk.num_errors = 0;

  for (int i = 0; i < MAX_VOLS; ++i) {
    vol_ptrs[i]  = vols + i;
    vols[i].disk = &disk;
    vols[i].len  = DEFAULT_STRIPE_SIZE;
    snprintf(buff, sizeof(buff), "/dev/sd%c %" PRIu64 ":%" PRIu64, 'a' + i, DEFAULT_SKIP, vols[i].len);
    CryptoContext().hash_immediate(vols[i].hash_id, buff, strlen(buff));
  }

  hr1.vol_hash_table = nullptr;
  hr1.vols           = vol_ptrs;
  hr1.num_vols       = MAX_VOLS;
  build_vol_hash_table(&hr1);

  hr2.vol_hash_table = nullptr;
  hr2.vols           = vol_ptrs;
  hr2.num_vols       = MAX_VOLS;

  sample      = vols + sample_idx;
  sample->len = 1024ULL * 1024 * 1024 * (1024 + 128); // 1.1 TB
  snprintf(buff, sizeof(buff), "/dev/sd%c %" PRIu64 ":%" PRIu64, 'a' + sample_idx, DEFAULT_SKIP, sample->len);
  CryptoContext().hash_immediate(sample->hash_id, buff, strlen(buff));
  build_vol_hash_table(&hr2);

  // See what the difference is
  int to = 0, from = 0;
  int then = 0, now = 0;
  for (int i = 0; i < VOL_HASH_TABLE_SIZE; ++i) {
    if (hr1.vol_hash_table[i] == sample_idx) {
      ++then;
    }
    if (hr2.vol_hash_table[i] == sample_idx) {
      ++now;
    }
    if (hr1.vol_hash_table[i] != hr2.vol_hash_table[i]) {
      if (hr1.vol_hash_table[i] == sample_idx) {
        ++from;
      } else {
        ++to;
      }
    }
  }
  rprintf(t,
          "Cache stability difference - "
          "delta = %d of %d : %d to, %d from, originally %d slots, now %d slots (net gain = %d/%d)\n",
          to + from, VOL_HASH_TABLE_SIZE, to, from, then, now, now - then, to - from);
  *pstatus = REGRESSION_TEST_PASSED;

  hr1.vols = nullptr;
  hr2.vols = nullptr;
}

static double zipf_alpha        = 1.2;
static int64_t zipf_bucket_size = 1;

#define ZIPF_SIZE (1 << 20)

static double *zipf_table = nullptr;

static void
build_zipf()
{
  if (zipf_table) {
    return;
  }
  zipf_table = (double *)ats_malloc(ZIPF_SIZE * sizeof(double));
  for (int i = 0; i < ZIPF_SIZE; i++) {
    zipf_table[i] = 1.0 / pow(i + 2, zipf_alpha);
  }
  for (int i = 1; i < ZIPF_SIZE; i++) {
    zipf_table[i] = zipf_table[i - 1] + zipf_table[i];
  }
  double x = zipf_table[ZIPF_SIZE - 1];
  for (int i = 0; i < ZIPF_SIZE; i++) {
    zipf_table[i] = zipf_table[i] / x;
  }
}

static int
get_zipf(double v)
{
  int l = 0, r = ZIPF_SIZE - 1, m;
  do {
    m = (r + l) / 2;
    if (v < zipf_table[m]) {
      r = m - 1;
    } else {
      l = m + 1;
    }
  } while (l < r);
  if (zipf_bucket_size == 1) {
    return m;
  }
  double x = zipf_table[m], y = zipf_table[m + 1];
  m += static_cast<int>((v - x) / (y - x));
  return m;
}

static bool
test_RamCache(RegressionTest *t, RamCache *cache, const char *name, int64_t cache_size)
{
  bool pass = true;
  CacheKey key;
  Vol *vol = theCache->key_to_vol(&key, "example.com", sizeof("example.com") - 1);
  vector<Ptr<IOBufferData>> data;

  cache->init(cache_size, vol);

  for (int l = 0; l < 10; l++) {
    for (int i = 0; i < 200; i++) {
      IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
      CryptoHash hash;

      d->alloc(BUFFER_SIZE_INDEX_16K);
      data.push_back(make_ptr(d));
      hash.u64[0] = ((uint64_t)i << 32) + i;
      hash.u64[1] = ((uint64_t)i << 32) + i;
      cache->put(&hash, data[i].get(), 1 << 15);
      // More hits for the first 10.
      for (int j = 0; j <= i && j < 10; j++) {
        Ptr<IOBufferData> data;
        CryptoHash hash;

        hash.u64[0] = ((uint64_t)j << 32) + j;
        hash.u64[1] = ((uint64_t)j << 32) + j;
        cache->get(&hash, &data);
      }
    }
  }

  for (int i = 0; i < 10; i++) {
    CryptoHash hash;
    Ptr<IOBufferData> data;

    hash.u64[0] = ((uint64_t)i << 32) + i;
    hash.u64[1] = ((uint64_t)i << 32) + i;
    if (!cache->get(&hash, &data)) {
      pass = false;
    }
  }

  int sample_size = cache_size >> 6;
  build_zipf();
  srand48(13);
  int *r = (int *)ats_malloc(sample_size * sizeof(int));
  for (int i = 0; i < sample_size; i++) {
    // coverity[dont_call]
    r[i] = get_zipf(drand48());
  }
  data.clear();
  int misses = 0;
  for (int i = 0; i < sample_size; i++) {
    CryptoHash hash;
    hash.u64[0] = ((uint64_t)r[i] << 32) + r[i];
    hash.u64[1] = ((uint64_t)r[i] << 32) + r[i];
    Ptr<IOBufferData> get_data;
    if (!cache->get(&hash, &get_data)) {
      IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
      d->alloc(BUFFER_SIZE_INDEX_16K);
      data.push_back(make_ptr(d));
      cache->put(&hash, data.back().get(), 1 << 15);
      if (i >= sample_size / 2) {
        misses++; // Sample last half of the gets.
      }
    }
  }
  double fixed_hit_rate = 1.0 - (((double)(misses)) / (sample_size / 2));
  rprintf(t, "RamCache %s Fixed Size Hit Rate %f\n", name, fixed_hit_rate);

  data.clear();
  misses = 0;
  for (int i = 0; i < sample_size; i++) {
    CryptoHash hash;
    hash.u64[0] = ((uint64_t)r[i] << 32) + r[i];
    hash.u64[1] = ((uint64_t)r[i] << 32) + r[i];
    Ptr<IOBufferData> get_data;
    if (!cache->get(&hash, &get_data)) {
      IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
      d->alloc(BUFFER_SIZE_INDEX_8K + (r[i] % 3));
      data.push_back(make_ptr(d));
      cache->put(&hash, data.back().get(), d->block_size());
      if (i >= sample_size / 2) {
        misses++; // Sample last half of the gets.
      }
    }
  }
  double variable_hit_rate = 1.0 - (((double)(misses)) / (sample_size / 2));
  rprintf(t, "RamCache %s Variable Size Hit Rate %f\n", name, variable_hit_rate);

  rprintf(t, "RamCache %s Nominal Size %lld Size %lld\n", name, cache_size, cache->size());

  if (fixed_hit_rate < 0.55 || variable_hit_rate < 0.55) {
    return false;
  }
  if (abs(cache_size - cache->size()) > 0.02 * cache_size) {
    return false;
  }

  ats_free(r);

  rprintf(t, "RamCache %s Test Done\r", name);

  return pass;
}

REGRESSION_TEST(ram_cache)(RegressionTest *t, int level, int *pstatus)
{
  // Run with -R 3 for now to trigger this check, until we figure out the CI
  if (REGRESSION_TEST_EXTENDED > level) {
    *pstatus = REGRESSION_TEST_PASSED;
    return;
  }

  if (cacheProcessor.IsCacheEnabled() != CACHE_INITIALIZED) {
    rprintf(t, "cache not initialized");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }
  for (int s = 20; s <= 28; s += 4) {
    int64_t cache_size = 1LL << s;
    *pstatus           = REGRESSION_TEST_PASSED;
    if (!test_RamCache(t, new_RamCacheLRU(), "LRU", cache_size) || !test_RamCache(t, new_RamCacheCLFUS(), "CLFUS", cache_size)) {
      *pstatus = REGRESSION_TEST_FAILED;
    }
  }
}
