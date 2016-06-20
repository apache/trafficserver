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

#include "tsmemcache.h"

/*
  TODO
  - on OPEN_WRITE_FAIL don't poll, figure out another way, and timeout
  - factor code better, particularly incr/set
  - MIOBufferAccessor::reader_for
  - cleanup creader dependency in stream_event
 */

#define REALTIME_MAXDELTA 60 * 60 * 24 * 30
#define STRCMP_REST(_c, _s, _e) (((_e) - (_s)) < (int)sizeof(_c) || STRCMP(_s, _c) || !isspace((_s)[sizeof(_c) - 1]))

ClassAllocator<MC> theMCAllocator("MC");

static time_t base_day_time;

// These should be persistent.
volatile int32_t MC::verbosity     = 0;
volatile ink_hrtime MC::last_flush = 0;
volatile int64_t MC::next_cas      = 1;

static void
tsmemcache_constants()
{
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  // jan 1 2010
  tm.tm_year    = 110;
  tm.tm_mon     = 1;
  tm.tm_mday    = 1;
  base_day_time = mktime(&tm);
  ink_assert(base_day_time != (time_t)-1);
}

#ifdef DEBUG
char debug_string_buffer[TSMEMCACHE_TMP_CMD_BUFFER_SIZE];
static char *
mc_string(const char *s, int len)
{
  int l = len;
  while (l && (s[l - 1] == '\r' || s[l - 1] == '\n'))
    l--;
  if (l > TSMEMCACHE_TMP_CMD_BUFFER_SIZE - 1)
    l = TSMEMCACHE_TMP_CMD_BUFFER_SIZE - 1;
  if (l)
    memcpy(debug_string_buffer, s, l);
  debug_string_buffer[l] = 0;
  return debug_string_buffer;
}
#endif

#ifdef DEBUG
#define MCDebugBuf(_t, _s, _l) \
  if (is_debug_tag_set(_t))    \
  printf(_t ": %s\n", mc_string(_s, _l))
#define MCDebug Debug
#else
#define MCDebugBuf(_t, _s, _l) \
  do {                         \
  } while (0)
#define MCDebug \
  if (0)        \
  Debug
#endif

static uint64_t
ink_hton64(uint64_t in)
{
  int32_t val = 1;
  uint8_t *c  = (uint8_t *)&val;
  if (*c == 1) {
    union {
      uint64_t rv;
      uint8_t b[8];
    } x;
#define SWP1B(_x, _y) \
  do {                \
    uint8_t t = (_y); \
    (_y)      = (_x); \
    (_x)      = t;    \
  } while (0)
    x.rv = in;
    SWP1B(x.b[0], x.b[7]);
    SWP1B(x.b[1], x.b[6]);
    SWP1B(x.b[2], x.b[5]);
    SWP1B(x.b[3], x.b[4]);
#undef SWP1B
    return x.rv;
  } else
    return in;
}
#define ink_ntoh64 ink_hton64

int
MCAccept::main_event(int event, void *data)
{
  if (event == NET_EVENT_ACCEPT) {
    NetVConnection *netvc = (NetVConnection *)data;
    MC *mc                = theMCAllocator.alloc();
    if (!mutex->thread_holding)
      mc->new_connection(netvc, netvc->thread);
    else
      mc->new_connection(netvc, mutex->thread_holding);
    return EVENT_CONT;
  } else {
    Fatal("tsmemcache accept received fatal error: errno = %d", -((int)(intptr_t)data));
    return EVENT_CONT;
  }
}

void
MC::new_connection(NetVConnection *netvc, EThread *thread)
{
  nvc              = netvc;
  mutex            = new_ProxyMutex();
  rbuf             = new_MIOBuffer(MAX_IOBUFFER_SIZE);
  rbuf->water_mark = TSMEMCACHE_TMP_CMD_BUFFER_SIZE;
  reader           = rbuf->alloc_reader();
  wbuf             = new_empty_MIOBuffer();
  cbuf             = 0;
  writer           = wbuf->alloc_reader();
  SCOPED_MUTEX_LOCK(lock, mutex, thread);
  rvio         = nvc->do_io_read(this, INT64_MAX, rbuf);
  wvio         = nvc->do_io_write(this, 0, writer);
  header.magic = TSMEMCACHE_HEADER_MAGIC;
  read_from_client();
}

int
MC::die()
{
  if (pending_action && pending_action != ACTION_RESULT_DONE)
    pending_action->cancel();
  if (nvc)
    nvc->do_io_close(1); // abort
  if (crvc)
    crvc->do_io_close(1); // abort
  if (cwvc)
    cwvc->do_io_close(1); // abort
  if (rbuf)
    free_MIOBuffer(rbuf);
  if (wbuf)
    free_MIOBuffer(wbuf);
  if (cbuf)
    free_MIOBuffer(cbuf);
  if (tbuf)
    ats_free(tbuf);
  mutex = NULL;
  theMCAllocator.free(this);
  return EVENT_DONE;
}

int
MC::unexpected_event()
{
  ink_assert(!"unexpected event");
  return die();
}

int
MC::write_then_close(int64_t ntowrite)
{
  SET_HANDLER(&MC::write_then_close_event);
  return write_to_client(ntowrite);
}

int
MC::write_then_read_from_client(int64_t ntowrite)
{
  SET_HANDLER(&MC::read_from_client_event);
  return write_to_client(ntowrite);
}

int
MC::stream_then_read_from_client(int64_t ntowrite)
{
  SET_HANDLER(&MC::read_from_client_event);
  creader = reader;
  TS_PUSH_HANDLER(&MC::stream_event);
  return write_to_client(ntowrite);
}

void
MC::add_binary_header(uint16_t err, uint8_t hdr_len, uint16_t key_len, uint32_t body_len)
{
  protocol_binary_response_header r;

  r.response.magic    = (uint8_t)PROTOCOL_BINARY_RES;
  r.response.opcode   = binary_header.request.opcode;
  r.response.keylen   = (uint16_t)htons(key_len);
  r.response.extlen   = (uint8_t)hdr_len;
  r.response.datatype = (uint8_t)PROTOCOL_BINARY_RAW_BYTES;
  r.response.status   = (uint16_t)htons(err);
  r.response.bodylen  = htonl(body_len);
  r.response.opaque   = binary_header.request.opaque;
  r.response.cas      = ink_hton64(header.cas);

  wbuf->write(&r, sizeof(r));
}

int
MC::write_binary_error(protocol_binary_response_status err, int swallow)
{
  const char *errstr = "Unknown error";
  switch (err) {
  case PROTOCOL_BINARY_RESPONSE_ENOMEM:
    errstr = "Out of memory";
    break;
  case PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND:
    errstr = "Unknown command";
    break;
  case PROTOCOL_BINARY_RESPONSE_KEY_ENOENT:
    errstr = "Not found";
    break;
  case PROTOCOL_BINARY_RESPONSE_EINVAL:
    errstr = "Invalid arguments";
    break;
  case PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS:
    errstr = "Data exists for key.";
    break;
  case PROTOCOL_BINARY_RESPONSE_E2BIG:
    errstr = "Too large.";
    break;
  case PROTOCOL_BINARY_RESPONSE_DELTA_BADVAL:
    errstr = "Non-numeric server-side value for incr or decr";
    break;
  case PROTOCOL_BINARY_RESPONSE_NOT_STORED:
    errstr = "Not stored.";
    break;
  case PROTOCOL_BINARY_RESPONSE_AUTH_ERROR:
    errstr = "Auth failure.";
    break;
  default:
    ink_assert(!"unhandled error");
    errstr = "UNHANDLED ERROR";
    Warning("tsmemcache: unhandled error: %d\n", err);
  }

  size_t len = strlen(errstr);
  add_binary_header(err, 0, 0, len);
  if (swallow > 0) {
    int64_t avail = reader->read_avail();
    if (avail >= swallow) {
      reader->consume(swallow);
    } else {
      swallow_bytes = swallow - avail;
      reader->consume(avail);
      SET_HANDLER(&MC::swallow_then_read_event);
    }
  }
  return 0;
}

int
MC::swallow_then_read_event(int event, void *data)
{
  rvio->nbytes  = INT64_MAX;
  int64_t avail = reader->read_avail();
  if (avail >= swallow_bytes) {
    reader->consume(swallow_bytes);
    swallow_bytes = 0;
    return read_from_client();
  } else {
    swallow_bytes -= avail;
    reader->consume(avail);
    return EVENT_CONT;
  }
}

int
MC::swallow_cmd_then_read_from_client_event(int event, void *data)
{
  int64_t avail = reader->read_avail();
  if (avail) {
    int64_t n = reader->memchr('\n');
    if (n >= 0) {
      reader->consume(n + 1);
      return read_from_client();
    }
    reader->consume(avail);
    return EVENT_CONT;
  }
  return EVENT_CONT;
}

int
MC::protocol_error()
{
  Warning("tsmemcache: protocol error");
  return write_then_close(write_binary_error(PROTOCOL_BINARY_RESPONSE_EINVAL, 0));
}

int
MC::read_from_client()
{
  if (swallow_bytes)
    return TS_SET_CALL(&MC::swallow_then_read_event, VC_EVENT_READ_READY, rvio);
  read_offset = 0;
  end_of_cmd  = 0;
  ngets       = 0;
  ff          = 0;
  if (crvc) {
    crvc->do_io_close();
    crvc  = 0;
    crvio = NULL;
  }
  if (cwvc) {
    cwvc->do_io_close();
    cwvc  = 0;
    cwvio = NULL;
  }
  if (cbuf)
    cbuf->clear();
  ink_assert(!crvc && !cwvc);
  if (tbuf)
    ats_free(tbuf);
  return TS_SET_CALL(&MC::read_from_client_event, VC_EVENT_READ_READY, rvio);
}

int
MC::write_to_client(int64_t towrite)
{
  (void)towrite;
  wvio->nbytes = INT64_MAX;
  wvio->reenable();
  return EVENT_CONT;
}

int
MC::write_binary_response(const void *d, int hlen, int keylen, int dlen)
{
  if (!f.noreply || binary_header.request.opcode == PROTOCOL_BINARY_CMD_GETQ ||
      binary_header.request.opcode == PROTOCOL_BINARY_CMD_GETKQ) {
    add_binary_header(0, hlen, keylen, dlen);
    if (dlen) {
      MCDebug("tsmemcache", "response dlen %d\n", dlen);
      wbuf->write(d, dlen);
    } else
      MCDebug("tsmemcache", "no response\n");
  }
  return writer->read_avail();
}

#define CHECK_READ_AVAIL(_n, _h)                     \
  do {                                               \
    if (reader->read_avail() < _n) {                 \
      switch (event) {                               \
      case VC_EVENT_EOS:                             \
        if ((VIO *)data == rvio)                     \
          break;                                     \
      case VC_EVENT_READ_READY:                      \
        return EVENT_CONT;                           \
      case VC_EVENT_WRITE_READY:                     \
        if (wvio->buffer.reader()->read_avail() > 0) \
          return EVENT_CONT;                         \
      case VC_EVENT_WRITE_COMPLETE:                  \
        return EVENT_DONE;                           \
      default:                                       \
        break;                                       \
      }                                              \
      return die();                                  \
    }                                                \
  } while (0)

static char *
get_pointer(MC *mc, int start, int len)
{
  if (mc->reader->block_read_avail() >= start + len)
    return mc->reader->start() + start;
  // the block of data straddles an IOBufferBlock boundary, exceptional case, malloc
  ink_assert(!mc->tbuf);
  mc->tbuf = (char *)ats_malloc(len);
  mc->reader->memcpy(mc->tbuf, len, start);
  return mc->tbuf;
}

static inline char *
binary_get_key(MC *mc)
{
  return get_pointer(mc, 0, mc->binary_header.request.keylen);
}

int
MC::cache_read_event(int event, void *data)
{
  switch (event) {
  case CACHE_EVENT_OPEN_READ: {
    crvc     = (CacheVConnection *)data;
    int hlen = 0;
    if (crvc->get_header((void **)&rcache_header, &hlen) < 0)
      goto Lfail;
    if (hlen < (int)sizeof(MCCacheHeader) || rcache_header->magic != TSMEMCACHE_HEADER_MAGIC)
      goto Lfail;
    if (header.nkey != rcache_header->nkey || hlen < (int)(sizeof(MCCacheHeader) + rcache_header->nkey))
      goto Lfail;
    if (memcmp(key, rcache_header->key(), header.nkey))
      goto Lfail;
    {
      ink_hrtime t = Thread::get_hrtime();
      if (((ink_hrtime)rcache_header->settime) <= last_flush ||
          t >= ((ink_hrtime)rcache_header->settime) + HRTIME_SECONDS(rcache_header->exptime))
        goto Lfail;
    }
    break;
  Lfail:
    crvc->do_io_close();
    crvc  = 0;
    crvio = NULL;
    event = CACHE_EVENT_OPEN_READ_FAILED; // convert to failure
    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case CACHE_EVENT_OPEN_READ_FAILED:
    break;
  default:
    return EVENT_CONT;
  }
  return TS_POP_CALL(event, data);
}

int
MC::get_item()
{
  TS_PUSH_HANDLER(&MC::cache_read_event);
  MD5Context().hash_immediate(cache_key, (void *)key, (int)header.nkey);
  pending_action = cacheProcessor.open_read(this, &cache_key, true);
  return EVENT_CONT;
}

int
MC::set_item()
{
  MD5Context().hash_immediate(cache_key, (void *)key, (int)header.nkey);
  pending_action = cacheProcessor.open_write(this, &cache_key, true, CACHE_FRAG_TYPE_NONE, header.nbytes,
                                             CACHE_WRITE_OPT_OVERWRITE | TSMEMCACHE_WRITE_SYNC);
  return EVENT_CONT;
}

int
MC::delete_item()
{
  MD5Context().hash_immediate(cache_key, (void *)key, (int)header.nkey);
  pending_action = cacheProcessor.remove(this, &cache_key, CACHE_FRAG_TYPE_NONE);
  return EVENT_CONT;
}

int
MC::binary_get_event(int event, void *data)
{
  ink_assert(!"EVENT_ITEM_GOT is incorrect here");
  if (event != TSMEMCACHE_EVENT_GOT_ITEM) {
    CHECK_READ_AVAIL(binary_header.request.keylen, &MC::binary_get);
    key         = binary_get_key(this);
    header.nkey = binary_header.request.keylen;
    return get_item();
  } else if (event == CACHE_EVENT_OPEN_READ_FAILED) {
    if (f.noreply)
      return read_from_client();
    if (binary_header.request.opcode == PROTOCOL_BINARY_CMD_GETK) {
      add_binary_header(PROTOCOL_BINARY_RESPONSE_KEY_ENOENT, 0, header.nkey, header.nkey);
      wbuf->write(key, header.nkey);
      return write_then_read_from_client();
    } else
      return write_binary_error(PROTOCOL_BINARY_RESPONSE_KEY_ENOENT, 0);
  } else if (event == CACHE_EVENT_OPEN_READ) {
    protocol_binary_response_get *rsp = &res.get;
    uint16_t keylen                   = 0;
    uint32_t bodylen                  = sizeof(rsp->message.body) + (rcache_header->nbytes - 2);
    bool getk =
      (binary_header.request.opcode == PROTOCOL_BINARY_CMD_GETK || binary_header.request.opcode == PROTOCOL_BINARY_CMD_GETKQ);
    if (getk) {
      bodylen += header.nkey;
      keylen = header.nkey;
    }
    add_binary_header(0, sizeof(rsp->message.body), keylen, bodylen);
    rsp->message.header.response.cas = ink_hton64(rcache_header->cas);
    rsp->message.body.flags          = htonl(rcache_header->flags);
    wbuf->write(&rsp->message.body, sizeof(rsp->message.body));
    if (getk)
      wbuf->write(key, header.nkey);
    crvio = crvc->do_io_read(this, rcache_header->nbytes, wbuf);
    return stream_then_read_from_client(rcache_header->nbytes);
  } else
    return unexpected_event();
  return 0;
}

int
MC::bin_read_key()
{
  return -1;
}

int
MC::read_binary_from_client_event(int event, void *data)
{
  if (reader->read_avail() < (int)sizeof(binary_header))
    return EVENT_CONT;
  reader->memcpy(&binary_header, sizeof(binary_header));
  if (binary_header.request.magic != PROTOCOL_BINARY_REQ) {
    Warning("tsmemcache: bad binary magic: %x", binary_header.request.magic);
    return die();
  }
  int keylen = binary_header.request.keylen = ntohs(binary_header.request.keylen);
  int bodylen = binary_header.request.bodylen = ntohl(binary_header.request.bodylen);
  binary_header.request.cas                   = ink_ntoh64(binary_header.request.cas);
  int extlen                                  = binary_header.request.extlen;
  end_of_cmd                                  = sizeof(binary_header) + extlen;

#define CHECK_PROTOCOL(_e) \
  if (!(_e))               \
    return protocol_error();

  MCDebug("tsmemcache", "bin cmd %d\n", binary_header.request.opcode);
  switch (binary_header.request.opcode) {
  case PROTOCOL_BINARY_CMD_VERSION:
    CHECK_PROTOCOL(extlen == 0 && keylen == 0 && bodylen == 0);
    return write_to_client(write_binary_response(TSMEMCACHE_VERSION, 0, 0, STRLEN(TSMEMCACHE_VERSION)));
  case PROTOCOL_BINARY_CMD_NOOP:
    CHECK_PROTOCOL(extlen == 0 && keylen == 0 && bodylen == 0);
    return write_to_client(write_binary_response(NULL, 0, 0, 0));
  case PROTOCOL_BINARY_CMD_GETKQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_GETQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_GETK:
  case PROTOCOL_BINARY_CMD_GET:
    CHECK_PROTOCOL(extlen == 0 && (int)bodylen == keylen && keylen > 0);
    return TS_SET_CALL(&MC::binary_get_event, event, data);
  case PROTOCOL_BINARY_CMD_APPENDQ:
  case PROTOCOL_BINARY_CMD_APPEND:
    f.set_append = 1;
    goto Lset;
  case PROTOCOL_BINARY_CMD_PREPENDQ:
  case PROTOCOL_BINARY_CMD_PREPEND:
    f.set_prepend = 1;
    goto Lset;
  case PROTOCOL_BINARY_CMD_ADDQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_ADD:
    CHECK_PROTOCOL(extlen == 8 && keylen != 0 && bodylen >= keylen + 8);
    f.set_add = 1;
    goto Lset;
  case PROTOCOL_BINARY_CMD_REPLACEQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_REPLACE:
    CHECK_PROTOCOL(extlen == 8 && keylen != 0 && bodylen >= keylen + 8);
    f.set_replace = 1;
    goto Lset;
  case PROTOCOL_BINARY_CMD_SETQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_SET: {
    CHECK_PROTOCOL(extlen == 8 && keylen != 0 && bodylen >= keylen + 8);
  Lset:
    if (bin_read_key() < 0)
      return EVENT_CONT;
    key                              = binary_get_key(this);
    header.nkey                      = keylen;
    protocol_binary_request_set *req = (protocol_binary_request_set *)&binary_header;
    req->message.body.flags          = ntohl(req->message.body.flags);
    req->message.body.expiration     = ntohl(req->message.body.expiration);
    nbytes                           = bodylen - (header.nkey + extlen);
    break;
  }
  case PROTOCOL_BINARY_CMD_DELETEQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_DELETE:
    break;
  case PROTOCOL_BINARY_CMD_INCREMENTQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_INCREMENT:
    break;
  case PROTOCOL_BINARY_CMD_DECREMENTQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_DECREMENT:
    break;
  case PROTOCOL_BINARY_CMD_QUITQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_QUIT:
    if (f.noreply)
      return die();
    return write_then_close(write_binary_response(NULL, 0, 0, 0));
  case PROTOCOL_BINARY_CMD_FLUSHQ:
    f.noreply = 1; // fall through
  case PROTOCOL_BINARY_CMD_FLUSH:
    break;
    break;
  case PROTOCOL_BINARY_CMD_STAT:
    break;
  case PROTOCOL_BINARY_CMD_SASL_LIST_MECHS:
  case PROTOCOL_BINARY_CMD_SASL_AUTH:
  case PROTOCOL_BINARY_CMD_SASL_STEP:
    Warning("tsmemcache: sasl not (yet) supported");
    return die();
  case PROTOCOL_BINARY_CMD_RGET:
  case PROTOCOL_BINARY_CMD_RSET:
  case PROTOCOL_BINARY_CMD_RSETQ:
  case PROTOCOL_BINARY_CMD_RAPPEND:
  case PROTOCOL_BINARY_CMD_RAPPENDQ:
  case PROTOCOL_BINARY_CMD_RPREPEND:
  case PROTOCOL_BINARY_CMD_RPREPENDQ:
  case PROTOCOL_BINARY_CMD_RDELETE:
  case PROTOCOL_BINARY_CMD_RDELETEQ:
  case PROTOCOL_BINARY_CMD_RINCR:
  case PROTOCOL_BINARY_CMD_RINCRQ:
  case PROTOCOL_BINARY_CMD_RDECR:
  case PROTOCOL_BINARY_CMD_RDECRQ:
    Warning("tsmemcache: range not (yet) supported");
    return die();
  default:
    Warning("tsmemcache: unexpected binary opcode %x", binary_header.request.opcode);
    return die();
  }
  return EVENT_CONT;
}

int
MC::ascii_response(const char *s, int len)
{
  if (!f.noreply) {
    wbuf->write(s, len);
    wvio->nbytes = INT64_MAX;
    wvio->reenable();
    MCDebugBuf("tsmemcache_ascii_response", s, len);
  }
  if (end_of_cmd > 0) {
    reader->consume(end_of_cmd);
    return read_from_client();
  } else if (end_of_cmd < 0)
    return read_from_client();
  else
    return TS_SET_CALL(&MC::swallow_cmd_then_read_from_client_event, EVENT_NONE, NULL);
}

char *
MC::get_ascii_input(int n, int *end)
{
  int block_read_avail = reader->block_read_avail();
  if (block_read_avail >= n) {
  Lblock:
    *end = block_read_avail;
    return reader->start();
  }
  int read_avail = reader->read_avail();
  if (block_read_avail == read_avail)
    goto Lblock;
  char *c = tmp_cmd_buffer;
  int e   = read_avail;
  if (e > n)
    e = n;
  reader->memcpy(c, e);
  *end = e;
  return c;
}

int
MC::ascii_get_event(int event, void *data)
{
  switch (event) {
  case CACHE_EVENT_OPEN_READ_FAILED:
    reader->consume(read_offset);
    read_offset = 0;
    break;
  case CACHE_EVENT_OPEN_READ: {
    wbuf->WRITE("VALUE ");
    wbuf->write(key, header.nkey);
    wbuf->WRITE(" ");
    char t[32], *te = t + 32;
    char *flags = xutoa(rcache_header->flags, te);
    wbuf->write(flags, te - flags);
    wbuf->WRITE(" ");
    char *bytes = xutoa(rcache_header->nbytes, te);
    wbuf->write(bytes, te - bytes);
    if (f.return_cas) {
      wbuf->WRITE(" ");
      char *pcas = xutoa(rcache_header->cas, te);
      wbuf->write(pcas, te - pcas);
    }
    wbuf->WRITE("\r\n");
    int ntowrite = writer->read_avail() + rcache_header->nbytes;
    crvio        = crvc->do_io_read(this, rcache_header->nbytes, wbuf);
    creader      = reader;
    TS_PUSH_HANDLER(&MC::stream_event);
    return write_to_client(ntowrite);
  }
  case TSMEMCACHE_STREAM_DONE:
    crvc->do_io_close();
    crvc  = 0;
    crvio = NULL;
    reader->consume(read_offset);
    read_offset = 0;
    wbuf->WRITE("\r\n");
    return ascii_gets();
  default:
    break;
  }
  return ascii_gets();
}

int
MC::ascii_set_event(int event, void *data)
{
  switch (event) {
  case CACHE_EVENT_OPEN_WRITE_FAILED:
    // another write currently in progress
    mutex->thread_holding->schedule_in(this, TSMEMCACHE_RETRY_WRITE_INTERVAL);
    return EVENT_CONT;
  case EVENT_INTERVAL:
    return read_from_client();
  case CACHE_EVENT_OPEN_WRITE: {
    cwvc     = (CacheVConnection *)data;
    int hlen = 0;
    if (cwvc->get_header((void **)&wcache_header, &hlen) >= 0) {
      if (hlen < (int)sizeof(MCCacheHeader) || wcache_header->magic != TSMEMCACHE_HEADER_MAGIC)
        goto Lfail;
      if (header.nkey != wcache_header->nkey || hlen < (int)(sizeof(MCCacheHeader) + wcache_header->nkey))
        goto Lfail;
      ink_hrtime t = Thread::get_hrtime();
      if (((ink_hrtime)wcache_header->settime) <= last_flush ||
          t >= ((ink_hrtime)wcache_header->settime) + HRTIME_SECONDS(wcache_header->exptime))
        goto Lstale;
      if (f.set_add)
        return ASCII_RESPONSE("NOT_STORED");
    } else {
    Lstale:
      if (f.set_replace)
        return ASCII_RESPONSE("NOT_STORED");
    }
    memcpy(tmp_cache_header_key, key, header.nkey);
    header.settime = Thread::get_hrtime();
    if (exptime) {
      if (exptime > REALTIME_MAXDELTA) {
        if (HRTIME_SECONDS(exptime) <= ((ink_hrtime)header.settime))
          header.exptime = 0;
        else
          header.exptime = (int32_t)(exptime - (header.settime / HRTIME_SECOND));
      } else
        header.exptime = exptime;
    } else
      header.exptime = UINT32_MAX; // 136 years
    if (f.set_cas) {
      if (!wcache_header)
        return ASCII_RESPONSE("NOT_FOUND");
      if (header.cas && header.cas != wcache_header->cas)
        return ASCII_RESPONSE("EXISTS");
    }
    header.cas = ink_atomic_increment(&next_cas, 1);
    if (f.set_append || f.set_prepend)
      header.nbytes = nbytes + rcache_header->nbytes;
    else
      header.nbytes = nbytes;
    cwvc->set_header(&header, header.len());
    reader->consume(end_of_cmd);
    end_of_cmd    = -1;
    swallow_bytes = 2; // \r\n
    if (f.set_append) {
      TS_PUSH_HANDLER(&MC::tunnel_event);
      if (!cbuf)
        cbuf  = new_empty_MIOBuffer();
      creader = cbuf->alloc_reader();
      crvio   = crvc->do_io_read(this, rcache_header->nbytes, cbuf);
      cwvio   = cwvc->do_io_write(this, header.nbytes, creader);
    } else {
      if (f.set_prepend) {
        int64_t a = reader->read_avail();
        if (a >= (int64_t)nbytes)
          a = (int64_t)nbytes;
        if (!cbuf)
          cbuf  = new_empty_MIOBuffer();
        creader = cbuf->alloc_reader();
        if (a) {
          cbuf->write(reader, a);
          reader->consume(a);
        }
        if (a == (int64_t)nbytes) {
          cwvio = cwvc->do_io_write(this, header.nbytes, creader);
          goto Lstreamdone;
        }
        rvio->nbytes = rvio->ndone + (int64_t)nbytes - a;
      } else
        creader = reader;
      TS_PUSH_HANDLER(&MC::stream_event);
      cwvio = cwvc->do_io_write(this, header.nbytes, creader);
    }
    return EVENT_CONT;
  }
  case TSMEMCACHE_STREAM_DONE:
    rvio->nbytes = UINT64_MAX;
  Lstreamdone:
    if (f.set_prepend) {
      TS_PUSH_HANDLER(&MC::tunnel_event);
      crvio = crvc->do_io_read(this, rcache_header->nbytes, cbuf);
      return EVENT_CONT;
    }
    return ASCII_RESPONSE("STORED");
  case TSMEMCACHE_TUNNEL_DONE:
    crvc->do_io_close();
    crvc  = 0;
    crvio = NULL;
    if (f.set_append) {
      int64_t a = reader->read_avail();
      if (a > (int64_t)nbytes)
        a = (int64_t)nbytes;
      if (a) {
        cbuf->write(reader, a);
        reader->consume(a);
      }
      TS_PUSH_HANDLER(&MC::stream_event);
      return handleEvent(VC_EVENT_READ_READY, rvio);
    }
    ink_assert(f.set_prepend);
    cwvc->do_io_close();
    cwvc = 0;
    return ASCII_RESPONSE("STORED");
  case CACHE_EVENT_OPEN_READ_FAILED:
    swallow_bytes = nbytes + 2;
    return ASCII_RESPONSE("NOT_STORED");
  case CACHE_EVENT_OPEN_READ:
    crvc = (CacheVConnection *)data;
    return set_item();
  default:
    break;
  }
  return EVENT_CONT;
Lfail:
  Warning("tsmemcache: bad cache data");
  return ASCII_SERVER_ERROR("");
}

int
MC::ascii_delete_event(int event, void *data)
{
  switch (event) {
  case CACHE_EVENT_REMOVE_FAILED:
    return ASCII_RESPONSE("NOT_FOUND");
  case CACHE_EVENT_REMOVE:
    return ASCII_RESPONSE("DELETED");
  default:
    return EVENT_CONT;
  }
}

int
MC::ascii_incr_decr_event(int event, void *data)
{
  switch (event) {
  case CACHE_EVENT_OPEN_WRITE_FAILED:
    // another write currently in progress
    mutex->thread_holding->schedule_in(this, TSMEMCACHE_RETRY_WRITE_INTERVAL);
    return EVENT_CONT;
  case EVENT_INTERVAL:
    return read_from_client();
  case CACHE_EVENT_OPEN_WRITE: {
    int hlen = 0;
    cwvc     = (CacheVConnection *)data;
    {
      if (cwvc->get_header((void **)&wcache_header, &hlen) >= 0) {
        if (hlen < (int)sizeof(MCCacheHeader) || wcache_header->magic != TSMEMCACHE_HEADER_MAGIC)
          goto Lfail;
        if (header.nkey != wcache_header->nkey || hlen < (int)(sizeof(MCCacheHeader) + wcache_header->nkey))
          goto Lfail;
        ink_hrtime t = Thread::get_hrtime();
        if (((ink_hrtime)wcache_header->settime) <= last_flush ||
            t >= ((ink_hrtime)wcache_header->settime) + HRTIME_SECONDS(wcache_header->exptime))
          goto Lfail;
      } else
        goto Lfail;
      memcpy(tmp_cache_header_key, key, header.nkey);
      header.settime = Thread::get_hrtime();
      if (exptime) {
        if (exptime > REALTIME_MAXDELTA) {
          if (HRTIME_SECONDS(exptime) <= ((ink_hrtime)header.settime))
            header.exptime = 0;
          else
            header.exptime = (int32_t)(exptime - (header.settime / HRTIME_SECOND));
        } else
          header.exptime = exptime;
      } else
        header.exptime = UINT32_MAX; // 136 years
    }
    header.cas = ink_atomic_increment(&next_cas, 1);
    {
      char *data = 0;
      int len    = 0;
      // must be huge, why convert to a counter ??
      if (cwvc->get_single_data((void **)&data, &len) < 0)
        goto Lfail;
      uint64_t new_value = xatoull(data, data + len);
      if (f.set_incr)
        new_value += delta;
      else {
        if (delta > new_value)
          new_value = 0;
        else
          new_value -= delta;
      }
      char new_value_str_buffer[32], *e = &new_value_str_buffer[30];
      e[0]    = '\r';
      e[1]    = '\n';
      char *s = xutoa(new_value, e);
      creader = wbuf->clone_reader(writer);
      wbuf->write(s, e - s + 2);
      if (f.noreply)
        writer->consume(e - s + 2);
      else
        wvio->reenable();
      MCDebugBuf("tsmemcache_ascii_response", s, e - s + 2);
      header.nbytes = e - s;
      cwvc->set_header(&header, header.len());
      TS_PUSH_HANDLER(&MC::stream_event);
      cwvio = cwvc->do_io_write(this, header.nbytes, creader);
    }
    return EVENT_CONT;
  }
  case TSMEMCACHE_STREAM_DONE: {
    wbuf->dealloc_reader(creader);
    creader = 0;
    reader->consume(end_of_cmd);
    return read_from_client();
  }
  default:
    break;
  }
  return EVENT_CONT;
Lfail:
  Warning("tsmemcache: bad cache data");
  return ASCII_RESPONSE("NOT_FOUND");
}

int
MC::get_ascii_key(char *as, char *e)
{
  char *s = as;
  // skip space
  while (*s == ' ') {
    s++;
    if (s >= e) {
      if (as - e >= TSMEMCACHE_TMP_CMD_BUFFER_SIZE)
        return ASCII_CLIENT_ERROR("bad command line");
      return EVENT_CONT;
    }
  }
  // grab key
  key = s;
  while (!isspace(*s)) {
    if (s >= e) {
      if (as - e >= TSMEMCACHE_TMP_CMD_BUFFER_SIZE)
        return ASCII_RESPONSE("key too large");
      return EVENT_CONT;
    }
    s++;
  }
  if (s - key > TSMEMCACHE_MAX_KEY_LEN)
    return ASCII_CLIENT_ERROR("bad command line");
  header.nkey = s - key;
  if (!header.nkey) {
    if (e - s >= 2) {
      if (*s == '\r')
        s++;
      if (*s == '\n' && ngets)
        return ASCII_RESPONSE("END");
      return ASCII_CLIENT_ERROR("bad command line");
    }
    return EVENT_CONT; // get some more
  }
  read_offset = s - as;
  return TSMEMCACHE_EVENT_GOT_KEY;
}

int
MC::ascii_get(char *as, char *e)
{
  SET_HANDLER(&MC::ascii_get_event);
  CHECK_RET(get_ascii_key(as, e), TSMEMCACHE_EVENT_GOT_KEY);
  ngets++;
  return get_item();
}

int
MC::ascii_gets()
{
  int len = 0;
  char *c = get_ascii_input(TSMEMCACHE_TMP_CMD_BUFFER_SIZE, &len);
  return ascii_get(c, c + len);
}

#define SKIP_SPACE                                     \
  do {                                                 \
    while (*s == ' ') {                                \
      s++;                                             \
      if (s >= e)                                      \
        return ASCII_CLIENT_ERROR("bad command line"); \
    }                                                  \
  } while (0)

#define SKIP_TOKEN                                     \
  do {                                                 \
    while (!isspace(*s)) {                             \
      s++;                                             \
      if (s >= e)                                      \
        return ASCII_CLIENT_ERROR("bad command line"); \
    }                                                  \
  } while (0)

#define GET_NUM(_n)                                    \
  do {                                                 \
    if (isdigit(*s)) {                                 \
      _n = *s - '0';                                   \
      s++;                                             \
      if (s >= e)                                      \
        return ASCII_CLIENT_ERROR("bad command line"); \
    } else                                             \
      _n = 0;                                          \
    while (isdigit(*s)) {                              \
      _n *= 10;                                        \
      _n += *s - '0';                                  \
      s++;                                             \
      if (s >= e)                                      \
        return ASCII_CLIENT_ERROR("bad command line"); \
    }                                                  \
  } while (0)

#define GET_SNUM(_n)                                   \
  do {                                                 \
    int neg = 0;                                       \
    if (*s == '-') {                                   \
      s++;                                             \
      neg = 1;                                         \
    }                                                  \
    if (isdigit(*s)) {                                 \
      _n = *s - '0';                                   \
      s++;                                             \
      if (s >= e)                                      \
        return ASCII_CLIENT_ERROR("bad command line"); \
    } else                                             \
      _n = 0;                                          \
    while (isdigit(*s)) {                              \
      _n *= 10;                                        \
      _n += *s - '0';                                  \
      s++;                                             \
      if (s >= e)                                      \
        return ASCII_CLIENT_ERROR("bad command line"); \
    }                                                  \
    if (neg)                                           \
      _n = -_n;                                        \
  } while (0)

int
MC::ascii_set(char *s, char *e)
{
  SKIP_SPACE;
  key = s;
  SKIP_TOKEN;
  header.nkey = s - key;
  SKIP_SPACE;
  GET_NUM(header.flags);
  SKIP_SPACE;
  GET_SNUM(exptime);
  SKIP_SPACE;
  GET_NUM(nbytes);
  swallow_bytes = nbytes + 2; // assume failure
  if (f.set_cas) {
    SKIP_SPACE;
    GET_NUM(header.cas);
  } else
    header.cas = 0;
  SKIP_SPACE;
  if (*s == 'n' && !STRCMP_REST("oreply", s + 1, e)) {
    f.noreply = 1;
    s += 7;
    if (s >= e)
      return ASCII_CLIENT_ERROR("bad command line");
    SKIP_SPACE;
  }
  if (*s == '\r')
    s++;
  if (*s == '\n')
    s++;
  if (s != e)
    return ASCII_CLIENT_ERROR("bad command line");
  SET_HANDLER(&MC::ascii_set_event);
  if (f.set_append || f.set_prepend)
    return get_item();
  else
    return set_item();
}

int
MC::ascii_delete(char *s, char *e)
{
  SKIP_SPACE;
  key = s;
  SKIP_TOKEN;
  header.nkey = s - key;
  SKIP_SPACE;
  if (*s == 'n' && !STRCMP_REST("oreply", s + 1, e)) {
    f.noreply = 1;
    s += 7;
    if (s >= e)
      return ASCII_CLIENT_ERROR("bad command line");
    SKIP_SPACE;
  }
  if (*s == '\r')
    s++;
  if (*s == '\n')
    s++;
  if (s != e)
    return ASCII_CLIENT_ERROR("bad command line");
  SET_HANDLER(&MC::ascii_delete_event);
  return delete_item();
}

int
MC::ascii_incr_decr(char *s, char *e)
{
  SKIP_SPACE;
  key = s;
  SKIP_TOKEN;
  header.nkey = s - key;
  SKIP_SPACE;
  GET_NUM(delta);
  SKIP_SPACE;
  if (*s == 'n' && !STRCMP_REST("oreply", s + 1, e)) {
    f.noreply = 1;
    s += 7;
    if (s >= e)
      return ASCII_CLIENT_ERROR("bad command line");
    SKIP_SPACE;
  }
  if (*s == '\r')
    s++;
  if (*s == '\n')
    s++;
  if (s != e)
    return ASCII_CLIENT_ERROR("bad command line");
  SET_HANDLER(&MC::ascii_incr_decr_event);
  return set_item();
}

static int
is_end_of_cmd(char *t, char *e)
{
  while (*t == ' ' && t < e)
    t++; // skip spaces
  if (*t == '\r')
    t++;
  if (t != e - 1)
    return 0;
  return 1;
}

// moves *pt past the noreply if it is found
static int
is_noreply(char **pt, char *e)
{
  char *t = *pt;
  if (t < e - 8) {
    while (*t == ' ') {
      if (t > e - 8)
        return 0;
      t++;
    }
    if (t[0] == 'n' && !STRCMP(t + 1, "oreply") && isspace(t[7])) {
      *pt = t + sizeof("noreply") - 1;
      return 1;
    }
  }
  return 0;
}

int
MC::read_ascii_from_client_event(int event, void *data)
{
  int len = 0;
  char *c = get_ascii_input(TSMEMCACHE_TMP_CMD_BUFFER_SIZE, &len), *s = c;
  MCDebugBuf("tsmemcache_ascii_cmd", c, len);
  char *e = c + len - 5; // at least 6 chars
  while (*s == ' ' && s < e)
    s++; // skip leading spaces
  if (s >= e) {
    if (len >= TSMEMCACHE_TMP_CMD_BUFFER_SIZE || memchr(c, '\n', len))
      return ASCII_CLIENT_ERROR("bad command line");
    return EVENT_CONT;
  }
  // gets can be large, so do not require the full cmd fit in the buffer
  e = c + len;
  switch (*s) {
  case 'g': // get gets
    if (s[3] == 's' && s[4] == ' ') {
      f.return_cas = 1;
      read_offset  = 5;
      goto Lget;
    } else if (s[3] == ' ') {
      read_offset = 4;
    Lget:
      reader->consume(read_offset);
      if (c != tmp_cmd_buffer) // all in the block
        return ascii_get(s + read_offset, e);
      else
        return ascii_gets();
    }
    break;
  case 'b': // bget
    if (s[4] != ' ')
      break;
    read_offset = 5;
    goto Lget;
    break;
  default:
    break;
  }
  // find the end of the command
  e = (char *)memchr(s, '\n', len);
  if (!e) {
    if (reader->read_avail() > TSMEMCACHE_MAX_CMD_SIZE)
      return ASCII_CLIENT_ERROR("bad command line");
    return EVENT_CONT;
  }
  e++; // skip nl
  end_of_cmd = e - c;
  switch (*s) {
  case 's': // set stats
    if (s[1] == 'e' && s[2] == 't' && s[3] == ' ')
      return ascii_set(s + sizeof("set") - 1, e);
    if (STRCMP_REST("tats", s + 1, e))
      break;
    s += sizeof("stats") - 1;
    if (is_noreply(&s, e))
      break; // to please memcapable
    else
      return ASCII_RESPONSE("END");
  case 'a': // add
    if (s[1] == 'd' && s[2] == 'd' && s[3] == ' ') {
      f.set_add = 1;
      return ascii_set(s + sizeof("add") - 1, e);
    }
    if (STRCMP_REST("ppend", s + 1, e))
      break;
    f.set_append = 1;
    return ascii_set(s + sizeof("append") - 1, e);
  case 'p': // prepend
    if (STRCMP_REST("repend", s + 1, e))
      break;
    f.set_prepend = 1;
    return ascii_set(s + sizeof("prepend") - 1, e);
  case 'c': // cas
    if (s[1] == 'a' && s[2] == 's' && s[3] == ' ') {
      f.set_cas = 1;
      return ascii_set(s + sizeof("cas") - 1, e);
    }
    break;
  case 'i': // incr
    if (s[1] == 'n' && s[2] == 'c' && s[3] == 'r' && s[4] == ' ') {
      f.set_incr = 1;
      return ascii_incr_decr(s + sizeof("incr") - 1, e);
    }
    break;
  case 'f': { // flush_all
    if (STRCMP_REST("lush_all", s + 1, e))
      break;
    s += sizeof("flush_all") - 1;
    SKIP_SPACE;
    int32_t time_offset = 0;
    if (isdigit(*s))
      GET_NUM(time_offset);
    f.noreply                 = is_noreply(&s, e);
    ink_hrtime new_last_flush = Thread::get_hrtime() + HRTIME_SECONDS(time_offset);
#if __WORDSIZE == 64
    last_flush = new_last_flush; // this will be atomic for native word size
#else
    ink_atomic_swap(&last_flush, new_last_flush);
#endif
    if (!is_end_of_cmd(s, e))
      break;
    return ASCII_RESPONSE("OK");
  }
  case 'd': // delete decr
    if (e - s < 5)
      break;
    if (s[2] == 'l') {
      if (s[1] == 'e' && s[3] == 'e' && s[4] == 't' && s[5] == 'e' && s[6] == ' ')
        return ascii_delete(s + sizeof("delete") - 1, e);
    } else if (s[1] == 'e' && s[2] == 'c' && s[3] == 'r' && s[4] == ' ') { // decr
      f.set_decr = 1;
      return ascii_incr_decr(s + sizeof("decr") - 1, e);
    }
    break;
  case 'r': // replace
    if (STRCMP_REST("eplace", s + 1, e))
      break;
    f.set_replace = 1;
    return ascii_set(s + sizeof("replace") - 1, e);
  case 'q': // quit
    if (STRCMP_REST("uit", s + 1, e))
      break;
    if (!is_end_of_cmd(s + sizeof("quit") - 1, e))
      break;
    return die();
  case 'v': { // version
    if (s[3] == 's') {
      if (STRCMP_REST("ersion", s + 1, e))
        break;
      if (!is_end_of_cmd(s + sizeof("version") - 1, e))
        break;
      return ASCII_RESPONSE("VERSION " TSMEMCACHE_VERSION);
    } else if (s[3] == 'b') {
      if (STRCMP_REST("erbosity", s + 1, e))
        break;
      s += sizeof("verbosity") - 1;
      SKIP_SPACE;
      if (!isdigit(*s))
        break;
      GET_NUM(verbosity);
      f.noreply = is_noreply(&s, e);
      if (!is_end_of_cmd(s, e))
        break;
      return ASCII_RESPONSE("OK");
    }
    break;
  }
  }
  return ASCII_ERROR();
}

int
MC::write_then_close_event(int event, void *data)
{
  switch (event) {
  case VC_EVENT_EOS:
    if ((VIO *)data == wvio)
      break;
  // fall through
  case VC_EVENT_READ_READY:
    return EVENT_DONE; // no more of that stuff
  case VC_EVENT_WRITE_READY:
    if (wvio->buffer.reader()->read_avail() > 0)
      return EVENT_CONT;
    break;
  default:
    break;
  }
  return die();
}

int
MC::read_from_client_event(int event, void *data)
{
  switch (event) {
  case TSMEMCACHE_STREAM_DONE:
    return read_from_client();
  case VC_EVENT_READ_READY:
  case VC_EVENT_EOS:
    if (reader->read_avail() < 1)
      return EVENT_CONT;
    if ((uint8_t)reader->start()[0] == (uint8_t)PROTOCOL_BINARY_REQ)
      return TS_SET_CALL(&MC::read_binary_from_client_event, event, data);
    else
      return TS_SET_CALL(&MC::read_ascii_from_client_event, event, data);
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    break;
  default:
    return die();
  }
  return EVENT_CONT;
}

// between client and cache
int
MC::stream_event(int event, void *data)
{
  if (data == crvio || data == cwvio) {
    switch (event) {
    case VC_EVENT_READ_READY:
      wvio->reenable();
      break;
    case VC_EVENT_WRITE_READY:
      rvio->reenable();
      break;
    case VC_EVENT_WRITE_COMPLETE:
    case VC_EVENT_EOS:
    case VC_EVENT_READ_COMPLETE:
      return TS_POP_CALL(TSMEMCACHE_STREAM_DONE, 0);
    default:
      return die();
    }
  } else {
    switch (event) {
    case VC_EVENT_READ_READY:
      if (cwvio) {
        if (creader != reader && creader->read_avail() < cwvio->nbytes) {
          int64_t a = reader->read_avail();
          if (a > (int64_t)nbytes)
            a = (int64_t)nbytes;
          if (a) {
            cbuf->write(reader, a);
            reader->consume(a);
          }
        }
        cwvio->reenable();
      }
      break;
    case VC_EVENT_WRITE_READY:
      if (crvio)
        crvio->reenable();
      break;
    case VC_EVENT_WRITE_COMPLETE:
    case VC_EVENT_READ_COMPLETE:
      return TS_POP_CALL(TSMEMCACHE_STREAM_DONE, 0);
    default:
      return die();
    }
  }
  return EVENT_CONT;
}

// cache to cache
int
MC::tunnel_event(int event, void *data)
{
  MCDebug("tsmemcache", "tunnel %d %p crvio %p cwvio %p", event, data, crvio, cwvio);
  if (data == crvio) {
    switch (event) {
    case VC_EVENT_READ_READY:
      cwvio->reenable();
      break;
    case VC_EVENT_EOS:
    case VC_EVENT_READ_COMPLETE:
      if (cwvio->nbytes == cwvio->ndone + cwvio->buffer.reader()->read_avail()) {
        cwvio->reenable();
        return EVENT_CONT;
      }
      return TS_POP_CALL(TSMEMCACHE_TUNNEL_DONE, 0);
    default:
      return die();
    }
  } else if (data == cwvio) {
    switch (event) {
    case VC_EVENT_WRITE_READY:
      crvio->reenable();
      break;
    case VC_EVENT_WRITE_COMPLETE:
    case VC_EVENT_EOS:
      return TS_POP_CALL(TSMEMCACHE_TUNNEL_DONE, 0);
    default:
      return die();
    }
  } else { // network I/O
    switch (event) {
    case VC_EVENT_READ_READY:
    case VC_EVENT_WRITE_READY:
    case VC_EVENT_WRITE_COMPLETE:
    case VC_EVENT_READ_COMPLETE:
      return EVENT_CONT;
    default:
      return die();
    }
  }
  return EVENT_CONT;
}

int
init_tsmemcache(int port)
{
  tsmemcache_constants();
  MCAccept *a = new MCAccept;
  a->mutex    = new_ProxyMutex();
  NetProcessor::AcceptOptions options(NetProcessor::DEFAULT_ACCEPT_OPTIONS);
  options.local_port = a->accept_port = port;
  netProcessor.accept(a, options);
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  ink_assert(sizeof(protocol_binary_request_header) == 24);

  TSPluginRegistrationInfo info;
  info.plugin_name   = (char *)"tsmemcache";
  info.vendor_name   = (char *)"ats";
  info.support_email = (char *)"jplevyak@apache.org";

  int port = 11211;

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[PluginInit] tsmemcache registration failed.\n");
    goto error;
  }

  if (argc < 2) {
    TSError("[tsmemcache] Usage: tsmemcache.so [accept_port]\n");
    goto error;
  } else if (argc > 1) {
    int port = atoi(argv[1]);
    if (!port) {
      TSError("[tsmemcache] bad accept_port '%s'\n", argv[1]);
      goto error;
    }
    MCDebug("tsmemcache", "using accept_port %d", port);
  }
  init_tsmemcache(port);
  return;

error:
  TSError("[PluginInit] Plugin not initialized");
}
