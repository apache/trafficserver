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

#ifndef tsmemcache_h
#define tsmemcache_h

#include "I_EventSystem.h"
#include "I_Net.h"
#include "I_Cache.h"
#include "tscore/I_Version.h"

#include "ts/ts.h" // plugin header
#include "protocol_binary.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_hrtime.h"
#include "tscore/CryptoHash.h"

#define TSMEMCACHE_VERSION "1.0.0"
#define TSMEMCACHE_MAX_CMD_SIZE (128 * 1024 * 1024) // silly large
#define TSMEMCACHE_MAX_KEY_LEN 250
#define TSMEMCACHE_TMP_CMD_BUFFER_SIZE 320
#define TSMEMCACHE_HEADER_MAGIC 0x8765ACDC
#define TSMEMCACHE_RETRY_WRITE_INTERVAL HRTIME_MSECONDS(20)

#define TSMEMCACHE_WRITE_SYNC 0 // not yet

#define TSMEMCACHE_EVENT_GOT_ITEM 100000
#define TSMEMCACHE_EVENT_GOT_KEY 100001
#define TSMEMCACHE_STREAM_DONE 100002
#define TSMEMCACHE_TUNNEL_DONE 100003

#define CHECK_RET(_e, _r) \
  do {                    \
    int ret = (_e);       \
    if (ret != _r)        \
      return _r;          \
  } while (0)
#define WRITE(_s) write(_s "", sizeof(_s "") - 1)
#define STRLEN(_s) (sizeof(_s "") - 1)

struct MCCacheHeader {
  uint32_t magic;
  uint32_t flags;
  uint32_t nkey : 8;
  uint32_t reserved : 24;
  uint32_t exptime; // seconds offset from settime
  uint64_t settime;
  uint64_t cas;
  uint64_t nbytes;
  char *
  key()
  {
    return ((char *)this) + sizeof(MCCacheHeader);
  }
  int
  len()
  {
    return sizeof(MCCacheHeader) + nkey;
  }
};

struct MCAccept : public Continuation {
#ifndef HAVE_TLS
  ProxyAllocator *theMCThreadAllocator;
#endif
  int accept_port;
  int main_event(int event, void *netvc);

  MCAccept()
    :
#ifndef HAVE_TLS
      theMCThreadAllocator(NULL),
#endif
      accept_port(0)
  {
    SET_HANDLER(&MCAccept::main_event);
  }
};

#define TS_PUSH_HANDLER(_h)                    \
  do {                                         \
    handler_stack[ihandler_stack++] = handler; \
    SET_HANDLER(_h);                           \
  } while (0)

#define TS_POP_HANDLER handler = handler_stack[--ihandler_stack]
#define TS_POP_CALL(_event, _data) handleEvent((TS_POP_HANDLER, _event), _data)
#define TS_SET_CALL(_h, _event, _data) handleEvent((SET_HANDLER(_h), _event), _data)
#define ASCII_RESPONSE(_s) ascii_response((_s "\r\n"), sizeof(_s "\r\n") - 1)
#define ASCII_ERROR() ascii_response(("ERROR\r\n"), sizeof("ERROR\r\n") - 1)
#define ASCII_CLIENT_ERROR(_s) ascii_response(("CLIENT_ERROR: " _s "\r\n"), sizeof("CLIENT_ERROR: " _s "\r\n") - 1)
#define ASCII_SERVER_ERROR(_s) ascii_response(("SERVER_ERROR: " _s "\r\n"), sizeof("SERVER_ERROR: " _s "\r\n") - 1)
#define STRCMP(_s, _const_string) strncmp(_s, _const_string "", sizeof(_const_string) - 1)

struct MC : Continuation {
  Action *pending_action;
  int ihandler_stack;
  int swallow_bytes;
  int64_t exptime;
  ContinuationHandler handler_stack[2];
  VConnection *nvc;
  MIOBuffer *rbuf, *wbuf, *cbuf;
  VIO *rvio, *wvio;
  IOBufferReader *reader, *writer, *creader;
  CacheVConnection *crvc, *cwvc;
  VIO *crvio, *cwvio;
  CacheKey cache_key;
  MCCacheHeader *rcache_header, *wcache_header;
  MCCacheHeader header;
  char tmp_cache_header_key[256];
  protocol_binary_request_header binary_header;
  union {
    protocol_binary_response_get get;
  } res;
  char *key, *tbuf;
  int read_offset;
  int end_of_cmd; // -1 means that it is already consumed
  int ngets;
  char tmp_cmd_buffer[TSMEMCACHE_TMP_CMD_BUFFER_SIZE];
  union {
    struct {
      unsigned int noreply : 1;
      unsigned int return_cas : 1;
      unsigned int set_add : 1;
      unsigned int set_cas : 1;
      unsigned int set_append : 1;
      unsigned int set_prepend : 1;
      unsigned int set_replace : 1;
      unsigned int set_incr : 1;
      unsigned int set_decr : 1;
    } f;
    unsigned int ff;
  };
  uint64_t nbytes;
  uint64_t delta;

  static int32_t verbosity;
  static ink_hrtime last_flush;
  static int64_t next_cas;

  int write_to_client(int64_t ntowrite = -1);
  int write_then_read_from_client(int64_t ntowrite = -1);
  int stream_then_read_from_client(int64_t ntowrite);
  int write_then_close(int64_t ntowrite = -1);
  int read_from_client();
  int get_item();
  int set_item();
  int delete_item();
  int read_from_client_event(int event, void *data);
  int swallow_then_read_event(int event, void *data);
  int swallow_cmd_then_read_from_client_event(int event, void *data);
  int read_binary_from_client_event(int event, void *data);
  int read_ascii_from_client_event(int event, void *data);
  int binary_get_event(int event, void *data);
  int cache_read_event(int event, void *data);
  int write_then_close_event(int event, void *data);
  int stream_event(int event, void *data); // cache <=> client
  int tunnel_event(int event, void *data); // cache <=> cache

  char *get_ascii_input(int n, int *end);
  int get_ascii_key(char *s, char *e);
  int ascii_response(const char *s, int len);
  int ascii_get(char *s, char *e);
  int ascii_gets();
  int ascii_set(char *s, char *e);
  int ascii_delete(char *s, char *e);
  int ascii_incr_decr(char *s, char *e);
  int ascii_get_event(int event, void *data);
  int ascii_set_event(int event, void *data);
  int ascii_delete_event(int event, void *data);
  int ascii_incr_decr_event(int event, void *data);

  int write_binary_error(protocol_binary_response_status err, int swallow);
  void add_binary_header(uint16_t err, uint8_t hdr_len, uint16_t key_len, uint32_t body_len);
  int write_binary_response(const void *d, int hlen, int keylen, int dlen);
  int protocol_error();
  int bin_read_key();

  void new_connection(NetVConnection *netvc, EThread *thread);
  int unexpected_event();
  int die();
};

int init_tsmemcache(int port = 11211);

// INLINE FUNCTIONS

static inline char *
xutoa(uint32_t i, char *e)
{
  do {
    *--e = (char)(i % 10 + 48);
  } while ((i /= 10) > 0);
  return e;
}

static inline char *
xutoa(uint64_t i, char *e)
{
  do {
    *--e = (char)(i % 10 + 48);
  } while ((i /= 10) > 0);
  return e;
}

static inline uint64_t
xatoull(char *s, char *e)
{
  uint64_t n = 0;
  if (isdigit(*s)) {
    n = *s - '0';
    s++;
    if (s >= e) {
      return n;
    }
  }
  while (isdigit(*s)) {
    n *= 10;
    n += *s - '0';
    s++;
  }
  return n;
}

#endif
