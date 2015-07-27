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

struct PinnedDocEntry {
  CacheKey key;
  ink_time_t time;
  LINK(PinnedDocEntry, link);
};

struct PinnedDocTable : public Continuation {
  Queue<PinnedDocEntry> bucket[PINNED_DOC_TABLE_SIZE];

  void insert(CacheKey *key, ink_time_t time, int update);
  int probe(CacheKey *key);
  int remove(CacheKey *key);
  int cleanup(int event, Event *e);

  PinnedDocTable() : Continuation(new_ProxyMutex()) { memset(bucket, 0, sizeof(Queue<PinnedDocEntry>) * PINNED_DOC_TABLE_SIZE); }
};

struct CacheTestHost {
  char *name;
  volatile unsigned int xlast_cachable_id;
  double xprev_host_prob;
  double xnext_host_prob;

  CacheTestHost() : name(NULL), xlast_cachable_id(0), xprev_host_prob(0), xnext_host_prob(0) {}
};

struct CacheTestHeader {
  uint64_t serial;
};

struct CacheTestSM : public RegressionSM {
  int start_memcpy_on_clone; // place all variables to be copied between these markers
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
  int64_t total_size;
  int64_t nbytes;
  CacheKey key;
  int repeat_count;
  int expect_event;
  int expect_initial_event;
  int initial_event;
  uint64_t content_salt;
  CacheTestHeader header;
  int end_memcpy_on_clone; // place all variables to be copied between these markers

  void fill_buffer();
  int check_buffer();
  int check_result(int event);
  int complete(int event);
  int event_handler(int event, void *edata);
  void
  make_request()
  {
    start_time = Thread::get_hrtime();
    make_request_internal();
  }
  virtual void make_request_internal() = 0;
  virtual int open_read_callout();
  virtual int open_write_callout();

  void
  cancel_timeout()
  {
    if (timeout)
      timeout->cancel();
    timeout = 0;
  }

  // RegressionSM API
  void
  run()
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    timeout = eventProcessor.schedule_imm(this);
  }
  virtual RegressionSM *clone() = 0;

  CacheTestSM(RegressionTest *t);
  CacheTestSM(const CacheTestSM &ao);
  ~CacheTestSM();
};

// It is 2010 and C++ STILL doesn't have closures, a technology of the 1950s, unbelievable
#define CACHE_SM(_t, _sm, _f)                                               \
  struct CacheTestSM__##_sm : public CacheTestSM {                          \
    void                                                                    \
    make_request_internal() _f CacheTestSM__##_sm(RegressionTest *t)        \
      : CacheTestSM(t)                                                      \
    {                                                                       \
    }                                                                       \
    CacheTestSM__##_sm(const CacheTestSM__##_sm &xsm) : CacheTestSM(xsm) {} \
    RegressionSM *                                                          \
    clone()                                                                 \
    {                                                                       \
      return new CacheTestSM__##_sm(*this);                                 \
    }                                                                       \
  } _sm(_t);

void force_link_CacheTest();

#endif /* __P_CACHE_TEST_H__ */
