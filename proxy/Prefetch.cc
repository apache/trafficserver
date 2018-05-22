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

#include "Prefetch.h"
#include "HdrUtils.h"
#include "HttpCompat.h"
#include <records/I_RecHttp.h>
#include <ts/IpMapConf.h>

#ifdef PREFETCH

struct html_tag prefetch_allowable_html_tags[] = {
  // All embedded objects (fetched by the browser without requiring a click)
  // should be here
  //{ "a", "href"},   /* NOT USED */
  {"img", "src"},
  {"body", "background"},
  {"frame", "src"},
  {"fig", "src"},
  {"applet", "code"},
  {"script", "src"},
  {"embed", "src"},
  {"td", "background"},
  {"base", "href"},    // special handling
  {"meta", "content"}, // special handling
  //{ "area", "href"},  //used for testing parser
  {"input", "src"},
  {"link", "href"},
  {NULL, NULL}};

// this attribute table is hard coded. It has to be the same size as
// the prefetch_allowable_html_tags table
struct html_tag prefetch_allowable_html_attrs[] = {{NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   {"rel", "stylesheet"}, // We want to prefetch the .css files that are
                                                                          // common; make sure this matches {"link", "href"}
                                                   {NULL, NULL}};

static const char *PREFETCH_FIELD_RECURSION;
static int PREFETCH_FIELD_LEN_RECURSION;

PrefetchProcessor prefetchProcessor;
KeepAliveConnTable *g_conn_table;

static int prefetch_udp_fd = 0;
static int32_t udp_seq_no;

TSPrefetchBlastData const UDP_BLAST_DATA = {TS_PREFETCH_UDP_BLAST};
TSPrefetchBlastData const TCP_BLAST_DATA = {TS_PREFETCH_TCP_BLAST};

#define PrefetchEstablishStaticConfigStringAlloc(_ix, _n) \
  REC_EstablishStaticConfigStringAlloc(_ix, _n);          \
  REC_RegisterConfigUpdateFunc(_n, prefetch_config_cb, NULL)

#define PrefetchEstablishStaticConfigLongLong(_ix, _n) \
  REC_EstablishStaticConfigInteger(_ix, _n);           \
  REC_RegisterConfigUpdateFunc(_n, prefetch_config_cb, NULL)

#define PrefetchEstablishStaticConfigFloat(_ix, _n) \
  REC_EstablishStaticConfigFloat(_ix, _n);          \
  REC_RegisterConfigUpdateFunc(_n, prefetch_config_cb, NULL)

#define PrefetchEstablishStaticConfigByte(_ix, _n) \
  REC_EstablishStaticConfigByte(_ix, _n);          \
  REC_RegisterConfigUpdateFunc(_n, prefetch_config_cb, NULL)

static inline uint32_t
get_udp_seq_no()
{
  return ink_atomic_increment(&udp_seq_no, 1);
}

static inline void
setup_udp_header(char *header, uint32_t seq_no, uint32_t pkt_no, bool last_pkt)
{
  uint32_t *hdr = (uint32_t *)header;
  hdr[0]        = 0;
  hdr[1]        = htonl(seq_no);
  hdr[2]        = htonl((last_pkt ? PRELOAD_UDP_LAST_PKT_FLAG : 0) | (pkt_no & PRELOAD_UDP_PKT_NUM_MASK));
}

static inline void
setup_object_header(char *header, int64_t size, bool url_promise)
{
  uint32_t *hdr = (uint32_t *)header;
  hdr[0]        = htonl(size);
  hdr[1]        = 0; // we are not pinning
  hdr[2]        = (url_promise) ? htonl(PRELOAD_HDR_URL_PROMISE_FLAG) : 0;
}

// Raghu's info about domain extraction
inline const char *
findDomainFromHost(const char *host, int host_len, bool &no_dot)
{
  const char *h_cur = host + host_len - 1;

  if (host_len > 4) {
    // checking for .com .edu .net .org .gov .mil .int
    h_cur = host + host_len - 4;
    if (*h_cur == '.') {
      // convert to lower case
      char c3 = *(h_cur + 1);
      char c1 = (c3 >= 'A' && c3 <= 'Z') ? (c3 + 'a' - 'A') : c3;
      c3      = *(h_cur + 2);
      char c2 = (c3 >= 'A' && c3 <= 'Z') ? (c3 + 'a' - 'A') : c3;
      c3      = *(h_cur + 3);
      if (c3 >= 'A' && c3 <= 'Z')
        c3 += 'a' - 'A';

      // there is a high posibility that the postfix is one of the seven
      if ((c1 == 'c' && c2 == 'o' && c3 == 'm') || (c1 == 'e' && c2 == 'd' && c3 == 'u') || (c1 == 'n' && c2 == 'e' && c3 == 't') ||
          (c1 == 'o' && c2 == 'r' && c3 == 'g') || (c1 == 'g' && c2 == 'o' && c3 == 'v') || (c1 == 'm' && c2 == 'i' && c3 == 'l') ||
          (c1 == 'i' && c2 == 'n' && c3 == 't')) {
        h_cur--;

        while (h_cur != host) {
          if (*h_cur == '.')
            break;
          h_cur--;
        }
        if (h_cur != host) {
          // found a '.'
          h_cur++;
        } else if (*h_cur == '.')
          return NULL;

        return h_cur;
      }
    }
  }
  // for non-top level domains, require the first char is not '.' and
  // two '.' minimum, e.g. abc.va.us
  int num_dots = 0;
  while (h_cur != host) {
    if (*h_cur == '.') {
      num_dots++;
      if (num_dots == 3) {
        h_cur++;
        return h_cur;
      }
    }
    h_cur--;
  }

  if (num_dots < 2 || *host == '.') {
    if (num_dots == 0)
      no_dot = true;
    return NULL;
  } else
    return h_cur;
}

static int
normalize_url(char *url, int *len)
{
  /* returns > 0 if the url is modified */

  char *p, *root, *end = url + len[0];
  int modified = 0; // most of the time we don't modify the url.

  enum {
    NONE,
    FIRST_DOT,
    SECOND_DOT,
    SLASH,
  } state;

  if (!(p = strstr(url, "://")))
    return -1;
  p += 3;

  // get to the first slash:
  root = (p = strchr(p, '/'));

  if (!root)
    return 0;

  state = SLASH;

  while (++p <= end) {
    switch (p[0]) {
    case '\0':
    case '/':
      switch (state) {
      case SLASH: // "//" => "/"
        if (p[0]) {
          modified = 1;
          p[0]     = 0;
        }
        break;

      case FIRST_DOT: // "/./" => "/"
        modified = 1;
        p[0]     = (p[-1] = 0);
        break;

      case SECOND_DOT: { // "/dir/../" or "/../" => "/"
        modified = 1;
        p[0]     = (p[-1] = (p[-2] = 0));

        char *dir = p - 3;
        while (dir[0] == 0 && dir > root)
          dir--;

        ink_assert(dir[0] == '/');
        if (dir > root && dir[0] == '/') {
          do {
            dir[0] = 0;
          } while (*--dir != '/');
        }
      } break;
      default: /* NONE */
               ;
      }; /* end of switch (state) */

      state = SLASH;
      break;

    case '.':
      switch (state) {
      case SLASH:
        state = FIRST_DOT;
        break;
      case FIRST_DOT:
        state = SECOND_DOT;
        break;
      default:
        state = NONE;
      }
      break;

    default:
      state = NONE;
    }
  }

  if (modified) {
    // ok, now remove all the 0s in between
    p = ++root;

    while (p < end) {
      if (p[0]) {
        *root++ = p[0];
      } else
        len[0]--;
      p++;
    }
    *root = 0;
    return 1;
  }

  return 0;
}

static PrefetchConfiguration *prefetch_config;
ClassAllocator<PrefetchUrlEntry> prefetchUrlEntryAllocator("prefetchUrlEntryAllocator");

#define IS_STATUS_REDIRECT(status)                                                                \
  (prefetch_config->redirection > 0 &&                                                            \
   (((status) == HTTP_STATUS_MOVED_PERMANENTLY) || ((status) == HTTP_STATUS_MOVED_TEMPORARILY) || \
    ((status) == HTTP_STATUS_SEE_OTHER) || (((status) == HTTP_STATUS_TEMPORARY_REDIRECT))))

struct PrefetchConfigCont;
typedef int (PrefetchConfigCont::*PrefetchConfigContHandler)(int, void *);
struct PrefetchConfigCont : public Continuation {
public:
  PrefetchConfigCont(ProxyMutex *m) : Continuation(m)
  {
    SET_HANDLER((PrefetchConfigContHandler)&PrefetchConfigCont::conf_update_handler);
  }
  int conf_update_handler(int event, void *edata);
};

static Ptr<ProxyMutex> prefetch_reconfig_mutex;

/** Used to free old PrefetchConfiguration data. */
struct PrefetchConfigFreerCont;
typedef int (PrefetchConfigFreerCont::*PrefetchConfigFreerContHandler)(int, void *);

struct PrefetchConfigFreerCont : public Continuation {
  PrefetchConfiguration *p;
  int
  freeEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    Debug("Prefetch", "Deleting old Prefetch config after change");
    delete p;
    delete this;
    return EVENT_DONE;
  }
  PrefetchConfigFreerCont(PrefetchConfiguration *ap) : Continuation(new_ProxyMutex()), p(ap)
  {
    SET_HANDLER((PrefetchConfigFreerContHandler)&PrefetchConfigFreerCont::freeEvent);
  }
};

int
PrefetchConfigCont::conf_update_handler(int /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  Debug("Prefetch", "Handling Prefetch config change");

  PrefetchConfiguration *new_prefetch_config = new PrefetchConfiguration;
  if (new_prefetch_config->readConfiguration() == 0) {
    // switch the prefetch_config
    eventProcessor.schedule_in(new PrefetchConfigFreerCont(prefetch_config), PREFETCH_CONFIG_UPDATE_TIMEOUT, ET_TASK);
    ink_atomic_swap(&prefetch_config, new_prefetch_config);
  } else {
    // new config construct error, we should not use the new config
    Debug("Prefetch", "New config in ERROR, keeping the old config");
    eventProcessor.schedule_in(new PrefetchConfigFreerCont(new_prefetch_config), PREFETCH_CONFIG_UPDATE_TIMEOUT, ET_TASK);
  }

  delete this;
  return EVENT_DONE;
}

static int
prefetch_config_cb(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData /* data ATS_UNUSED */,
                   void * /* cookie ATS_UNUSED */)
{
  INK_MEMORY_BARRIER;

  eventProcessor.schedule_in(new PrefetchConfigCont(prefetch_reconfig_mutex), HRTIME_SECONDS(1), ET_TASK);
  return 0;
}

PrefetchTransform::PrefetchTransform(HttpSM *sm, HTTPHdr *resp)
  : INKVConnInternal(NULL, reinterpret_cast<TSMutex>((ProxyMutex *)sm->mutex)), m_output_buf(NULL), m_output_vio(NULL), m_sm(sm)
{
  refcount_inc();

  HTTPHdr *request = &sm->t_state.hdr_info.client_request;
  url              = request->url_get()->string_get(NULL, NULL);

  html_parser.Init(url, prefetch_config->html_tags_table, prefetch_config->html_attrs_table);

  SET_HANDLER(&PrefetchTransform::handle_event);

  Debug("PrefetchParser", "Created: transform for %s", url);

  memset(&hash_table[0], 0, HASH_TABLE_LENGTH * sizeof(hash_table[0]));

  udp_url_list = blasterUrlListAllocator.alloc();
  udp_url_list->init(UDP_BLAST_DATA, prefetch_config->url_buffer_timeout, prefetch_config->url_buffer_size);
  tcp_url_list = blasterUrlListAllocator.alloc();
  tcp_url_list->init(TCP_BLAST_DATA, prefetch_config->url_buffer_timeout, prefetch_config->url_buffer_size);

  // extract domain
  host_start = request->url_get()->host_get(&host_len);

  if (!host_start || !host_len)
    host_start = request->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_len);

  no_dot_in_host = false;
  if (host_start && host_len) {
    domain_end   = host_start + (host_len - 1);
    domain_start = findDomainFromHost(host_start, host_len, no_dot_in_host);
  } else
    domain_start = 0;

  // Check for redirection and redirect get the redirect URL before parsing the
  // body of the redirect.
  redirect(resp);
}

PrefetchTransform::~PrefetchTransform()
{
  // inform the lists that there no more urls left.
  this_ethread()->schedule_imm_local(udp_url_list);
  this_ethread()->schedule_imm_local(tcp_url_list);

  Debug("PrefetchParserURLs", "Unique URLs 0x%p (%s):", this, url);
  int nurls = 0;
  for (int i = 0; i < HASH_TABLE_LENGTH; i++) {
    PrefetchUrlEntry *e = hash_table[i];
    while (e) {
      Debug("PrefetchParserURLs", "(0x%p) %d: %s", this, i, e->url);
      nurls++;
      PrefetchUrlEntry *next = e->hash_link;
      e->free();
      e = next;
    }
  }

  Debug("PrefetchParserURLs", "Number of embedded objects extracted for %s: %d", url, nurls);

  if (m_output_buf)
    free_MIOBuffer(m_output_buf);
  ats_free(url);
}

int
PrefetchTransform::handle_event(int event, void *edata)
{
  handle_event_count(event);

  if (m_closed) {
    if (m_deletable) {
      Debug("PrefetchParser", "PrefetchTransform free(): %" PRId64 "", m_output_vio ? m_output_vio->ndone : 0);
      if (m_output_buf) {
        free_MIOBuffer(m_output_buf);
        m_output_buf = 0;
      }
      Debug("Prefetch", "Freeing after closed %p", this);
      free();
    }
  } else {
    switch (event) {
    case VC_EVENT_ERROR:
      m_write_vio.cont->handleEvent(VC_EVENT_ERROR, &m_write_vio);
      break;

    case VC_EVENT_WRITE_COMPLETE:

      Debug("Prefetch", "got write_complete %p", this);
      ink_assert(m_output_vio == (VIO *)edata);

      ink_assert(m_write_vio.ntodo() == 0);

      m_output_vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
      break;

    case VC_EVENT_WRITE_READY:
    default: {
      if (!m_output_vio) {
        m_output_buf    = new_empty_MIOBuffer();
        m_output_reader = m_output_buf->alloc_reader();
        m_output_vio    = m_output_vc->do_io_write(this, INT64_MAX, m_output_reader);
      }
      // If the write vio is null, it means it doesn't want
      // to get anymore event (WRITE_READY or WRITE_COMPLETE)
      // It also means we're done reading
      if (m_write_vio.op == VIO::NONE) {
        m_output_vio->nbytes = m_write_vio.ndone;
        m_output_vio->reenable();
        return 0;
      }

      ink_assert(m_output_vc != nullptr);

      MUTEX_TRY_LOCK(trylock, m_write_vio.mutex, this_ethread());
      if (!trylock.is_locked()) {
        retry(10);
        return 0;
      }

      if (m_closed) {
        return 0;
      }

      int64_t towrite = m_write_vio.ntodo();

      if (towrite > 0) {
        IOBufferReader *buf_reader = m_write_vio.get_reader();
        int64_t avail              = buf_reader->read_avail();

        if (towrite > avail) {
          towrite = avail;
        }

        if (towrite > 0) {
          Debug("PrefetchParser",
                "handle_event() "
                "writing %" PRId64 " bytes to output",
                towrite);

          // Debug("PrefetchParser", "Read avail before = %d", avail);

          m_output_buf->write(buf_reader, towrite);

          parse_data(buf_reader);

          // buf_reader->consume (towrite);
          m_write_vio.ndone += towrite;
        }
      }

      if (m_write_vio.ntodo() > 0) {
        if (towrite > 0) {
          m_output_vio->reenable();
          m_write_vio.cont->handleEvent(VC_EVENT_WRITE_READY, &m_write_vio);
        }
      } else {
        m_output_vio->nbytes = m_write_vio.ndone;
        m_output_vio->reenable();
        m_write_vio.cont->handleEvent(VC_EVENT_WRITE_COMPLETE, &m_write_vio);
      }

      break;
    }
    }
  }
  return 0;
}

int
PrefetchTransform::redirect(HTTPHdr *resp)
{
  HTTPHdr *req        = NULL;
  int response_status = 0;
  char *req_url       = NULL;
  char *redirect_url  = NULL;

  /* Check for responses validity. If the response is valid, determine the status of the response.
     We need to find out if there was a redirection (301, 302, 303, 307).
   */
  if ((resp != NULL) && (resp->valid())) {
    response_status = resp->status_get();

    /* OK, so we got the response. Now if the response is a redirect we have to check if we also
       got a Location: header. This indicates the new location where our object is located.
       If refirect_url was not found, letz falter back to just a recursion. Since
       we might find the url in the body.
     */
    if (resp->presence(MIME_PRESENCE_LOCATION)) {
      int redirect_url_len = 0;
      const char *tmp_url  = resp->value_get(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, &redirect_url_len);

      redirect_url = (char *)alloca(redirect_url_len + 1);
      ink_strlcpy(redirect_url, tmp_url, redirect_url_len + 1);
      Debug("PrefetchTransform", "redirect_url = %s", redirect_url);
    } else {
      response_status = -1;
    }
  } else {
    response_status = -1;
  }

  if (IS_STATUS_REDIRECT(response_status)) {
    if (redirect_url) {
      req     = &m_sm->t_state.hdr_info.client_request;
      req_url = req->url_get()->string_get(NULL, NULL);

      Debug("PrefetchTransform", "Received response status = %d", response_status);
      Debug("PrefetchTransform", "Redirect from request = %s", req_url);

      int location_len = strlen(redirect_url);
      Debug("PrefetchTransform", "Redirect url to HTTP Hdr Location: \'%s\'", redirect_url);
      if (strncmp(redirect_url, req_url, location_len) == 0) {
        Debug("PrefetchTransform", "'%s' -> '%s' - Could be a loop. Discontinuing this path.", req_url, redirect_url);
        ats_free(req_url);
        return 0;
      }

      PrefetchUrlEntry *entry = hash_add(redirect_url);

      if (!entry) {
        Debug("PrefetchParserURLs", "Ignoring duplicate url '%s'", redirect_url);
        ats_free(req_url);
        return 0;
      }

      Debug("PrefetchTransform", "Found embedded URL: %s", redirect_url);
      entry->req_ip = m_sm->t_state.client_info.src_addr;

      PrefetchBlaster *blaster = prefetchBlasterAllocator.alloc();
      blaster->init(entry, &m_sm->t_state.hdr_info.client_request, this);
      ats_free(req_url);
    }
  }
  return 0;
}

int
PrefetchTransform::parse_data(IOBufferReader *reader)
{
  char *url_start = NULL, *url_end = NULL;

  while (html_parser.ParseHtml(reader, &url_start, &url_end)) {
    PrefetchUrlEntry *entry = hash_add(url_start);

    if (!entry) {
      // Debug("PrefetchParserURLs", "Duplicate URL: %s", url_start);
      continue;
    }
    // Debug("PrefetchParserURLs", "Found embedded URL: %s", url_start);
    ats_ip_copy(&entry->req_ip, &m_sm->t_state.client_info.src_addr);

    PrefetchBlaster *blaster = prefetchBlasterAllocator.alloc();
    blaster->init(entry, &m_sm->t_state.hdr_info.client_request, this);
  }

  return 0;
}

PrefetchUrlEntry *
PrefetchTransform::hash_add(char *s)
{
  uint32_t index = 0;
  int str_len    = strlen(s);

  if (normalize_url(s, &str_len) > 0)
    Debug("PrefetchParserURLs", "Normalized URL: %s", s);

  INK_MD5 hash;
  MD5Context().hash_immediate(hash, s, str_len);
  index = hash.slice32(1) % HASH_TABLE_LENGTH;

  PrefetchUrlEntry **e = &hash_table[index];
  for (; *e; e = &(*e)->hash_link)
    if (strcmp((*e)->url, s) == 0)
      return NULL;

  *e = prefetchUrlEntryAllocator.alloc();
  (*e)->init(ats_strdup(s), hash);

  return *e;
}

#define IS_RECURSIVE_PREFETCH(req_ip) (prefetch_config->max_recursion > 0 && ats_is_ip_loopback(&req_ip))

static void
check_n_attach_prefetch_transform(HttpSM *sm, HTTPHdr *resp, bool from_cache)
{
  INKVConnInternal *prefetch_trans;
  ip_text_buffer client_ipb;

  IpEndpoint client_ip = sm->t_state.client_info.src_addr;

  // we depend on this to setup @a client_ipb for all subsequent Debug().
  Debug("PrefetchParser", "Checking response for request from %s", ats_ip_ntop(&client_ip, client_ipb, sizeof(client_ipb)));

  unsigned int rec_depth = 0;
  HTTPHdr *request       = &sm->t_state.hdr_info.client_request;

  if (IS_RECURSIVE_PREFETCH(client_ip)) {
    rec_depth = request->value_get_int(PREFETCH_FIELD_RECURSION, PREFETCH_FIELD_LEN_RECURSION);
    rec_depth++;

    Debug("PrefetchTemp", "recursion: %d", rec_depth);

    if (rec_depth > prefetch_config->max_recursion) {
      Debug("PrefetchParserRecursion",
            "Recursive parsing is not done "
            "since recursion depth(%d) is greater than max allowed (%d)",
            rec_depth, prefetch_config->max_recursion);
      return;
    }
  } else if (!prefetch_config->ip_map.contains(&client_ip)) {
    Debug("PrefetchParser",
          "client (%s) does not match any of the "
          "prefetch_children mentioned in configuration\n",
          client_ipb);
    return;
  }

  if (prefetch_config->max_recursion > 0) {
    request->value_set_int(PREFETCH_FIELD_RECURSION, PREFETCH_FIELD_LEN_RECURSION, rec_depth);
  }

  int c_type_len;
  const char *c_type = resp->value_get(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, &c_type_len);

  if ((c_type == NULL) || strncmp("text/html", c_type, 9) != 0) {
    Debug("PrefetchParserCT", "Content type is not text/html.. skipping");
    return;
  }

  /* skip if it is encoded */
  c_type = resp->value_get(MIME_FIELD_CONTENT_ENCODING, MIME_LEN_CONTENT_ENCODING, &c_type_len);
  if (c_type) {
    char type[64];
    memcpy(type, c_type, c_type_len);
    type[c_type_len] = 0;
    Debug("PrefetchParserCT", "Content is encoded with %s .. skipping", type);
    return;
  }

  Debug("PrefetchParserCT", "Content type is text/html");

  if (prefetch_config->pre_parse_hook) {
    TSPrefetchInfo info;

    HTTPHdr *req      = &sm->t_state.hdr_info.client_request;
    info.request_buf  = reinterpret_cast<TSMBuffer>(req);
    info.request_loc  = reinterpret_cast<TSMLoc>(req->m_http);
    info.response_buf = reinterpret_cast<TSMBuffer>(resp);
    info.response_loc = reinterpret_cast<TSMLoc>(resp->m_http);

    ats_ip_copy(ats_ip_sa_cast(&info.client_ip), &client_ip);
    info.embedded_url     = 0;
    info.present_in_cache = from_cache;
    ink_zero(info.url_blast);
    ink_zero(info.url_response_blast);

    info.object_buf        = 0;
    info.object_buf_reader = 0;
    info.object_buf_status = TS_PREFETCH_OBJ_BUF_NOT_NEEDED;

    int ret = (prefetch_config->pre_parse_hook)(TS_PREFETCH_PRE_PARSE_HOOK, &info);
    if (ret == TS_PREFETCH_DISCONTINUE)
      return;
  }
  // now insert the parser
  prefetch_trans = new PrefetchTransform(sm, resp);

  if (prefetch_trans) {
    Debug("PrefetchParser", "Adding Prefetch Parser 0x%p", prefetch_trans);
    TSHttpTxnHookAdd(reinterpret_cast<TSHttpTxn>(sm), TS_HTTP_RESPONSE_TRANSFORM_HOOK, reinterpret_cast<TSCont>(prefetch_trans));

    DUMP_HEADER("PrefetchParserHdrs", &sm->t_state.hdr_info.client_request, (int64_t)0,
                "Request Header given for  Prefetch Parser");
  }
}

static int
PrefetchPlugin(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  HttpSM *sm      = (HttpSM *)edata;
  HTTPHdr *resp   = 0;
  bool from_cache = false;

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
    Debug("PrefetchPlugin",
          "Received TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK "
          "event (sm = 0x%p)\n",
          sm);
    int status;
    TSHttpTxnCacheLookupStatusGet((TSHttpTxn)sm, &status);

    if (status == TS_CACHE_LOOKUP_HIT_FRESH) {
      Debug("PrefetchPlugin", "Cached object is fresh");
      resp       = sm->t_state.cache_info.object_read->response_get();
      from_cache = true;
    } else {
      Debug("PrefetchPlugin", "Cache lookup did not succeed");
    }

    break;
  }
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    Debug("PrefetchPlugin",
          "Received TS_EVENT_HTTP_READ_RESPONSE_HDR "
          "event (sm = 0x%p)\n",
          sm);
    resp = &sm->t_state.hdr_info.server_response;

    break;

  default:
    Debug("PrefetchPlugin", "Error: Received unexpected event");
    return 0;
  }

  if (resp && resp->valid())
    check_n_attach_prefetch_transform(sm, resp, from_cache);

  TSHttpTxnReenable(reinterpret_cast<TSHttpTxn>(sm), TS_EVENT_HTTP_CONTINUE);

  // Debug("PrefetchPlugin", "Returning after check_n_attach_prefetch_transform()");

  return 0;
}

void
PrefetchProcessor::start()
{
  // we need to create the config and register all config callbacks
  // first.
  prefetch_reconfig_mutex = new_ProxyMutex();
  prefetch_config         = new PrefetchConfiguration;
  RecRegisterConfigUpdateCb("proxy.config.prefetch.prefetch_enabled", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.http.server_port", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.child_port", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.url_buffer_size", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.url_buffer_timeout", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.keepalive_timeout", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.push_cached_objects", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.max_object_size", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.max_recursion", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.redirection", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.default_url_proto", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.default_url_proto", prefetch_config_cb, NULL);
  RecRegisterConfigUpdateCb("proxy.config.prefetch.config_file", prefetch_config_cb, NULL);

  prefetch_config->readConfiguration();

  if (prefetch_config->prefetch_enabled) {
    PREFETCH_FIELD_RECURSION     = "@InkPrefetch";
    PREFETCH_FIELD_LEN_RECURSION = strlen(PREFETCH_FIELD_RECURSION);
    // hdrtoken_wks_to_length(PREFETCH_FIELD_RECURSION);

    g_conn_table = new KeepAliveConnTable;
    g_conn_table->init();

    udp_seq_no = this_ethread()->generator.random();

    prefetch_udp_fd = socketManager.socket(PF_INET, SOCK_DGRAM, 0);

    TSCont contp = TSContCreate(PrefetchPlugin, NULL);
    TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
    TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);

    Note("PrefetchProcessor: Started the prefetch processor\n");
  } else {
    Debug("PrefetchProcessor", "Prefetch processor is not started");
  }
}

// Blaster

ClassAllocator<BlasterUrlList> blasterUrlListAllocator("blasterUrlList");

int
BlasterUrlList::handleEvent(int event, void *data)
{
  switch (event) {
  case EVENT_INTERVAL:
    ink_assert(list_head);
    if (list_head) {
      invokeUrlBlaster();
    }
    action = NULL;
    break;

  case EVENT_IMMEDIATE:
    /*
       PrefetchTransform informed us not to expect any more URLs
       This is used so that we dont wait for timeout when the mtu has not filled
       but theren't any URLs left in the page.
     */
    if (list_head) {
      action->cancel();
      action = NULL;
      invokeUrlBlaster();
    }
    free(); // we need to call free because PrefetchTransform does not.
    break;

  case PREFETCH_EVENT_SEND_URL: {
    PrefetchUrlEntry *entry = ((PrefetchUrlEntry *)data)->assign();

    if (list_head) {
      action->cancel();
      action = NULL;
      if ((cur_len + entry->len) > mtu) {
        invokeUrlBlaster();
      }
    }

    entry->blaster_link = list_head; // will be reversed before sending
    list_head           = entry;
    cur_len += entry->len;

    if (cur_len >= mtu || timeout == 0) {
      invokeUrlBlaster();
    } else {
      action = this_ethread()->schedule_in(this, HRTIME_MSECONDS(timeout));
    }

    break;
  }

  default:
    ink_assert(!"not reached");
  }

  return EVENT_DONE;
}

ClassAllocator<PrefetchUrlBlaster> prefetchUrlBlasterAllocator("prefetchUrlBlaster");

inline void
PrefetchUrlBlaster::free()
{
  if (action)
    action->cancel();

  // free the list;
  while (url_head) {
    PrefetchUrlEntry *next = url_head->blaster_link;
    this_ethread()->schedule_imm(url_head->resp_blaster);
    url_head->free();
    url_head = next;
  }

  mutex.clear();
  prefetchUrlBlasterAllocator.free(this);
}

void
PrefetchUrlBlaster::writeBuffer(MIOBuffer *buf)
{
  // reverse the list:
  PrefetchUrlEntry *entry = NULL;
  // int total_len = 0;
  while (url_head) {
    // total_len += url_head->len;

    PrefetchUrlEntry *next = url_head->blaster_link;
    url_head->blaster_link = entry;
    entry                  = url_head;
    url_head               = next;
  }
  url_head = entry;

  int nurls = 0;
  // write it:
  while (entry) {
    buf->write(entry->url, entry->len);
    entry = entry->blaster_link;
    nurls++;
  }
  Debug("PrefetchBlasterUrlList", "found %d urls in the list", nurls);
  return;
}

int
PrefetchUrlBlaster::udpUrlBlaster(int event, void *data)
{
  switch (event) {
  case SIMPLE_EVENT_EVENTS_START: {
    SET_HANDLER((EventHandler)(&PrefetchUrlBlaster::udpUrlBlaster));

    MIOBuffer *buf         = new_MIOBuffer();
    IOBufferReader *reader = buf->alloc_reader();

    int udp_hdr_len = (TS_PREFETCH_TCP_BLAST == blast.type) ? 0 : PRELOAD_UDP_HEADER_LEN;

    buf->fill(udp_hdr_len + PRELOAD_HEADER_LEN);

    writeBuffer(buf);

    if (TS_PREFETCH_TCP_BLAST == blast.type) {
      setup_object_header(reader->start(), reader->read_avail(), true);
      g_conn_table->append(url_head->child_ip, buf, reader);
      free();
    } else {
      IOBufferBlock *block = buf->get_current_block();
      ink_assert(reader->read_avail() == block->read_avail());
      setup_udp_header(block->start(), get_udp_seq_no(), 0, true);
      setup_object_header(block->start() + PRELOAD_UDP_HEADER_LEN, block->read_avail() - PRELOAD_UDP_HEADER_LEN, true);

      IpEndpoint saddr;
      ats_ip_copy(&saddr, &url_head->url_multicast_ip) || ats_ip_copy(&saddr, &url_head->child_ip);
      ats_ip_port_cast(&saddr.sa) = htons(prefetch_config->stuffer_port);

      udpNet.sendto_re(this, NULL, prefetch_udp_fd, &saddr.sa, sizeof(saddr), block, block->read_avail());
    }
    break;
  }

  case NET_EVENT_DATAGRAM_WRITE_ERROR:
    Debug("PrefetchBlaster", "Error in sending the url list on UDP (%p)", data);
  case NET_EVENT_DATAGRAM_WRITE_COMPLETE:
    free();
    break;
  }
  return EVENT_DONE;
}

ClassAllocator<PrefetchBlaster> prefetchBlasterAllocator("PrefetchBlasterAllocator");

int
PrefetchBlaster::init(PrefetchUrlEntry *entry, HTTPHdr *req_hdr, PrefetchTransform *p_trans)
{
  mutex = new_ProxyMutex();

  // extract host and the path
  // by this time, the url is sufficiently error checked..
  // we will just use sscanf to parse it:
  // int host_pos=-1, path_pos=-1;
  int url_len = strlen(entry->url);

  request = new HTTPHdr;
  request->copy(req_hdr);
  url_clear(request->url_get()->m_url_impl); /* BugID: INKqa11148 */
  // request->url_get()->clear();

  // INKqa12871
  request->field_delete(MIME_FIELD_HOST, MIME_LEN_HOST);
  request->field_delete(MIME_FIELD_IF_MATCH, MIME_LEN_IF_MATCH);
  request->field_delete(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE);
  request->field_delete(MIME_FIELD_IF_NONE_MATCH, MIME_LEN_IF_NONE_MATCH);
  request->field_delete(MIME_FIELD_IF_RANGE, MIME_LEN_IF_RANGE);
  request->field_delete(MIME_FIELD_IF_UNMODIFIED_SINCE, MIME_LEN_IF_UNMODIFIED_SINCE);
  request->field_delete(MIME_FIELD_CACHE_CONTROL, MIME_LEN_CACHE_CONTROL);
  // BZ 50540
  request->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);

  int temp;
  if (request->url_get()->parse(entry->url, url_len) != PARSE_DONE || request->url_get()->scheme_get(&temp) != URL_SCHEME_HTTP) {
    Debug("PrefetchParserURLs",
          "URL parsing failed or scheme is not HTTP "
          "for %s",
          entry->url);
    free();
    return -1;
  }

  request->method_set(HTTP_METHOD_GET, HTTP_LEN_GET);

  request->field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
  request->value_set(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION, "close", 5);

  // INKqa12871
  if (request->field_find(MIME_FIELD_REFERER, MIME_LEN_REFERER)) {
    int topurl_len;
    char *topurl = req_hdr->url_get()->string_get(NULL, &topurl_len);
    if (topurl) {
      request->value_set(MIME_FIELD_REFERER, MIME_LEN_REFERER, topurl, topurl_len);
      ats_free(topurl);
    }
  }

  if (request->field_find(MIME_FIELD_AUTHORIZATION, MIME_LEN_AUTHORIZATION)) {
    int host_len;
    bool delete_auth;
    const char *host_start = request->url_get()->host_get(&host_len);

    if (host_start == NULL)
      delete_auth = true;
    else {
      const char *host_end = host_start + host_len - 1;
      int cmp_len          = p_trans->domain_end - p_trans->domain_start + 1;

      if (cmp_len <= 0 || host_len < cmp_len ||
          (host_len > cmp_len && host_start[host_len - cmp_len - 1] != '.') || // nbc.com != cnbc.com
          strncasecmp(p_trans->domain_start, host_end - (cmp_len - 1), cmp_len) != 0) {
        delete_auth = true;
      } else
        delete_auth = false;
    }

    if (delete_auth)
      request->field_delete(MIME_FIELD_AUTHORIZATION, MIME_LEN_AUTHORIZATION);
  }
  // Should we remove any cookies? Probably yes
  // We should probably add a referer header.
  handleCookieHeaders(req_hdr, &p_trans->m_sm->t_state.hdr_info.server_response, p_trans->domain_start, p_trans->domain_end,
                      p_trans->host_start, p_trans->host_len, p_trans->no_dot_in_host);

  // FIXME? ip_len is pretty useless here.
  int ip_len;
  const char *ip_str;
  if (IS_RECURSIVE_PREFETCH(entry->req_ip) && (ip_str = request->value_get(MIME_FIELD_CLIENT_IP, MIME_LEN_CLIENT_IP, &ip_len))) {
    ip_text_buffer b;
    // this is a recursive prefetch. get child ip address from
    // Client-IP header
    ink_strlcpy(b, ip_str, sizeof(b));
    ats_ip_pton(b, &entry->child_ip.sa);
  } else
    entry->child_ip = entry->req_ip;

  DUMP_HEADER("PrefetchBlasterHdrs", request, (int64_t)0, "Request Header from Prefetch Blaster");

  url_ent   = entry->assign(); // refcount
  transform = p_trans->assign();

  buf    = new_MIOBuffer();
  reader = buf->alloc_reader();

  SET_HANDLER((EventHandler)(&PrefetchBlaster::handleEvent));

  this_ethread()->schedule_imm(this);

  return EVENT_DONE;
}

void
PrefetchBlaster::free()
{
  if (serverVC)
    serverVC->do_io_close();

  if (url_ent)
    url_ent->free();
  if (transform)
    transform->free();

  if (buf)
    free_MIOBuffer(buf);
  if (io_block) {
    io_block->free();
  }

  if (request) {
    request->destroy();
    delete request;
  }

  mutex.clear();
  prefetchBlasterAllocator.free(this);
}

bool
isCookieUnique(HTTPHdr *req, const char *move_cookie, int move_cookie_len)
{
  // another double for loop for multiple Cookie headers
  MIMEField *o_cookie = req->field_find(MIME_FIELD_COOKIE, MIME_LEN_COOKIE);
  const char *a_raw;
  int a_raw_len;
  const char *iter_cookie;
  int iter_cookie_len;
  bool equalsign = false;

  if ((a_raw = (const char *)memchr(move_cookie, '=', move_cookie_len)) != NULL) {
    int tmp_len = (int)(a_raw - move_cookie) + 1;
    if (tmp_len < move_cookie_len) {
      equalsign       = true;
      move_cookie_len = tmp_len;
    }
  }

  for (; o_cookie; o_cookie = o_cookie->m_next_dup) {
    a_raw = o_cookie->value_get(&a_raw_len);
    if (a_raw != NULL && a_raw_len > 0) {
      StrList a_param_list;
      Str *a_param;

      HttpCompat::parse_tok_list(&a_param_list, 0, a_raw, a_raw_len, ';');
      for (a_param = a_param_list.head; a_param; a_param = a_param->next) {
        iter_cookie     = a_param->str;
        iter_cookie_len = a_param->len;

        if (equalsign) {
          if (iter_cookie_len > move_cookie_len && memcmp(iter_cookie, move_cookie, move_cookie_len) == 0) {
            // INKqa11823 id=new to replace id=old
            return false;
          }
        } else {
          if (iter_cookie_len == move_cookie_len && memcmp(iter_cookie, move_cookie, iter_cookie_len) == 0) {
            // dupliate - do not add
            return false;
          }
        }
      }
    }
  }

  return true;
}

inline void
cookie_debug(const char *level, const char *value, int value_len)
{
  if (is_debug_tag_set("PrefetchCookies")) {
    char *str = (char *)ats_malloc(value_len + 1);
    memcpy(str, value, value_len);
    str[value_len] = 0;
    Debug("PrefetchCookies", "Processing %s value: %s", level, str);
    ats_free(str);
  }
}

// resp_hdr is the server response for the top page
void
PrefetchBlaster::handleCookieHeaders(HTTPHdr *req_hdr, HTTPHdr *resp_hdr, const char *domain_start, const char *domain_end,
                                     const char *thost_start, int thost_len, bool no_dot)
{
  bool add_cookies           = true;
  bool existing_req_cookies  = request->valid() && request->presence(MIME_PRESENCE_COOKIE);
  bool existing_resp_cookies = resp_hdr->valid() && resp_hdr->presence(MIME_PRESENCE_SET_COOKIE);
  bool default_domain_match;
  const char *host_start;
  const char *host_end;
  int host_len, cmp_len;

  if (!existing_req_cookies && !existing_resp_cookies)
    return;

  if (!domain_start && (!thost_start || no_dot == false)) {
    // mising domain name information
    add_cookies = false;
    goto Lcheckcookie;
  }

  host_start = request->url_get()->host_get(&host_len);

  if (!host_start || !host_len)
    host_start = request->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_len);

  if (!host_start && !host_len) {
    add_cookies = false;
    goto Lcheckcookie;
  }

  host_end = host_start + host_len - 1;
  if (domain_start) {
    cmp_len = domain_end - domain_start + 1;

    if (host_len < cmp_len || (host_len > cmp_len && host_start[host_len - cmp_len - 1] != '.') || // nbc.com != cnbc.com
        strncasecmp(domain_start, host_end - (cmp_len - 1), cmp_len) != 0) {
      add_cookies = false;
      goto Lcheckcookie;
    }
    // Netscape Cookie spec says the default domain is the host name
    if (thost_len != host_len || strncasecmp(thost_start, host_start, host_len) != 0)
      default_domain_match = false;
    else
      default_domain_match = true;
  } else {
    if (host_len != thost_len || strncasecmp(thost_start, host_start, host_len) != 0) {
      add_cookies = false;
      goto Lcheckcookie;
    }
    default_domain_match = true;
  }

  if (existing_resp_cookies) {
    const char *a_raw;
    int a_raw_len;
    const char *move_cookie;
    int move_cookie_len;
    MIMEField *s_cookie = NULL;

    add_cookies = false;
    // delete the old Cookie first - INKqa11823
    request->field_delete(MIME_FIELD_COOKIE, MIME_LEN_COOKIE);
    // for Set-Cookie it is not comma separated, each a_value contains
    // the value for one Set-Cookie header
    s_cookie = resp_hdr->field_find(MIME_FIELD_SET_COOKIE, MIME_LEN_SET_COOKIE);
    for (; s_cookie; s_cookie = s_cookie->m_next_dup) {
      a_raw                 = s_cookie->value_get(&a_raw_len);
      MIMEField *new_cookie = NULL;
      StrList a_param_list;
      Str *a_param;
      bool first_move;
      bool domain_match;

      cookie_debug("PrefetchCookies", a_raw, a_raw_len);

      domain_match = default_domain_match;
      HttpCompat::parse_tok_list(&a_param_list, 0, a_raw, a_raw_len, ';');
      for (a_param = a_param_list.head; a_param; a_param = a_param->next) {
        move_cookie     = a_param->str;
        move_cookie_len = a_param->len;

        cookie_debug("Field", move_cookie, move_cookie_len);

        if (!new_cookie) {
          new_cookie = request->field_create();
          first_move = true;
        } else
          first_move = false;

        if (move_cookie_len > 7 && strncasecmp(move_cookie, "domain=", 7) == 0) {
          // the Set-cookie header specify the domain name
          const char *cookie_domain_start = move_cookie + 7;
          int cookie_domain_len           = move_cookie_len - 7;
          const char *cookie_domain_end   = (const char *)(move_cookie + move_cookie_len - 1);

          if (*cookie_domain_start == '"') {
            // domain=".amazon.com" style
            if (*cookie_domain_end == '"') {
              cookie_domain_start++;
              cookie_domain_end--;
              cookie_domain_len -= 2;
              if (cookie_domain_len <= 0)
                goto Lnotmatch;
            } else {
              // invalid fomat, missing trailing quote
              goto Lnotmatch;
            }
          }
          // remove trailing .
          while (*cookie_domain_end == '.' && cookie_domain_len > 0)
            cookie_domain_end--, cookie_domain_len--;

          if (cookie_domain_len <= 0)
            goto Lnotmatch;

          // matching domain based on RFC2109
          int prefix_len = host_len - cookie_domain_len;
          if (host_len <= 0 || prefix_len < 0)
            goto Lnotmatch;

          if (strncasecmp(host_start + prefix_len, cookie_domain_start, cookie_domain_len) != 0)
            goto Lnotmatch;

          // make sure that the prefix doesn't contain a '.'
          if (prefix_len > 0 && memchr(host_start, '.', prefix_len))
            goto Lnotmatch;

          // Ok, when we get here, it should be a real match as far as
          //        domain is concerned.
          // possibly overwrite the default domain matching result
          domain_match = true;
          continue;
        } else if (move_cookie_len > 5 && strncasecmp(move_cookie, "path=", 5) == 0) {
          const char *cookie_path_start = move_cookie + 5;
          int cookie_path_len           = move_cookie_len - 5;
          const char *cookie_path_end   = (const char *)(move_cookie + move_cookie_len - 1);

          if (cookie_path_len <= 0)
            goto Lnotmatch;

          if (*cookie_path_start == '/') {
            cookie_path_start++;
            cookie_path_len--;
          }

          if (cookie_path_len == 0) {
            // a match - "/"
            continue;
          }

          if (*cookie_path_end == '/') {
            cookie_path_end--;
            cookie_path_len--;
          }

          if (cookie_path_len == 0) {
            // invalid format "//"
            goto Lnotmatch;
          }
          // matching path based on RFC2109

          int dest_path_len;
          const char *dest_path_start = request->url_get()->path_get(&dest_path_len);

          // BZ 49734
          if (dest_path_start == NULL || dest_path_len == 0) {
            goto Lnotmatch;
          }

          if (*dest_path_start == '/') {
            dest_path_start++;
            dest_path_len--;
          }

          if (dest_path_len < cookie_path_len || strncasecmp(dest_path_start, cookie_path_start, cookie_path_len) != 0)
            goto Lnotmatch;

          // when we get here the path is a match
        } else if (move_cookie_len > 8 && strncasecmp(move_cookie, "expires=", 8) == 0) {
          // ignore expires directive for the time being
          continue;
        } else {
          // append the value to the request Cookie header
          request->field_value_append(new_cookie, move_cookie, move_cookie_len, !first_move, ';');
        }
      }

      if (domain_match == false)
        goto Lnotmatch;

      if (new_cookie) {
        add_cookies = true;
        new_cookie->name_set(request->m_heap, request->m_mime, MIME_FIELD_COOKIE, MIME_LEN_COOKIE);
        request->field_attach(new_cookie);
      }
      continue;

    Lnotmatch:
      if (new_cookie) {
        new_cookie->name_set(request->m_heap, request->m_mime, MIME_FIELD_COOKIE, MIME_LEN_COOKIE);
        request->field_attach(new_cookie);
        request->field_delete(new_cookie);
        new_cookie = NULL;
      }
    }

    // INKqa11823 - now add the old Cookies back based on the new cookies
    if (add_cookies && existing_req_cookies) {
      MIMEField *o_cookie = req_hdr->field_find(MIME_FIELD_COOKIE, MIME_LEN_COOKIE);
      const char *iter_cookie;
      int iter_cookie_len;

      for (; o_cookie; o_cookie = o_cookie->m_next_dup) {
        MIMEField *n_cookie = NULL;
        a_raw               = o_cookie->value_get(&a_raw_len);
        if (a_raw != NULL && a_raw_len > 0) {
          StrList a_param_list;
          Str *a_param;
          bool f_move;

          HttpCompat::parse_tok_list(&a_param_list, 0, a_raw, a_raw_len, ';');
          for (a_param = a_param_list.head; a_param; a_param = a_param->next) {
            iter_cookie     = a_param->str;
            iter_cookie_len = a_param->len;

            if (isCookieUnique(request, iter_cookie, iter_cookie_len)) {
              // this is a unique cookie attribute, ready to add
              if (n_cookie == NULL) {
                n_cookie = request->field_create();
                f_move   = true;
              } else
                f_move = false;

              request->field_value_append(n_cookie, iter_cookie, iter_cookie_len, !f_move, ';');
            }
          }

          if (n_cookie) {
            n_cookie->name_set(request->m_heap, request->m_mime, MIME_FIELD_COOKIE, MIME_LEN_COOKIE);
            request->field_attach(n_cookie);
          }
        }
      }
    }
    // add_cookies now means whether new Cookie headers are created
    // from the Set-Cookie headers
    // now also check the existing Cookie headers from the req_hdr
    add_cookies = add_cookies || existing_req_cookies;
  }

Lcheckcookie:
  if (add_cookies == false) {
    // delete the cookie header, if there is any at all
    request->field_delete(MIME_FIELD_COOKIE, MIME_LEN_COOKIE);
  }

  DUMP_HEADER("PrefetchCookies", req_hdr, (int64_t)0,
              "Request Header for the top page used as the base for the new request with Cookies");
  DUMP_HEADER("PrefetchCookies", resp_hdr, (int64_t)0,
              "Response Header for the top page used as the base for the new request with Cookies");
  DUMP_HEADER("PrefetchCookies", request, (int64_t)0, "Request Header with Cookies generated by Prefetch Parser");
}

int
PrefetchBlaster::handleEvent(int event, void *data)
{
  /*
     This one first decides if we need to send the url or not.
     If necessary, send the url ( Right now, just connect on TCP
     connection and send the data)
   */

  switch (event) {
  case EVENT_IMMEDIATE: {
    HttpCacheKey key;

    // Here, we need to decide if we need to prefetch based on whether it
    // is in the cache or not.
    Cache::generate_key(&key, request->url_get()); // XXX choose a cache generation number ...
    cacheProcessor.open_read(this, &key, false, request, &http_config_params->oride, 0);

    break;
  }

  case EVENT_INTERVAL: {
    if (url_list) {
      MUTEX_TRY_LOCK(trylock, url_list->mutex, this_ethread());
      if (!trylock.is_locked()) {
        this_ethread()->schedule_in(this, HRTIME_MSECONDS(10));
        break;
      }
      url_ent->resp_blaster = this;
      url_list->handleEvent(PREFETCH_EVENT_SEND_URL, url_ent);
    }

    if (serverVC) {
      SET_HANDLER((EventHandler)(&PrefetchBlaster::bufferObject));
    } else {
      SET_HANDLER((EventHandler)(&PrefetchBlaster::httpClient));
    }

    transform->free();
    transform = NULL;

    if (!url_list)
      this_ethread()->schedule_imm_local(this);
    // Otherwise, just wait till PrefetchUrlBlaster signals us.

    break;
  }

  case CACHE_EVENT_OPEN_READ: {
    // action = NULL;

    Debug("PrefetchBlaster", "Cache lookup succeded for %s", url_ent->url);

    serverVC = (VConnection *)data;

    ((CacheVConnection *)data)->get_http_info(&cache_http_info);

    invokeBlaster();
    break;
  }
  case CACHE_EVENT_OPEN_READ_FAILED:
    // action = NULL;
    Debug("PrefetchBlaster", "Cache lookup failed for %s", url_ent->url);

    invokeBlaster();
    break;

  default:
    ink_assert(!"not reached");
    free();
  }

  return EVENT_DONE;
}

static int
copy_header(MIOBuffer *buf, HTTPHdr *hdr, const char *hdr_tail)
{
  // copy the http header into to the buffer
  int64_t done   = 0;
  int64_t offset = 0;

  while (!done) {
    int64_t block_len = buf->block_write_avail();
    int index = 0, temp = offset;

    done = hdr->print(buf->end(), block_len, &index, &temp);

    ink_assert(done || index == block_len);

    offset += index;

    if (!done) {
      buf->fill(index);
      buf->add_block();
    } else {
      ink_assert(index >= 2);
      if (hdr_tail && index >= 2) {
        /*This is a hack to be able to send headers beginning with @ */
        int len = strlen(hdr_tail);
        offset += len - 2;
        buf->fill(index - 2);
        buf->write(hdr_tail, len);
      } else
        buf->fill(index);
    }
  }

  return offset;
}

int
PrefetchBlaster::httpClient(int event, void *data)
{
  /*
     This one makes an http connection on the local host and sends the request
   */

  switch (event) {
  case EVENT_IMMEDIATE: {
    IpEndpoint target;
    target.setToLoopback(AF_INET);
    target.port() = prefetch_config->local_http_server_port;
    netProcessor.connect_re(this, &target.sa);
    break;
  }

  case NET_EVENT_OPEN: {
    serverVC = (VConnection *)data;
    buf->reset();

    char *rec_header = 0;
    char hdr_buf[64];

    if (request->field_find(PREFETCH_FIELD_RECURSION, PREFETCH_FIELD_LEN_RECURSION)) {
      snprintf(hdr_buf, sizeof(hdr_buf), "%s: %d\r\n\r\n", PREFETCH_FIELD_RECURSION,
               request->value_get_int(PREFETCH_FIELD_RECURSION, PREFETCH_FIELD_LEN_RECURSION));
      rec_header = hdr_buf;
    }

    int len = copy_header(buf, request, rec_header);

    serverVC->do_io_write(this, len, reader);

    break;
  }

  case NET_EVENT_OPEN_FAILED:
    Debug("PrefetchBlaster", "Open to local http port failed.. strange");
    free();
    break;

  case VC_EVENT_WRITE_READY:
    break;
  case VC_EVENT_WRITE_COMPLETE:
    SET_HANDLER((EventHandler)(&PrefetchBlaster::bufferObject));
    bufferObject(EVENT_IMMEDIATE, NULL);
    break;

  default:
    Debug("PrefetchBlaster", "Unexpected Event: %d(%s)", event, get_vc_event_name(event));
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    free();
    break;
  }

  return EVENT_DONE;
}

int
PrefetchBlaster::bufferObject(int event, void * /* data ATS_UNUSED */)
{
  switch (event) {
  case EVENT_INTERVAL:
  case EVENT_IMMEDIATE: {
    buf->reset();
    buf->water_mark = prefetch_config->max_object_size;
    buf->fill(PRELOAD_HEADER_LEN);

    int64_t ntoread = INT64_MAX;
    copy_header(buf, request, NULL);

    if (cache_http_info) {
      copy_header(buf, cache_http_info->response_get(), NULL);
      ntoread = cache_http_info->object_size_get();
    }
    serverVC->do_io_read(this, ntoread, buf);
    break;
  }

  case VC_EVENT_READ_READY:
    if (buf->high_water()) {
      // right now we don't handle DEL events on the child
      Debug("PrefetchBlasterTemp",
            "The object is bigger than %" PRId64 " bytes "
            "cancelling the url",
            buf->water_mark);
      buf->reset();
      buf->fill(PRELOAD_HEADER_LEN);
      buf->write("DEL ", 4);
      buf->write(url_ent->url, url_ent->len);
      blastObject(EVENT_IMMEDIATE, (void *)1);
    }
    break;

  default:
    Debug("PrefetchBlaster", "Error Event: %s", get_vc_event_name(event));
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS:
    blastObject(EVENT_IMMEDIATE, NULL);
    break;
  }

  return EVENT_DONE;
}

/* following sturcture and masks should be the same in StufferUdpReceiver.cc
   on microTS */

int
PrefetchBlaster::blastObject(int event, void *data)
{
  switch (event) {
  case EVENT_IMMEDIATE:
    serverVC->do_io_close();
    serverVC = 0;

    // (data == (void*)1) implies we are not sending the object because
    // it is too large. Instead we will send "DEL" msg for the promise
    bool obj_cancelled;
    obj_cancelled = (data == (void *)1);

    setup_object_header(reader->start(), reader->read_avail(), obj_cancelled);

    if (url_ent->object_buf_status != TS_PREFETCH_OBJ_BUF_NOT_NEEDED && prefetch_config->embedded_obj_hook && !obj_cancelled) {
      TSPrefetchInfo info;
      memset(&info, 0, sizeof(info));

      info.embedded_url      = url_ent->url;
      info.object_buf_status = url_ent->object_buf_status;

      info.object_buf        = TSIOBufferCreate();
      info.object_buf_reader = TSIOBufferReaderAlloc(info.object_buf);

      ((MIOBuffer *)info.object_buf)->write(reader);

      prefetch_config->embedded_obj_hook(TS_PREFETCH_EMBEDDED_OBJECT_HOOK, &info);
    }

    if (url_ent->object_buf_status == TS_PREFETCH_OBJ_BUF_NEEDED) {
      // we need not send this to the child
      free();
      break;
    }

    if (data_blast.type == TS_PREFETCH_TCP_BLAST) {
      g_conn_table->append(url_ent->child_ip, buf, reader);
      buf = 0;
      free();
      break;
    } else {
      SET_HANDLER((EventHandler)(&PrefetchBlaster::blastObject));
      *(int *)reader->start() = htonl(reader->read_avail());

      io_block = ioBlockAllocator.alloc();
      io_block->alloc(BUFFER_SIZE_INDEX_32K);

      seq_no = get_udp_seq_no();
      // fall through
    }

  case NET_EVENT_DATAGRAM_WRITE_COMPLETE: {
    io_block->reset();
    io_block->fill(PRELOAD_UDP_HEADER_LEN);

    int64_t nread_avail = reader->read_avail();

    if (nread_avail <= 0) {
      free();
      break;
    }

    int64_t nwrite_avail = io_block->write_avail();

    int64_t towrite = (nread_avail < nwrite_avail) ? nread_avail : nwrite_avail;

    reader->read(io_block->end(), towrite);
    io_block->fill(towrite);

    Debug("PrefetchBlaster",
          "UDP: sending data: pkt_no: %d last_pkt: %d"
          " url: %s",
          n_pkts_sent, (towrite >= nread_avail), url_ent->url);

    setup_udp_header(io_block->start(), seq_no, n_pkts_sent++, (towrite >= nread_avail));

    IpEndpoint saddr;
    ats_ip_copy(&saddr.sa, ats_is_ip(&url_ent->data_multicast_ip) ? &url_ent->data_multicast_ip.sa : &url_ent->child_ip.sa);
    ats_ip_port_cast(&saddr) = htons(prefetch_config->stuffer_port);

    // saddr.sin_addr.s_addr = htonl((209<<24)|(131<<16)|(60<<8)|243);
    // saddr.sin_addr.s_addr = htonl((209<<24)|(131<<16)|(48<<8)|52);

    udpNet.sendto_re(this, NULL, prefetch_udp_fd, &saddr.sa, sizeof(saddr), io_block, io_block->read_avail());
  } break;

  case NET_EVENT_DATAGRAM_WRITE_ERROR:
    Debug("PrefetchBlaster", "error in sending the udp data %p", data);

  default:
    ink_assert(!"unexpected event");
  }
  return EVENT_DONE;
}

int
PrefetchBlaster::invokeBlaster()
{
  int ret = (cache_http_info && !prefetch_config->push_cached_objects) ? TS_PREFETCH_DISCONTINUE : TS_PREFETCH_CONTINUE;

  TSPrefetchBlastData url_blast = prefetch_config->default_url_blast;
  data_blast                    = prefetch_config->default_data_blast;

  if (prefetch_config->embedded_url_hook) {
    TSPrefetchInfo info;

    info.request_buf  = reinterpret_cast<TSMBuffer>(request);
    info.request_loc  = reinterpret_cast<TSMLoc>(request->m_http);
    info.response_buf = 0;
    info.response_loc = 0;

    info.object_buf        = 0;
    info.object_buf_reader = 0;
    info.object_buf_status = TS_PREFETCH_OBJ_BUF_NOT_NEEDED;

    ats_ip_copy(ats_ip_sa_cast(&info.client_ip), &url_ent->child_ip);
    info.embedded_url       = url_ent->url;
    info.present_in_cache   = (cache_http_info != NULL);
    info.url_blast          = url_blast;
    info.url_response_blast = data_blast;

    ret = (*prefetch_config->embedded_url_hook)(TS_PREFETCH_EMBEDDED_URL_HOOK, &info);

    url_blast  = info.url_blast;
    data_blast = info.url_response_blast;

    url_ent->object_buf_status = info.object_buf_status;
  }

  if (ret == TS_PREFETCH_CONTINUE) {
    if (TS_PREFETCH_MULTICAST_BLAST == url_blast.type)
      ats_ip_copy(&url_ent->url_multicast_ip, ats_ip_sa_cast(&url_blast.ip));
    if (TS_PREFETCH_MULTICAST_BLAST == data_blast.type)
      ats_ip_copy(&url_ent->data_multicast_ip, ats_ip_sa_cast(&data_blast.ip));

    if (url_ent->object_buf_status != TS_PREFETCH_OBJ_BUF_NEEDED) {
      if (url_blast.type == TS_PREFETCH_TCP_BLAST)
        url_list = transform->tcp_url_list;
      else
        url_list = transform->udp_url_list;
    }
    // if recursion is enabled, go through local host even for cached
    // objects
    if (prefetch_config->max_recursion > 0 && serverVC) {
      serverVC->do_io_close();
      serverVC        = NULL;
      cache_http_info = 0;
    }

    /*
       if (data_proto == TCP_BLAST)
       data_blaster = (EventHandler)(&PrefetchBlaster::tcpDataBlaster);
       else data_blaster = (EventHandler)(&PrefetchBlaster::udpDataBlaster);
     */
    handleEvent(EVENT_INTERVAL, NULL);
  } else {
    free();
  }
  return 0;
}

static int
config_read_proto(TSPrefetchBlastData &blast, const char *str)
{
  if (strncasecmp(str, "udp", 3) == 0)
    blast.type = TS_PREFETCH_UDP_BLAST;
  else if (strncasecmp(str, "tcp", 3) == 0)
    blast.type = TS_PREFETCH_TCP_BLAST;
  else { // this is a multicast address:
    if (strncasecmp("multicast:", str, 10) == 0) {
      if (0 != ats_ip_pton(str, ats_ip_sa_cast(&blast.ip))) {
        Error("PrefetchProcessor: Address specified for multicast does not seem to "
              "be of the form multicast:ip_addr (eg: multicast:224.0.0.1)");
        return 1;
      } else {
        ip_text_buffer ipb;
        blast.type = TS_PREFETCH_MULTICAST_BLAST;
        Debug("Prefetch", "Setting multicast address: %s", ats_ip_ntop(ats_ip_sa_cast(&blast.ip), ipb, sizeof(ipb)));
      }
    } else {
      Error("PrefetchProcessor: The protocol for Prefetch should of the form: "
            "tcp or udp or multicast:ip_address");
      return 1;
    }
  }

  return 0;
}

int
PrefetchConfiguration::readConfiguration()
{
  ats_scoped_str conf_path;
  int fd = -1;

  local_http_server_port = stuffer_port = 0;
  prefetch_enabled                      = REC_ConfigReadInteger("proxy.config.prefetch.prefetch_enabled");
  if (prefetch_enabled <= 0) {
    prefetch_enabled = 0;
    return 0;
  }

  local_http_server_port = HttpProxyPort::findHttp(AF_INET)->m_port;
  REC_ReadConfigInteger(stuffer_port, "proxy.config.prefetch.child_port");
  REC_ReadConfigInteger(url_buffer_size, "proxy.config.prefetch.url_buffer_size");
  REC_ReadConfigInteger(url_buffer_timeout, "proxy.config.prefetch.url_buffer_timeout");
  REC_ReadConfigInteger(keepalive_timeout, "proxy.config.prefetch.keepalive_timeout");
  if (keepalive_timeout <= 0)
    keepalive_timeout = 3600;

  REC_ReadConfigInteger(push_cached_objects, "proxy.config.prefetch.push_cached_objects");

  REC_ReadConfigInteger(max_object_size, "proxy.config.prefetch.max_object_size");

  REC_ReadConfigInteger(max_recursion, "proxy.config.prefetch.max_recursion");

  REC_ReadConfigInteger(redirection, "proxy.config.prefetch.redirection");

  char *tstr = REC_ConfigReadString("proxy.config.prefetch.default_url_proto");
  if (config_read_proto(default_url_blast, tstr))
    goto Lerror;

  tstr = REC_ConfigReadString("proxy.config.prefetch.default_data_proto");
  if (config_read_proto(default_data_blast, tstr))
    goto Lerror;

  // pre_parse_hook = 0;
  // embedded_url_hook = 0;

  conf_path = RecConfigReadConfigPath("proxy.config.prefetch.config_file");
  if (!conf_path) {
    Warning("PrefetchProcessor: No prefetch configuration file specified. Prefetch disabled\n");
    goto Lerror;
  }

  fd = open(conf_path, O_RDONLY);
  if (fd < 0) {
    Error("PrefetchProcessor: Error, could not open '%s' disabling Prefetch\n", (const char *)conf_path);
    goto Lerror;
  }

  char *temp_str;
  if ((temp_str = Load_IpMap_From_File(&ip_map, fd, "prefetch_children")) != 0) {
    Error("PrefetchProcessor: Error in reading ip_range from %s: %.256s\n", (const char *)conf_path, temp_str);
    ats_free(temp_str);
    goto Lerror;
  }

  lseek(fd, 0, SEEK_SET);
  readHtmlTags(fd, &html_tags_table, &html_attrs_table);
  if (html_tags_table == NULL) {
    html_tags_table = &prefetch_allowable_html_tags[0];
    ink_assert(html_attrs_table == NULL);
    html_attrs_table = &prefetch_allowable_html_attrs[0];
  }

  close(fd);
  return 0;
Lerror:
  if (fd >= 0)
    close(fd);
  prefetch_enabled = 0;
  return -1;
}

void
PrefetchConfiguration::readHtmlTags(int fd, html_tag **ptags, html_tag **pattrs)
{
  int ntags = 0;
  html_tag tags[256];
  html_tag attrs[256];
  bool attrs_exist = false;
  char buf[512], tag[64], attr[64], attr_tag[64], attr_attr[64];
  int num;
  int end_of_file = 0;

  memset(attrs, 0, 256 * sizeof(html_tag));
  while (!end_of_file && ntags < 256) {
    char c;
    int ret, len = 0;
    // read the line
    while (((ret = read(fd, &c, 1)) == 1) && (c != '\n'))
      if (len < 511)
        buf[len++] = c;
    buf[len] = 0;
    if (ret <= 0)
      end_of_file = 1;

    // length(63) specified in sscanf, no need to worry about string overflow
    // coverity[secure_coding]
    if ((num = sscanf(buf, " html_tag %63s %63s %63s %63s", tag, attr, attr_tag, attr_attr)) >= 2) {
      Debug("Prefetch", "Read html_tag: %s %s", tag, attr);
      tags[ntags].tag  = ats_strdup(tag);
      tags[ntags].attr = ats_strdup(attr);
      if (num >= 4) {
        if (!attrs_exist)
          attrs_exist = true;
        attrs[ntags].tag = ats_strdup(attr_tag);
        attrs[ntags].tag = ats_strdup(attr_attr);
      }
      ntags++;
    }
  }

  if (ntags > 0) {
    html_tag *xtags = (html_tag *)ats_malloc((ntags + 3) * sizeof(html_tag));

    memcpy(xtags, &tags[0], ntags * sizeof(tags[0]));
    // the following two are always added
    xtags[ntags].tag      = "base";
    xtags[ntags].attr     = "href";
    xtags[ntags + 1].tag  = "meta";
    xtags[ntags + 1].attr = "content";
    xtags[ntags + 2].tag = xtags[ntags + 2].attr = NULL;

    *ptags = xtags;
    if (attrs_exist) {
      html_tag *xattrs = (html_tag *)ats_malloc((ntags + 3) * sizeof(html_tag));
      memcpy(xattrs, &attrs[0], 256 * sizeof(html_tag));
      *pattrs = xattrs;
    } else
      *pattrs = NULL;
    return;
  }

  *ptags  = NULL;
  *pattrs = NULL;
}

/* Keep Alive stuff */

#define CONN_ARR_SIZE 256
inline int
KeepAliveConnTable::ip_hash(IpEndpoint const &ip)
{
  return ats_ip_hash(&ip.sa) & (CONN_ARR_SIZE - 1);
}

inline int
KeepAliveConn::append(IOBufferReader *rdr)
{
  int64_t size = rdr->read_avail();

  nbytes_added += size;

  buf->write(rdr);
  vio->reenable();

  return 0;
}

int
KeepAliveConnTable::init()
{
  arr = new conn_elem[CONN_ARR_SIZE];

  for (int i = 0; i < CONN_ARR_SIZE; i++) {
    arr[i].conn  = 0;
    arr[i].mutex = new_ProxyMutex();
  }

  return 0;
}

void
KeepAliveConnTable::free()
{
  for (int i = 0; i < CONN_ARR_SIZE; i++)
    arr[i].mutex.clear();

  delete arr;
  delete this;
}

ClassAllocator<KeepAliveLockHandler> prefetchLockHandlerAllocator("prefetchLockHandlerAllocator");

int
KeepAliveConnTable::append(IpEndpoint const &ip, MIOBuffer *buf, IOBufferReader *reader)
{
  int index = ip_hash(ip);

  MUTEX_TRY_LOCK(trylock, arr[index].mutex, this_ethread());
  if (!trylock.is_locked()) {
    /* This lock fails quite often. This can be expected because,
       multiple threads try to append their buffer all the the same
       time to the same connection. Other thread holds it for a long
       time when it is doing network IO 'n stuff. This is one more
       reason why URL messages should be sent by UDP. We will avoid
       appending small messages here and those URL message reach the
       child much faster */

    prefetchLockHandlerAllocator.alloc()->init(ip, buf, reader);
    return 1;
  }

  KeepAliveConn **conn = &arr[index].conn;

  while (*conn && !ats_ip_addr_eq(&(*conn)->ip, &ip))
    conn = &(*conn)->next;

  if (*conn) {
    (*conn)->append(reader);
    free_MIOBuffer(buf);
  } else {
    *conn = new KeepAliveConn; // change to fast allocator?
    (*conn)->init(ip, buf, reader);
  }

  return 0;
}

int
KeepAliveConn::init(IpEndpoint const &xip, MIOBuffer *xbuf, IOBufferReader *xreader)
{
  mutex = g_conn_table->arr[KeepAliveConnTable::ip_hash(xip)].mutex;

  ip     = xip;
  buf    = xbuf;
  reader = xreader;

  childVC = 0;
  vio     = 0;
  next    = 0;

  read_buf = new_MIOBuffer(); // we should give minimum size possible

  nbytes_added = reader->read_avail();

  SET_HANDLER(&KeepAliveConn::handleEvent);

  // we are already under lock
  netProcessor.connect_re(this, &ip.sa);

  return 0;
}

void
KeepAliveConn::free()
{
  if (childVC)
    childVC->do_io_close();

  if (buf)
    free_MIOBuffer(buf);
  if (read_buf)
    free_MIOBuffer(read_buf);

  KeepAliveConn *prev  = 0;
  KeepAliveConn **head = &g_conn_table->arr[KeepAliveConnTable::ip_hash(ip)].conn;

  KeepAliveConn *conn = *head;
  while (conn != this) {
    prev = conn;
    conn = conn->next;
  }

  if (prev)
    prev->next = next;
  else
    *head = next;

  mutex.clear();
  Debug("PrefetchKConn", "deleting a KeepAliveConn");
  delete this;
}

int
KeepAliveConn::handleEvent(int event, void *data)
{
  ip_text_buffer ipb;

  switch (event) {
  case NET_EVENT_OPEN:

    childVC = (NetVConnection *)data;

    childVC->set_inactivity_timeout(HRTIME_SECONDS(prefetch_config->keepalive_timeout));

    vio = childVC->do_io_write(this, INT64_MAX, reader);

    // this read lets us disconnect when the other side closes
    childVC->do_io_read(this, INT64_MAX, read_buf);
    break;

  case NET_EVENT_OPEN_FAILED:
    Debug("PrefetchKeepAlive", "Connection to child %s failed", ats_ip_ntop(&ip.sa, ipb, sizeof(ipb)));
    free();
    break;

  case VC_EVENT_WRITE_READY:
    // Debug("PrefetchTemp", "ndone = %d", vio->ndone);

    break;

  case VC_EVENT_INACTIVITY_TIMEOUT:
    // Debug("PrefetchTemp", "%d sec timeout expired for %d.%d.%d.%d",
    // prefetch_config->keepalive_timeout, IPSTRARGS(ip));

    if (reader->read_avail())
      childVC->set_inactivity_timeout(HRTIME_SECONDS(prefetch_config->keepalive_timeout));
    else
      free();
    break;

  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_READ_READY:
  /*Right now we dont expect any response from the child.
     Read event implies POLLHUP */
  case VC_EVENT_EOS:
    Debug("PrefetchKeepAlive", "the other side closed the connection");
    free();
    break;

  case VC_EVENT_ERROR:
    Debug("PrefetchKeepAlive",
          "got VC_ERROR.. connection problem? "
          "(ip: %s)",
          ats_ip_ntop(&ip.sa, ipb, sizeof(ipb)));
    free();
    break;

  default:
    ink_assert(!"not reached");
    free();
  }

  return EVENT_DONE;
}

int
KeepAliveLockHandler::handleEvent(int event, void * /* data ATS_UNUSED */)
{
  if (event == EVENT_INTERVAL)
    g_conn_table->append(ip, buf, reader);

  prefetchLockHandlerAllocator.free(this);

  return EVENT_DONE;
}

/* API */
int
TSPrefetchHookSet(int hook_no, TSPrefetchHook hook)
{
  switch (hook_no) {
  case TS_PREFETCH_PRE_PARSE_HOOK:
    prefetch_config->pre_parse_hook = hook;
    return 0;

  case TS_PREFETCH_EMBEDDED_URL_HOOK:
    prefetch_config->embedded_url_hook = hook;
    return 0;

  case TS_PREFETCH_EMBEDDED_OBJECT_HOOK:
    prefetch_config->embedded_obj_hook = hook;
    return 0;

  default:
    return -1;
  }
}

#endif // PREFETCH
