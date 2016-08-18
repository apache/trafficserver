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

#ifndef _PREFETCH_H_
#define _PREFETCH_H_

#include <ts/IpMap.h>
#include "TransformInternal.h"

#ifdef PREFETCH

#include "api/ts/experimental.h"

class BlasterUrlList;
class PrefetchUrlBlaster;
class PrefetchBlaster;
extern BlasterUrlList *multicastUrlBlaster;

struct PrefetchConfiguration {
  int prefetch_enabled;
  IpMap ip_map;
  struct html_tag *html_tags_table;
  struct html_tag *html_attrs_table;

  int local_http_server_port;
  int stuffer_port;

  int url_buffer_size;
  int url_buffer_timeout;

  TSPrefetchBlastData default_url_blast;
  TSPrefetchBlastData default_data_blast;

  int keepalive_timeout;
  int push_cached_objects;

  unsigned int max_object_size;

  unsigned int max_recursion; // limit on depth of recursive prefetch
  unsigned int redirection;   // limit on depth of redirect prefetch

  TSPrefetchHook pre_parse_hook;
  TSPrefetchHook embedded_url_hook;
  TSPrefetchHook embedded_obj_hook;

  PrefetchConfiguration()
    : prefetch_enabled(0),
      html_tags_table(0),
      html_attrs_table(0),
      local_http_server_port(0),
      stuffer_port(0),
      url_buffer_size(0),
      url_buffer_timeout(0),
      keepalive_timeout(0),
      push_cached_objects(0),
      max_object_size(0),
      max_recursion(0),
      redirection(0),
      pre_parse_hook(0),
      embedded_url_hook(0),
      embedded_obj_hook(0)
  {
  }
  int readConfiguration();
  void readHtmlTags(int fd, html_tag **ptags, html_tag **pattrs);
};

// TODO: This used to be private, which seems wrong.
class PrefetchUrlEntry : public RefCountObj
{
public:
  PrefetchUrlEntry()
    : url(0), len(INT_MAX), resp_blaster(0), object_buf_status(TS_PREFETCH_OBJ_BUF_NOT_NEEDED), blaster_link(0), hash_link(0)
  {
    ink_zero(req_ip);
    ink_zero(child_ip);
    ink_zero(url_multicast_ip);
    ink_zero(data_multicast_ip);
    refcount_inc();
  }

  void
  init(char *str, INK_MD5 &xmd5)
  {
    len = strlen(url = str) + 1;
    md5 = xmd5;
  }
  void free();

  PrefetchUrlEntry *
  assign()
  {
    refcount_inc();
    return this;
  };

  char *url;
  int len;
  INK_MD5 md5;

  PrefetchBlaster *resp_blaster;

  int object_buf_status;

  IpEndpoint req_ip; /*ip address where request is coming from */
  IpEndpoint child_ip;
  IpEndpoint url_multicast_ip;
  IpEndpoint data_multicast_ip;

  PrefetchUrlEntry *blaster_link;
  PrefetchUrlEntry *hash_link;

private:
  // this private copy ctor is set to prevent from being used
  // coverity[uninit_member]
  PrefetchUrlEntry(const PrefetchUrlEntry &){};
};

extern ClassAllocator<PrefetchUrlEntry> prefetchUrlEntryAllocator;

inline void
PrefetchUrlEntry::free()
{
  if (refcount_dec() == 0) {
    ats_free(url);
    prefetchUrlEntryAllocator.free(this);
  }
}

class PrefetchTransform : public INKVConnInternal, public RefCountObj
{
  /* Could be 127, 511 */
  enum {
    HASH_TABLE_LENGTH = 61,
  };

public:
  PrefetchTransform(HttpSM *sm, HTTPHdr *resp);
  ~PrefetchTransform();
  void
  free()
  {
    if (refcount_dec() == 0)
      delete this;
  };
  PrefetchTransform *
  assign()
  {
    refcount_inc();
    return this;
  };

  int handle_event(int event, void *edata);
  int parse_data(IOBufferReader *reader);
  int redirect(HTTPHdr *resp);

  PrefetchUrlEntry *hash_add(char *url);

public:
  MIOBuffer *m_output_buf;
  IOBufferReader *m_output_reader;
  VIO *m_output_vio;

  HttpSM *m_sm;

  char *url;

  HtmlParser html_parser;

  PrefetchUrlEntry *hash_table[HASH_TABLE_LENGTH];

  BlasterUrlList *udp_url_list;
  BlasterUrlList *tcp_url_list;

  const char *domain_start;
  const char *domain_end;
  const char *host_start;
  int host_len;
  bool no_dot_in_host;
};

extern TSPrefetchBlastData const UDP_BLAST_DATA;
extern TSPrefetchBlastData const TCP_BLAST_DATA;

// blaster
class BlasterUrlList : public Continuation
{
  int timeout; // in milliseconds
  Action *action;
  int mtu;
  TSPrefetchBlastData blast;

  PrefetchUrlEntry *list_head;
  int cur_len;

public:
  BlasterUrlList() : Continuation(), timeout(0), action(0), mtu(0), list_head(0), cur_len(0) {}
  void
  init(TSPrefetchBlastData const &bdata = UDP_BLAST_DATA, int tout = 0, int xmtu = INT_MAX)
  {
    SET_HANDLER((int (BlasterUrlList::*)(int, void *))(&BlasterUrlList::handleEvent));
    mutex   = new_ProxyMutex();
    blast   = bdata;
    timeout = tout;
    mtu     = xmtu;
  }

  void free();

  int handleEvent(int event, void *data);
  void invokeUrlBlaster();
};

extern ClassAllocator<BlasterUrlList> blasterUrlListAllocator;

inline void
BlasterUrlList::free()
{
  mutex = NULL;
  blasterUrlListAllocator.free(this);
}

class PrefetchUrlBlaster : public Continuation
{
public:
  typedef int (PrefetchUrlBlaster::*EventHandler)(int, void *);

  PrefetchUrlBlaster() : url_head(0), action(0) { ink_zero(blast); }
  void init(PrefetchUrlEntry *list_head, TSPrefetchBlastData const &u_bd = UDP_BLAST_DATA);

  void free();

  PrefetchUrlEntry *url_head;
  TSPrefetchBlastData blast;

  Action *action;

  void writeBuffer(MIOBuffer *buf);

  int udpUrlBlaster(int event, void *data);
};

extern ClassAllocator<PrefetchUrlBlaster> prefetchUrlBlasterAllocator;

void
PrefetchUrlBlaster::init(PrefetchUrlEntry *list_head, TSPrefetchBlastData const &u_bd)
{
  /* More clean up necessary... we should not need this class
     XXXXXXXXX */
  mutex = new_ProxyMutex();

  url_head = list_head;
  blast    = u_bd;

  SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());

  udpUrlBlaster(SIMPLE_EVENT_EVENTS_START, NULL);
}

inline void
BlasterUrlList::invokeUrlBlaster()
{
  PrefetchUrlBlaster *u_blaster = prefetchUrlBlasterAllocator.alloc();
  u_blaster->init(list_head, blast);
  list_head = NULL;
  cur_len   = 0;
}

class PrefetchBlaster : public Continuation
{
public:
  typedef int (PrefetchBlaster::*EventHandler)(int event, void *data);

  PrefetchBlaster()
    : Continuation(),
      url_ent(0),
      transform(0),
      url_list(0),
      request(0),
      cache_http_info(0),
      buf(0),
      reader(0),
      serverVC(0),
      n_pkts_sent(0),
      seq_no(0),
      io_block(0){};
  ~PrefetchBlaster(){};

  int init(PrefetchUrlEntry *entry, HTTPHdr *request, PrefetchTransform *p_trans);

  int handleEvent(int event, void *data);
  int bufferObject(int event, void *data);
  int blastObject(int event, void *data);
  int httpClient(int event, void *data);

  int invokeBlaster();
  void initCacheLookupConfig();

  void handleCookieHeaders(HTTPHdr *req_hdr, HTTPHdr *resp_hdr, const char *domain_start, const char *domain_end,
                           const char *host_start, int host_len, bool no_dot);

  void free();

  PrefetchUrlEntry *url_ent;
  PrefetchTransform *transform;
  BlasterUrlList *url_list;

  HTTPHdr *request;
  CacheHTTPInfo *cache_http_info;

  MIOBuffer *buf;
  IOBufferReader *reader;

  VConnection *serverVC;

  TSPrefetchBlastData data_blast;

  CacheLookupHttpConfig cache_lookup_config;

  // udp related:
  uint32_t n_pkts_sent;
  uint32_t seq_no;
  IOBufferBlock *io_block;
};

extern ClassAllocator<PrefetchBlaster> prefetchBlasterAllocator;

/*Conncetion keep alive*/

#define PRELOAD_HEADER_LEN 12 // this is the new header
// assuming bigendian bit order. does any body have a little endian bit order?
#define PRELOAD_HDR_URL_PROMISE_FLAG (0x40000000)
#define PRELOAD_HDR_RESPONSE_FLAG (0x80000000)
#define PRELOAD_UDP_HEADER_LEN 12
#define PRELOAD_UDP_LAST_PKT_FLAG (0x80000000)
#define PRELOAD_UDP_PKT_NUM_MASK (0x7fffffff)

class KeepAliveConn : public Continuation
{
public:
  KeepAliveConn() : Continuation(), nbytes_added(0) { ink_zero(ip); }
  int init(IpEndpoint const &ip, MIOBuffer *buf, IOBufferReader *reader);
  void free();

  int append(IOBufferReader *reader);
  int handleEvent(int event, void *data);

  IpEndpoint ip;

  MIOBuffer *buf;
  IOBufferReader *reader;

  MIOBuffer *read_buf;

  NetVConnection *childVC;
  VIO *vio;

  KeepAliveConn *next;

  int64_t nbytes_added;
};

class KeepAliveConnTable
{
public:
  KeepAliveConnTable() : arr(NULL){};

  int init();
  void free();
  static int ip_hash(IpEndpoint const &ip);
  int append(IpEndpoint const &ip, MIOBuffer *buf, IOBufferReader *reader);

  typedef struct {
    KeepAliveConn *conn;
    Ptr<ProxyMutex> mutex;
  } conn_elem;

  conn_elem *arr;
};
extern KeepAliveConnTable *g_conn_table;

class KeepAliveLockHandler : public Continuation
{
  /* Used when we miss the lock for the connection */

public:
  KeepAliveLockHandler() : Continuation() { ink_zero(ip); };
  void
  init(IpEndpoint const &xip, MIOBuffer *xbuf, IOBufferReader *xreader)
  {
    mutex = g_conn_table->arr[KeepAliveConnTable::ip_hash(xip)].mutex;

    ats_ip_copy(&ip, &xip);
    buf    = xbuf;
    reader = xreader;

    SET_HANDLER(&KeepAliveLockHandler::handleEvent);
    this_ethread()->schedule_in(this, HRTIME_MSECONDS(10));
  }

  ~KeepAliveLockHandler() { mutex = NULL; }
  int handleEvent(int event, void *data);

  IpEndpoint ip;
  MIOBuffer *buf;
  IOBufferReader *reader;
};

#define PREFETCH_CONFIG_UPDATE_TIMEOUT (HRTIME_SECOND * 60)

#endif // PREFETCH

#endif // _PREFETCH_H_
