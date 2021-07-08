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

#pragma once

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

  PinnedDocTable() : Continuation(new_ProxyMutex())
  {
    memset(static_cast<void *>(bucket), 0, sizeof(Queue<PinnedDocEntry>) * PINNED_DOC_TABLE_SIZE);
  }
};

struct CacheTestHost {
  char *name;
  unsigned int xlast_cachable_id;
  double xprev_host_prob;
  double xnext_host_prob;

  CacheTestHost() : name(nullptr), xlast_cachable_id(0), xprev_host_prob(0), xnext_host_prob(0) {}
};

struct CacheTestHeader {
  CacheTestHeader() : serial(0) {}
  uint64_t serial;
};

struct CacheTestSM : public RegressionSM {
  int start_memcpy_on_clone = 0; // place all variables to be copied between these markers

  // Cache test instance name. This is a pointer to a string literal, so copying is safe.
  const char *cache_test_name = nullptr;

  Action *timeout               = nullptr;
  Action *cache_action          = nullptr;
  ink_hrtime start_time         = 0;
  CacheVConnection *cache_vc    = nullptr;
  VIO *cvio                     = nullptr;
  MIOBuffer *buffer             = nullptr;
  IOBufferReader *buffer_reader = nullptr;
  CacheHTTPInfo info;
  char urlstr[1024];
  int64_t total_size = 0;
  int64_t nbytes     = -1;
  CacheKey key;
  int repeat_count         = 0;
  int expect_event         = EVENT_NONE;
  int expect_initial_event = EVENT_NONE;
  int initial_event        = EVENT_NONE;
  uint64_t content_salt    = 0;
  CacheTestHeader header;
  int end_memcpy_on_clone = 0; // place all variables to be copied between these markers

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
    timeout = nullptr;
  }

  // RegressionSM API
  void
  run() override
  {
    rprintf(this->t, "running %s (%p)\n", this->cache_test_name, this);
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    timeout = eventProcessor.schedule_imm(this);
  }

  RegressionSM *clone() override = 0;

  CacheTestSM(RegressionTest *t, const char *name);
  CacheTestSM(const CacheTestSM &ao);
  ~CacheTestSM() override;
};

// It is 2010 and C++ STILL doesn't have closures, a technology of the 1950s, unbelievable
#define CACHE_SM(_t, _sm, _f)                                               \
  struct CacheTestSM__##_sm : public CacheTestSM {                          \
    void                                                                    \
    make_request_internal() _f                                              \
                                                                            \
      CacheTestSM__##_sm(RegressionTest *t)                                 \
      : CacheTestSM(t, #_sm)                                                \
    {                                                                       \
    }                                                                       \
                                                                            \
    CacheTestSM__##_sm(const CacheTestSM__##_sm &xsm) : CacheTestSM(xsm) {} \
    RegressionSM *                                                          \
    clone()                                                                 \
    {                                                                       \
      return new CacheTestSM__##_sm(*this);                                 \
    }                                                                       \
  } _sm(_t);

void force_link_CacheTest();
