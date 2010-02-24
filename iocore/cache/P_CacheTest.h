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

#ifndef __P_CACHE_TEST_H__
#define __P_CACHE_TEST_H__

#include "P_Cache.h"
#include "RegressionSM.h"

#define MAX_HOSTS_POSSIBLE 256
#define PINNED_DOC_TABLE_SIZE 16
#define PINNED_DOC_TABLES 246

struct PinnedDocEntry
{
  CacheKey key;
  ink_time_t time;
  Link<PinnedDocEntry> link;
};

struct PinnedDocTable:Continuation
{
  Queue<PinnedDocEntry> bucket[PINNED_DOC_TABLE_SIZE];

  void insert(CacheKey * key, ink_time_t time, int update);
  int probe(CacheKey * key);
  int remove(CacheKey * key);
  int cleanup(int event, Event * e);

  PinnedDocTable():Continuation(new_ProxyMutex()) {
    memset(bucket, 0, sizeof(Queue<PinnedDocEntry>) * PINNED_DOC_TABLE_SIZE);
  }
};

struct CacheTestHost {
  char *name;
  volatile unsigned int xlast_cachable_id;
  volatile unsigned int xlast_ftp_cachable_id;
  double xprev_host_prob;
  double xnext_host_prob;

  CacheTestHost():name(NULL), xlast_cachable_id(0), xlast_ftp_cachable_id(0), 
                  xprev_host_prob(0), xnext_host_prob(0) {}
};

struct CacheTestSM;

struct CacheTestSM : RegressionSM {
  Action *timeout;
  Action *cache_action;
  ink_hrtime start_time;
  CacheVConnection *cache_vc;
  VIO *cvio;
  MIOBuffer *buffer;
  IOBufferReader *buffer_reader;
#ifdef HTTP_CACHE
  CacheLookupHttpConfig params;
  CacheHTTPInfo info;
  char urlstr[1024];
#endif
  ink64 total_size;
  CacheKey key;
  int repeat_count;
  int expect_event;
  int expect_initial_event;
  int initial_event;
  union
  {
    unsigned int flags;
    struct
    {
      unsigned int http_request:1;
      unsigned int writing:1;
      unsigned int update:1;
      unsigned int hit:1;
      unsigned int remove:1;
    } f;
  };

  void fill_buffer();
  int check_buffer();
  int check_result(int event);
  int complete(int event);
  int event_handler(int event, void *edata);
  void make_request() {
    start_time = ink_get_hrtime();
    make_request_internal();
  }
  virtual void make_request_internal() = 0;

  void cancel_timeout() {
    if (timeout) timeout->cancel();
    timeout = 0;
  }

  // RegressionSM API
  void run() { MUTEX_LOCK(lock, mutex, this_ethread()); timeout = eventProcessor.schedule_imm(this); }
  virtual RegressionSM *clone() = 0;

  CacheTestSM(RegressionTest *t);
  CacheTestSM(const CacheTestSM &ao);
  ~CacheTestSM();
};

// It is 2010 and C++ STILL doesn't have closures, a technology of the 1950s, unbelievable
#define CACHE_SM(_t, _sm, _f)                \
  struct CacheTestSM__##_sm : CacheTestSM { \
    void make_request_internal() _f \
    CacheTestSM__##_sm(RegressionTest *t) : CacheTestSM(t) {} \
    CacheTestSM__##_sm(const CacheTestSM__##_sm &xsm) : CacheTestSM(xsm) {} \
    RegressionSM *clone() { return new CacheTestSM__##_sm(*this); } \
} _sm(_t);

void verify_cache_api();

#endif /* __P_CACHE_TEST_H__ */
