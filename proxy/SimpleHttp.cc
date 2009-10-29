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

#include <limits.h>
#include <string.h>
#include "inktomi++.h"
#include "P_Cache.h"
#include "P_Net.h"
#include "P_HostDB.h"
#include "Config.h"
#include "Diags.h"
#include "HTTP.h"

#if COMPILE_SIMPLE_HTTP

#if 0
#define HISTORY_SIZE     128
#define HISTORY_DECL     int history_idx; int history[HISTORY_SIZE];
#define HISTORY_MARK()   history[history_idx++ % HISTORY_SIZE] = __LINE__
#else
#define HISTORY_SIZE     0
#define HISTORY_DECL
#define HISTORY_MARK()
#endif


static int enabled = 0;
static int port = 8888;
static int buffer_size = 32 * 1024;
static int buffer_size_idx = BUFFER_SIZE_INDEX_32K;


class AcceptCont:public Continuation
{
public:
  AcceptCont();

  int handle_event(int event, void *edata);
};


class SimpleCont:public Continuation
{
public:
  SimpleCont();

  static SimpleCont *create();
  void destroy();

  void start(NetVConnection * ua);
  void parse_ua_req(int eof);
  void cache_read(void);
  void cache_ua_tunnel(void);
  void dns_lookup(void);
  void os_connect(unsigned int addr);
  void os_write_req(void);
  void os_read_resp(void);
  void parse_os_resp(int eof);
  void ua_write_resp(void);
  void cache_write(void);
  void os_ua_tunnel(void);
  void ua_close(void);
  void cache_close(void);
  void cache_abort(void);

  int ua_read_req_event(int event, void *edata);
  int cache_read_event(int event, void *edata);
  int cache_ua_tunnel_event(int event, void *edata);
  int dns_event(int event, void *edata);
  int os_connect_event(int event, void *edata);
  int os_write_event(int event, void *edata);
  int os_read_resp_event(int event, void *edata);
  int cache_write_event(int event, void *edata);
  int os_ua_tunnel_event(int event, void *edata);

public:
    HTTPParser m_parser;
  Action *m_pending_action;

  NetVConnection *m_ua_vc;
  MIOBuffer *m_ua_read_buf;
  IOBufferReader *m_ua_reader;
  VIO *m_ua_read_vio;
  VIO *m_ua_write_vio;
  HTTPHdr m_ua_req;

  MIOBuffer *m_ua_write_buf;
  int m_ua_resp_size;

  const char *m_os_name;
  int m_os_port;
  NetVConnection *m_os_vc;
  MIOBuffer *m_os_write_buf;
  VIO *m_os_write_vio;
  VIO *m_os_read_vio;

  MIOBuffer *m_os_read_buf;
  IOBufferReader *m_os_reader;
  HTTPHdr m_os_resp;

  CacheKey m_key;
  VConnection *m_cache_read_vc;
  VIO *m_cache_read_vio;
  VConnection *m_cache_write_vc;
  VIO *m_cache_write_vio;

  HISTORY_DECL};


static ClassAllocator<SimpleCont> simpleContAllocator("simpleContAllocator");


AcceptCont::AcceptCont()
:Continuation(NULL)
{
  SET_HANDLER(&AcceptCont::handle_event);
}

int
AcceptCont::handle_event(int event, void *edata)
{
  if (event == NET_EVENT_ACCEPT) {
    SimpleCont *sm = SimpleCont::create();
    sm->start((NetVConnection *) edata);
  }
  return 0;
}


SimpleCont::SimpleCont()
:Continuation(NULL),
m_pending_action(NULL),
m_ua_vc(NULL),
m_ua_read_buf(NULL),
m_ua_reader(NULL),
m_ua_read_vio(NULL),
m_ua_write_vio(NULL),
m_ua_req(),
m_ua_write_buf(NULL),
m_ua_resp_size(0),
m_os_name(NULL),
m_os_port(0),
m_os_vc(NULL),
m_os_write_buf(NULL),
m_os_write_vio(NULL),
m_os_read_vio(NULL),
m_os_read_buf(NULL),
m_os_reader(NULL),
m_os_resp(), m_cache_read_vc(NULL), m_cache_read_vio(NULL), m_cache_write_vc(NULL), m_cache_write_vio(NULL)
{
}

SimpleCont *
SimpleCont::create()
{
  return simpleContAllocator.alloc();
}

void
SimpleCont::destroy()
{
  if (m_pending_action) {
    m_pending_action->cancel();
  }

  if (m_ua_vc) {
    m_ua_vc->do_io_close();
  }
  if (m_os_vc) {
    m_os_vc->do_io_close();
  }
  if (m_cache_read_vc) {
    m_cache_read_vc->do_io_close(1);
  }
  if (m_cache_write_vc) {
    m_cache_write_vc->do_io_close(1);
  }

  if (m_ua_read_buf) {
    free_MIOBuffer(m_ua_read_buf);
  }
  if (m_ua_write_buf) {
    free_MIOBuffer(m_ua_write_buf);
  }
  if (m_os_write_buf) {
    free_MIOBuffer(m_os_write_buf);
  }
  if (m_os_read_buf) {
    free_MIOBuffer(m_os_read_buf);
  }

  m_ua_req.destroy();
  m_os_resp.destroy();

  mutex = NULL;

  simpleContAllocator.free(this);
}

void
SimpleCont::start(NetVConnection * ua)
{
  mutex = this_ethread()->mutex;

  http_parser_init(&m_parser);

  m_ua_vc = ua;
  m_ua_read_buf = new_MIOBuffer(buffer_size_idx);
  m_ua_reader = m_ua_read_buf->alloc_reader();

  SET_HANDLER(&SimpleCont::ua_read_req_event);

  m_ua_read_vio = m_ua_vc->do_io_read(this, INT_MAX, m_ua_read_buf);
}

void
SimpleCont::parse_ua_req(int eof)
{
  const char *start, *p;
  const char *end;
  int avail;
  int err;

  for (;;) {
    avail = m_ua_reader->block_read_avail();
    if (!avail) {
      if (eof) {
        destroy();
      }
      return;
    }
    p = start = m_ua_reader->start();
    end = start + avail;

    err = m_ua_req.parse_req(&m_parser, &p, end, eof);

    m_ua_reader->consume(p - start);

    if (err == PARSE_DONE) {
      if (is_debug_tag_set("simple_http")) {
        m_ua_req.print(NULL, 0, NULL, NULL);
      }
      m_ua_read_vio->nbytes = m_ua_read_vio->ndone;
      cache_read();
      return;
    }

    if (err == PARSE_ERROR) {
      destroy();
      return;
    }
  }

  m_ua_read_vio->reenable();
}

void
SimpleCont::cache_read()
{
  URL url;
  INK_MD5 md5;
  Action *action;

  url = m_ua_req.url_get();
  url.MD5_get(&md5);
  m_key.set(md5);

  SET_HANDLER(&SimpleCont::cache_read_event);

  action = cacheProcessor.open_read(this, &m_key);
  if (action != ACTION_RESULT_DONE) {
    m_pending_action = action;
  }
}

void
SimpleCont::cache_ua_tunnel()
{
  int length;

  length = 0;
  m_cache_read_vc->get_data(CACHE_DATA_SIZE, &length);

  m_ua_write_buf = new_empty_MIOBuffer(buffer_size_idx);
  m_ua_reader = m_ua_write_buf->alloc_reader();

  SET_HANDLER(&SimpleCont::cache_ua_tunnel_event);

  Debug("simple_http", "cache-ua tunnel %d", m_ua_reader->read_avail());

  m_cache_read_vio = m_cache_read_vc->do_io_read(this, length, m_ua_write_buf);
  m_ua_write_vio = m_ua_vc->do_io_write(this, length, m_ua_reader);
}

void
SimpleCont::dns_lookup()
{
  URL url;
  Action *action;

  url = m_ua_req.url_get();
  m_os_name = url.host_get();
  m_os_port = url.port_get();

  SET_HANDLER(&SimpleCont::dns_event);

  action = hostDBProcessor.getbyname_re(this, (char *) m_os_name, 0, m_os_port);
  if (action != ACTION_RESULT_DONE) {
    m_pending_action = action;
  }
}

void
SimpleCont::os_connect(unsigned int addr)
{
  Action *action;

  SET_HANDLER(&SimpleCont::os_connect_event);

  action = netProcessor.connect_re(this, addr, m_os_port);
  if (action != ACTION_RESULT_DONE) {
    m_pending_action = action;
  }
}

void
SimpleCont::os_write_req()
{
  HTTPHdr req;
  URL url;
  IOBufferReader *reader;
  IOBufferBlock *blk;
  int bufindex;
  int tmp, dumpoffset;
  int done;

  req.create();
  req.copy(m_ua_req);

  url = req.url_get();
  url.scheme_set(NULL);
  url.host_set(NULL);

  m_os_write_buf = new_empty_MIOBuffer(buffer_size_idx);
  reader = m_os_write_buf->alloc_reader();

  dumpoffset = 0;
  do {
    blk = m_os_write_buf->get_current_block();
    if (!blk) {
      m_os_write_buf->add_block();
    }
    blk = m_os_write_buf->get_current_block();

    bufindex = 0;
    tmp = dumpoffset;

    done = req.print(blk->end(), blk->write_avail(), &bufindex, &tmp);

    dumpoffset += bufindex;
    m_os_write_buf->fill(bufindex);
  } while (!done);

  req.destroy();

  SET_HANDLER(&SimpleCont::os_write_event);

  m_os_write_vio = m_os_vc->do_io_write(this, reader->read_avail(), reader);
}

void
SimpleCont::os_read_resp()
{
  http_parser_init(&m_parser);

  m_os_read_buf = new_MIOBuffer(buffer_size_idx);
  m_os_reader = m_os_read_buf->alloc_reader();

  SET_HANDLER(&SimpleCont::os_read_resp_event);

  m_os_read_vio = m_os_vc->do_io_read(this, INT_MAX, m_os_read_buf);
}

void
SimpleCont::parse_os_resp(int eof)
{
  const char *start, *p;
  const char *end;
  int avail;
  int err;

  for (;;) {
    avail = m_os_reader->block_read_avail();
    if (!avail) {
      if (eof) {
        destroy();
      }
      return;
    }
    p = start = m_os_reader->start();
    end = start + avail;

    err = m_os_resp.parse_resp(&m_parser, &p, end, eof);

    m_os_reader->consume(p - start);

    if (err == PARSE_DONE) {
      if (is_debug_tag_set("simple_http")) {
        m_os_resp.print(NULL, 0, NULL, NULL);
      }
      m_os_read_vio->nbytes = m_os_read_vio->ndone;
      ua_write_resp();
      return;
    }

    if (err == PARSE_ERROR) {
      destroy();
      return;
    }
  }


  m_os_read_vio->reenable();
}

void
SimpleCont::ua_write_resp()
{
  IOBufferBlock *blk;
  int bufindex;
  int tmp, dumpoffset;
  int done;

  m_ua_write_buf = new_empty_MIOBuffer(buffer_size_idx);
  m_ua_reader = m_ua_write_buf->alloc_reader();

  dumpoffset = 0;
  do {
    blk = m_ua_write_buf->get_current_block();
    if (!blk) {
      m_ua_write_buf->add_block();
    }
    blk = m_ua_write_buf->get_current_block();

    bufindex = 0;
    tmp = dumpoffset;

    done = m_os_resp.print(blk->end(), blk->write_avail(), &bufindex, &tmp);

    dumpoffset += bufindex;
    m_ua_write_buf->fill(bufindex);
  } while (!done);

  m_ua_write_buf->write(m_os_reader);
  m_ua_resp_size = m_ua_reader->read_avail();

  cache_write();
}

void
SimpleCont::cache_write()
{
  Action *action;
  MIMEField field;
  int i, count;

  field = m_os_resp.field_retrieve(MIME_FIELD_CACHE_CONTROL);
  count = field.values_count();
  for (i = 0; i < count; i++) {
    if (field.value_get(i) == HTTP_VALUE_NO_CACHE) {
      os_ua_tunnel();
      return;
    }
  }

  SET_HANDLER(&SimpleCont::cache_write_event);

  action = cacheProcessor.open_write(this, 32 * 1024, &m_key, CACHE_FRAG_TYPE_HTTP);
  if (action != ACTION_RESULT_DONE) {
    m_pending_action = action;
  }
}

void
SimpleCont::os_ua_tunnel()
{
  SET_HANDLER(&SimpleCont::os_ua_tunnel_event);

  m_os_read_vio = m_os_vc->do_io_read(this, INT_MAX, m_ua_write_buf);
  if (m_cache_write_vc) {
    m_cache_write_vio = m_cache_write_vc->do_io_write(this, INT_MAX, m_ua_reader->clone());
  }
  m_ua_write_vio = m_ua_vc->do_io_write(this, INT_MAX, m_ua_reader);
}

void
SimpleCont::ua_close()
{
  m_ua_vc->do_io_close();
  m_ua_vc = NULL;
  m_ua_write_vio = NULL;

  if (!m_ua_vc && !m_cache_write_vc) {
    destroy();
  }
}

void
SimpleCont::cache_close()
{
  m_cache_write_vc->do_io_close();
  m_cache_write_vc = NULL;
  m_cache_write_vio = NULL;

  if (!m_ua_vc && !m_cache_write_vc) {
    destroy();
  }
}

void
SimpleCont::cache_abort()
{
  m_cache_write_vc->do_io_close(1);
  m_cache_write_vc = NULL;
  m_cache_write_vio = NULL;

  if (!m_ua_vc && !m_cache_write_vc) {
    destroy();
  }
}

int
SimpleCont::ua_read_req_event(int event, void *edata)
{
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS:
    HISTORY_MARK();
    Debug("simple_http", "read event %d", event);
    parse_ua_req((event == VC_EVENT_EOS) || (event == VC_EVENT_READ_COMPLETE));
    break;
  case VC_EVENT_ERROR:
  default:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}

int
SimpleCont::cache_read_event(int event, void *edata)
{
  switch (event) {
  case CACHE_EVENT_OPEN_READ:
    HISTORY_MARK();
    m_pending_action = NULL;
    Debug("simple_http", "cache read success");
    m_cache_read_vc = (VConnection *) edata;
    cache_ua_tunnel();
    break;
  case CACHE_EVENT_OPEN_READ_FAILED:
    HISTORY_MARK();
    m_pending_action = NULL;
    Debug("simple_http", "cache read failure");
    dns_lookup();
    break;
  default:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}

int
SimpleCont::cache_ua_tunnel_event(int event, void *edata)
{
  switch (event) {
  case VC_EVENT_READ_READY:
    HISTORY_MARK();
    Debug("simple_http", "cache read ready");
    m_ua_write_vio->reenable();
    break;
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS:
    Debug("simple_http", "cache read complete");
    m_ua_resp_size += m_cache_read_vio->ndone;
    m_cache_read_vc->do_io_close();
    m_cache_read_vc = NULL;
    m_cache_read_vio = NULL;

    m_ua_write_vio->nbytes = m_ua_resp_size;

    if (m_ua_write_vio->ndone == m_ua_write_vio->nbytes) {
      HISTORY_MARK();
      Debug("simple_http", "ua write complete");
      ua_close();
    } else {
      HISTORY_MARK();
      m_ua_write_vio->reenable();
    }
    break;
  case VC_EVENT_WRITE_READY:
    HISTORY_MARK();
    Debug("simple_http", "ua write ready");
    if (m_cache_read_vc) {
      m_cache_read_vio->reenable();
    }
    break;
  case VC_EVENT_WRITE_COMPLETE:
    HISTORY_MARK();
    Debug("simple_http", "ua write complete");
    ua_close();
    break;
  case VC_EVENT_ERROR:
  default:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}

int
SimpleCont::dns_event(int event, void *edata)
{
  switch (event) {
  case EVENT_HOST_DB_LOOKUP:
    m_pending_action = NULL;
    if (edata) {
      HostDBInfo *r = (HostDBInfo *) edata;
      HostDBInfo *rr = NULL;

      Debug("simple_http", "dns lookup success");

      if (r->round_robin) {
        Debug("simple_http", "dns round robin");
        rr = r->rr()->select_best(0);
      } else {
        rr = r;
      }

      if (rr) {
        HISTORY_MARK();
        os_connect(rr->ip());
      } else {
        HISTORY_MARK();
        Debug("simple_http", "dns error");
        destroy();
      }
    } else {
      HISTORY_MARK();
      Debug("simple_http", "dns lookup failure");
      destroy();
    }
    break;
  case EVENT_HOST_DB_IP_REMOVED:
    m_pending_action = NULL;
  default:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}

int
SimpleCont::os_connect_event(int event, void *edata)
{
  switch (event) {
  case NET_EVENT_OPEN:
    HISTORY_MARK();
    m_pending_action = NULL;
    Debug("simple_http", "os connect success");
    m_os_vc = (NetVConnection *) edata;
    os_write_req();
    break;
  case NET_EVENT_OPEN_FAILED:
    HISTORY_MARK();
    m_pending_action = NULL;
    Debug("simple_http", "os connect failure");
    destroy();
    break;
  default:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}

int
SimpleCont::os_write_event(int event, void *edata)
{
  switch (event) {
  case VC_EVENT_WRITE_READY:
    HISTORY_MARK();
    Debug("simple_http", "os write ready");
    m_os_write_vio->reenable();
    break;
  case VC_EVENT_WRITE_COMPLETE:
    HISTORY_MARK();
    Debug("simple_http", "os write complete");
    os_read_resp();
    break;
  case VC_EVENT_ERROR:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}

int
SimpleCont::os_read_resp_event(int event, void *edata)
{
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS:
    HISTORY_MARK();
    Debug("simple_http", "read event %d", event);
    parse_os_resp((event == VC_EVENT_EOS) || (event == VC_EVENT_READ_COMPLETE));
    break;
  case VC_EVENT_ERROR:
  default:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}

int
SimpleCont::cache_write_event(int event, void *edata)
{
  switch (event) {
  case CACHE_EVENT_OPEN_WRITE:
    HISTORY_MARK();
    Debug("simple_http", "cache write success");
    m_cache_write_vc = (VConnection *) edata;
    os_ua_tunnel();
    break;
  case CACHE_EVENT_OPEN_WRITE_FAILED:
    HISTORY_MARK();
    Debug("simple_http", "cache write failure");
    os_ua_tunnel();
    break;
  default:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}

int
SimpleCont::os_ua_tunnel_event(int event, void *edata)
{
  switch (event) {
  case VC_EVENT_READ_READY:
    HISTORY_MARK();
    Debug("simple_http", "os read ready");
    m_ua_write_vio->reenable();
    if (m_cache_write_vio) {
      m_cache_write_vio->reenable();
    }
    break;
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS:
    HISTORY_MARK();
    Debug("simple_http", "os read complete");
    m_ua_resp_size += m_os_read_vio->ndone;
    m_os_vc->do_io_close();
    m_os_vc = NULL;
    m_os_read_vio = NULL;

    if (m_cache_write_vio) {
      m_cache_write_vio->nbytes = m_ua_resp_size;
      if (m_cache_write_vio->ndone == m_cache_write_vio->nbytes) {
        Debug("simple_http", "cache write complete");
        cache_close();
      } else {
        m_cache_write_vio->reenable();
      }
    }

    m_ua_write_vio->nbytes = m_ua_resp_size;
    if (m_ua_write_vio->ndone == m_ua_write_vio->nbytes) {
      Debug("simple_http", "ua write complete");
      ua_close();
    } else {
      m_ua_write_vio->reenable();
    }
    break;
  case VC_EVENT_WRITE_READY:
    if ((VIO *) edata == m_ua_write_vio) {
      HISTORY_MARK();
      Debug("simple_http", "ua write ready");
    } else if ((VIO *) edata == m_cache_write_vio) {
      HISTORY_MARK();
      Debug("simple_http", "cache write ready");
    }

    if (m_os_vc) {
      m_os_read_vio->reenable();
    }
    break;
  case VC_EVENT_WRITE_COMPLETE:
    if ((VIO *) edata == m_ua_write_vio) {
      HISTORY_MARK();
      Debug("simple_http", "ua write complete");
      ua_close();
    } else if ((VIO *) edata == m_cache_write_vio) {
      HISTORY_MARK();
      Debug("simple_http", "cache write complete");
      cache_close();
    }
    break;
  case VC_EVENT_ERROR:
    if ((VIO *) edata == m_cache_write_vio) {
      HISTORY_MARK();
      Debug("simple_http", "cache error");
      cache_abort();
      break;
    }
  default:
    HISTORY_MARK();
    Debug("simple_http", "unexpected event %d", event);
    destroy();
    break;
  }
  return 0;
}


void
run_SimpleHttp()
{
  if (is_action_tag_set("simple_http")) {

    RecReadConfigInteger(port, "proxy.config.simple.http.port");
    RecReadConfigInteger(buffer_size, "proxy.config.simple.http.buffer_size");
    buffer_size_idx = buffer_size_to_index(buffer_size);

    Note("simple http running on port %d", port);
    netProcessor.main_accept(NEW(new AcceptCont()), NO_FD, port);
  }

  return;
}

#else

void
run_SimpleHttp()
{
  if (is_action_tag_set("simple_http")) {
    Error("simple http not implemented for new headers");
  }
  return;
}

#endif
