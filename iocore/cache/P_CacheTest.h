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

#define MAX_HOSTS_POSSIBLE 256

#define PINNED_DOC_TABLE_SIZE 16

#define PINNED_DOC_TABLES 246


struct PinnedDocEntry
{

  CacheKey key;
  ink_time_t time;
    Link<PinnedDocEntry> link;
};

struct PinnedDocTable:public Continuation
{

  Queue<PinnedDocEntry> bucket[PINNED_DOC_TABLE_SIZE];

  PinnedDocTable():Continuation(new_ProxyMutex())
  {
    memset(bucket, 0, sizeof(Queue<PinnedDocEntry>) * PINNED_DOC_TABLE_SIZE);
  }

  void insert(CacheKey * key, ink_time_t time, int update);
  int probe(CacheKey * key);
  int remove(CacheKey * key);
  int cleanup(int event, Event * e);
};

class CacheTestHost
{
public:

  char *name;
  volatile unsigned int xlast_cachable_id;
  volatile unsigned int xlast_ftp_cachable_id;
  double xprev_host_prob;
  double xnext_host_prob;

    CacheTestHost():name(NULL), xlast_cachable_id(0), xlast_ftp_cachable_id(0), xprev_host_prob(0), xnext_host_prob(0)
  {
  }
};

class CacheSMTest;

class CacheTestControl:public Continuation
{
public:
  CacheTestControl();
  ~CacheTestControl();

  int open(const char *path);
  int parse(const char *buf, int length);
  int get_token(const char *&buf, int &length, char **token_start, char **token_end);
  int get_left_paren(const char *&buf, int &length);
  int get_right_paren(const char *&buf, int &length);
  int get_float(const char *&buf, int &length, double *val);
  int get_integer(const char *&buf, int &length, int *val);
  int get_string(const char *&buf, int &length, char str[1024]);
  int get_symbol(const char *&buf, int &length, char symbol[1024]);

  int users();
  int buffer_size();
  int get_request(CacheSMTest * test, char *buffer, int size);

  void lookup_success(ink_hrtime elapsed);
  void lookup_failure(ink_hrtime elapsed);
  void read_success(ink_hrtime elapsed, int bytes);
  void read_failure();
  void vc_read_failure();
  void write_success(ink_hrtime elapsed, int bytes);
  void write_failure();
  void aio_success(ink_hrtime elapsed);
  void vc_write_failure();
  void update_success();
  void update_failure();
  void vc_update_failure();
  void pin_success();
  void pin_failure();
  PinnedDocTable *getPinnedTable(INK_MD5 * md5);

  void rw_failure();
  int print_stats_event(int event, void *edata);
  int print_stats(void);

public:
    ink_hrtime start_time;
  ink_hrtime last_time;

  int xusers;
  volatile int xstate_machines;
  volatile int xrequests;
  int xbuffer_size;
  int xaverage_over;
  int xwarmup;
  int xrun_length;
  int xmean_doc_size;
  int xnum_hosts;
  CacheTestHost host_array[MAX_HOSTS_POSSIBLE];
  volatile unsigned int xlast_cachable_id;
  volatile unsigned int xlast_ftp_cachable_id;
  double xhotset_probability;
  double xremove_probability;
  double xupdate_probability;
  double xpure_update_probability;
  double xcancel_probability;
  double xabort_probability;
  double xhttp_req_probability;
  int xnum_alternates;
  int xcheck_content;
  int xfill_cache;
  int xaio_test;
  double xpin_percent;
  int xmean_pin_time;
  PinnedDocTable *pin_tables[PINNED_DOC_TABLES];
  double last_elapsed;
  int n_average;
  double avg_ops_sec;
  double avg_aio_ops_sec;
  volatile int lookup_successes;
  volatile int lookup_failures;
  volatile ink64 lookup_hit_time;
  volatile ink64 lookup_miss_time;
  volatile int read_successes;
  int lread_successes;
  volatile int read_failures;
  volatile int vc_read_failures;
  volatile ink64 read_bytes;
  volatile ink64 read_time;
  volatile int write_successes;
  int lwrite_successes;
  volatile int write_failures;
  volatile int aio_successes;
  int laio_successes;
  volatile ink64 aio_time;
  volatile int vc_write_failures;
  volatile ink64 write_bytes;
  volatile ink64 write_time;
  volatile int update_successes;
  volatile int update_failures;
  volatile int vc_update_failures;
  volatile int pin_failures;
  volatile int pin_successes;
  volatile int pin_writes;
  volatile int rw_failures;
  Event *trigger;
};

int runCacheTest();
int runCacheTest(CacheTestControl *);



class CacheSMTest:public Continuation
{
public:
  CacheSMTest(CacheTestControl * xcontrol, int xid);
   ~CacheSMTest();

  void make_request();
  void fill_buffer();
  void check_buffer();
  int event_handler(int event, void *edata);

public:
    Action * timeout;
  Action *cache_action;
  ink_hrtime start_time;
  CacheTestControl *control;
  CacheVConnection *cache_vc;
  MIOBuffer *buffer;
  IOBufferReader *buffer_reader;
#ifdef HTTP_CACHE
  CacheLookupHttpConfig params;
  CacheHTTPInfo info;
#endif
  char urlstr[1024];
  int iterations;
  int request_size;
  int total_size;
  int done_size;
  int buffer_size_index;
  int idx;
  int id;
  INK_MD5 md5;
  ink_time_t pin_in_cache;
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
  Event *m_timeout;
};


#endif /* __P_CACHE_TEST_H__ */
