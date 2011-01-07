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


#include "NetVConnection.h"
#include "CacheInternal.h"
#include "OneWayTunnel.h"
#include "HttpTransact.h"

/* A URL promise will be deleted if the following timeout occurs before the
   corresponding data arrives */
#define STUFFER_URL_PROMISE_TIMEOUT_MSECS 120000

/* We block the cache for maximum of this time while waiting for a URL data to
  arrive */
#define STUFFER_CACHE_BLOCK_TIMEOUT_MSECS 120000

//from Prefetch.cc
#define KEEPALIVE_LEN_BYTES sizeof(int)

/*Note on locking: TS Micro is going to run on single processror
  machines.  On top of that we cannot have more than one threads for
  events even if we want to due to resource contraints. For now, all
  the stuffer objects and the hash table use the same mutex namely
  stuffer_htable->mutex.
*/
class StufferURLPromise;

class StufferHashTable
{

public:

  StufferHashTable(int sz)
  {
    size = sz;
    array = NEW(new StufferURLPromise *[size]);
    memset(array, 0, size * sizeof(StufferURLPromise *));
    mutex = new_ProxyMutex();
  }
   ~StufferHashTable()
  {
    delete array;
    mutex = NULL;
  }

  int index(const char *url);
  StufferURLPromise **position(const char *url);
  StufferURLPromise *lookup(const char *url)
  {
    return *position(url);
  }
  void add(char *url);
  void remove(StufferURLPromise * e);

  ProxyMutexPtr mutex;
  int size;
  StufferURLPromise **array;
};

extern StufferHashTable *stuffer_htable;
class StufferCacheWriter;

class Stuffer:public Continuation
{

  enum
  {
    STUFFER_START,
    URL_PROMISES,
    URL_OBJECT,
    CACHE_WRITE,
    STUFFER_DONE
  };

public:

  Stuffer()
    : Continuation(), state(STUFFER_START), buf(0), source_vc(0),
      cur_ntodo(0), cache_writer(0), active_cache_writers(0), active_cache_buffer(0)
  {  }

  ~Stuffer() {
    mutex = NULL;
  }

  int init(NetVConnection * netvc)
  {
    mutex = stuffer_htable->mutex;
    source_vc = netvc;
    SET_HANDLER(&Stuffer::mainEvent);
    //this_ethread()->schedule_imm(this);
    mainEvent(NET_EVENT_ACCEPT, NULL);
    return EVENT_DONE;
  }

  void free();
  void reset();

  int mainEvent(int event, void *data);
  int tunnel(int event, void *data);

  int processInitialData();

  int state;

  MIOBuffer *buf;
  IOBufferReader *reader;
  NetVConnection *source_vc;
  VIO *source_vio;

  int64_t cur_ntodo;

  StufferCacheWriter *cache_writer;
  int active_cache_writers;
  int active_cache_buffer;
};

#define MAX_CACHE_WRITERS_OUTSTANDING 10
#define MAX_KEEPALIVE_BUFFER	(200 * 1024)
#define STUFFER_CACHE_WRITER_ID 0xCAC11E0B

class StufferCacheWriter:public Continuation
{

  /* This class takes care of writing to the cache. This is done in
     a seperate class so that we can parallelize writing to the
     cache */
  enum
  {
    PARSE_HEADERS,
    CACHE_READ_OPEN,
    CACHE_WRITE_OPEN,
    CACHE_WRITE,

    PARSE_START,
    PARSE_REQ,
    PARSE_RESP
  };

public:
    StufferCacheWriter()
  : Continuation(), object_id(STUFFER_CACHE_WRITER_ID), nadded(0),
    state(PARSE_HEADERS), parse_state(PARSE_START), got_read_complete(0), cache_vc(0), open_read_vc(0), url(0)
  {
    http_parser_init(&http_parser);
  };

  void init(Stuffer * s, int64_t ntodo);
  void free();
  int addData(int max);

  int mainEvent(int event, void *data);
  int parseHeaders();
  void initCacheLookupConfig();

  unsigned int object_id;
  MIOBuffer *buf;
  IOBufferReader *reader;
  int64_t ntodo;
  int nadded;

  int state;
  int parse_state;
  int got_read_complete;

  Stuffer *stuffer;

  CacheVC *cache_vc;
  VIO *cache_vio;

  CacheVC *open_read_vc;

  CacheHTTPInfo http_info;
  HTTPParser http_parser;

  char *url;

  CacheLookupHttpConfig cache_lookup_config;
};

#define STUFFER_CACHE_WRITER(cont) \
(((StufferCacheWriter *)(cont))->object_id == STUFFER_CACHE_WRITER_ID)

extern ClassAllocator<Stuffer> stufferAllocator;

struct cache_obj_list
{
  cache_obj_list *next;
  CacheVC *cache_vc;

    cache_obj_list():next(0), cache_vc(0)
  {
  }
};

class StufferURLPromise:public Continuation
{

public:
  StufferURLPromise():Continuation(), url(0), next(0)
  {
  };
  ~StufferURLPromise() {
    mutex = NULL;
  };

  int init(char *str)
  {
    mutex = stuffer_htable->mutex;
    url = str;
    SET_HANDLER(&StufferURLPromise::mainEvent);
    overall_timeout = this_ethread()->schedule_in(this, ink_hrtime_from_msec(STUFFER_URL_PROMISE_TIMEOUT_MSECS));
    cache_block_timeout = 0;
    return EVENT_DONE;
  }
  void free(bool object_pushed = false);
  void add_waiter(CacheVC * cache_vc);
  int mainEvent(int event, void *data);

  char *url;

  Action *overall_timeout;
  Action *cache_block_timeout;

  /* We will rarely have more than one cache object waiting.
     in that case we will just dynamically allocate these elements */
  cache_obj_list head;

  StufferURLPromise *next;      //used for chaining in the hash table
};

extern ClassAllocator<StufferURLPromise> stufferURLPromiseAllocator;
