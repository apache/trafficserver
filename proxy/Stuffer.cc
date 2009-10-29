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

#include "Net.h"
#include "HTTP.h"
#include "HttpTransact.h"
#include "HttpTransactHeaders.h"
#include "Stuffer.h"
#include "ParentSelection.h"

StufferHashTable *stuffer_htable;
static inku32 *stuffer_parent_ip_array = 0;
static int stuffer_num_parents;

static inline bool
connAllowed(inku32 ip)
{
  if (((unsigned char *) &ip)[0] == 127)        // allow localhost connetions
    return true;

  int n = stuffer_num_parents;
  inku32 *ips = stuffer_parent_ip_array;

  for (int i = 0; i < n; i++)
    if (ip == ips[i])
      return true;

  return false;
}

struct StufferAccepter:Continuation
{

  StufferAccepter():Continuation(NULL)
  {
    SET_HANDLER(&StufferAccepter::mainEvent);
  }

  int mainEvent(int event, void *data)
  {
    //Debug("stuffer", "received a connection");
    ink_assert(event == NET_EVENT_ACCEPT);
    NetVConnection *netvc = (NetVConnection *) data;

    if (connAllowed(netvc->get_remote_ip())) {
      stufferAllocator.alloc()->init(netvc);
    } else {
      inku32 ip = netvc->get_remote_ip();
      unsigned char *str = (unsigned char *) &ip;
      Debug("stuffer", "rejecting connection from %d.%d.%d.%d", str[0], str[1], str[2], str[3]);
      netvc->do_io_close();
    }

    return EVENT_DONE;
  }
};


#define MAX_PARENTS 64
static int
readIPs(ParentRecord * parentRec, inku32 * ip_arr, int max)
{
  if (!parentRec)
    return 0;

  int n = 0;

  pRecord *pr = parentRec->parents;
  int n_parents = parentRec->num_parents;

  for (int i = 0; i < n_parents && n < max; i++) {

#if !defined(VxWorks)
    ink_gethostbyname_r_data data;
    struct hostent *ent = ink_gethostbyname_r(pr[i].hostname, &data);
    if (ent)
      ip_arr[n++] = *(inku32 *) ent->h_addr_list[0];
#else
    ip_arr[n++] = inet_addr(pr[i].hostname);
#endif
  }

  return n;
}

static void
buildParentIPTable()
{
  inku32 ips[MAX_PARENTS];
  int n = 0;

  ParentConfigParams *params = ParentConfig::acquire();

  /* there is no simple way to get the parent ip addresses.
     we will dig through the structures */

  n += readIPs(params->DefaultParent, &ips[n], MAX_PARENTS - n);

#define READ_IPS(x) if (x) \
	n += readIPs((x)->data_array, &ips[n], MAX_PARENTS-n)

  READ_IPS(params->ParentTable->reMatch);
  READ_IPS(params->ParentTable->hostMatch);
  READ_IPS(params->ParentTable->ipMatch);

#undef READ_IPS

  ParentConfig::release(params);

  stuffer_num_parents = n;
  if (n > 0) {
    stuffer_parent_ip_array = (inku32 *) xmalloc(n * sizeof(inku32));
    memcpy(stuffer_parent_ip_array, &ips[0], n * sizeof(inku32));
    for (int i = 0; i < n; i++) {
      unsigned char *str = (unsigned char *) &ips[i];
      Debug("stuffer_parent_ips", "parent ip [%d] = %d.%d.%d.%d", i, str[0], str[1], str[2], str[3]);
    }
  }

  return;
}

void
StufferInitialize(void)
{
  stuffer_htable = NEW(new StufferHashTable(512));

  int stuffer_port;
  ReadConfigInteger(stuffer_port, proxy_config_stuffer_port);

  Debug("stuffer", "stuffer initialized (port = %d%s)", stuffer_port, stuffer_port ? "" : " accept disabled");

  buildParentIPTable();

#ifdef VxWorks
  extern void *ts_udp_receiver(void *);
  Debug("stuffer", "starting udp listener");
  ink_thread_create(ts_udp_receiver, (void *) stuffer_port);
#endif

  if (stuffer_port > 0)
    netProcessor.main_accept(NEW(new StufferAccepter), NO_FD, stuffer_port);
}

ClassAllocator<Stuffer> stufferAllocator("stufferAllocator");
ClassAllocator<StufferCacheWriter> stufferCacheWriterAllocator("stufferCacheWriterAllocator");

inline void
StufferCacheWriter::init(Stuffer * s, int ntowrite)
{
  mutex = s->mutex;
  SET_HANDLER(&StufferCacheWriter::mainEvent);

  stuffer = s;

  buf = new_MIOBuffer(BUFFER_SIZE_INDEX_128);
  reader = buf->alloc_reader();

  ntodo = ntowrite;
}

inline int
StufferCacheWriter::addData(int max)
{
  int nwritten = buf->write(stuffer->reader, max);
  nadded += nwritten;
  return nwritten;
}

inline void
Stuffer::reset()
{
  ink_assert(cur_ntodo == 0);
  state = STUFFER_START;
}

void
Stuffer::free()
{
  if (active_cache_writers > 0) {
    mainEvent(EVENT_INTERVAL, NULL);
    return;
  }

  ink_assert(active_cache_buffer == 0);
  if (buf)
    free_MIOBuffer(buf);

  ink_assert(!source_vc);
  if (source_vc)
    source_vc->do_io_close();

  stufferAllocator.free(this);
}

inline int
Stuffer::processInitialData()
{
  cur_ntodo = -1;
  int nbytes_avail = reader->read_avail();

  if (nbytes_avail < KEEPALIVE_LEN_BYTES + 3)
    return STUFFER_START;

  int size;
  ink_assert(KEEPALIVE_LEN_BYTES == sizeof(size));
  reader->read((char *) &size, sizeof(size));
  cur_ntodo = ntohl(size) - KEEPALIVE_LEN_BYTES;
  Debug("stuffer_keepalive", "cur doc size = %d", cur_ntodo);
  INCREMENT_DYN_STAT(stuffer_total_bytes_received);

  char cbuf[3];
  reader->memcpy(cbuf, 3);

  if (strncmp(cbuf, "GET", 3) != 0)
    return URL_PROMISES;

  return URL_OBJECT;
}

int
Stuffer::mainEvent(int event, void *data)
{
  //if (data && (event == VC_EVENT_READ_READY || event == VC_EVENT_EOS))
  //Debug("stuffer_keepalive", "ndone = %d", source_vio->ndone);

  switch (event) {

  case NET_EVENT_ACCEPT:
    Debug("stuffer", "accepted a new connetion on stuffer port");
    buf = new_MIOBuffer();
    reader = buf->alloc_reader();
    buf->water_mark = buf->block_write_avail();

    source_vio = source_vc->do_io_read(this, INT_MAX, buf);
    break;

  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
    source_vc->do_io_close();
    source_vc = 0;

  process_read_ready:          //this label reduces recursion
  case VC_EVENT_READ_READY:
    if (state == STUFFER_START)
      state = processInitialData();

    switch (state) {

    case URL_PROMISES:{
        int null_pos;

        while ((null_pos = reader->memchr(0)) >= 0) {
          null_pos++;

          char *str = (char *) xmalloc(null_pos);
          if (str) {
            reader->read(str, null_pos);
            stuffer_htable->add(str);
          } else
            reader->consume(null_pos);

          cur_ntodo -= null_pos;
          if (cur_ntodo <= 0) {
            INCREMENT_DYN_STAT(stuffer_total_promises);
            reset();
            goto process_read_ready;
          }
        }
        //bug: if a url is larger than block size this will
        //stop reading after 1 block
        break;
      }

    case URL_OBJECT:
      if (active_cache_writers >= MAX_CACHE_WRITERS_OUTSTANDING) {
        Debug("stuffer_temp", "%d cache writers already active\n", active_cache_writers);
        return EVENT_DONE;
      }

      INCREMENT_DYN_STAT(stuffer_total_objects);
      cache_writer = stufferCacheWriterAllocator.alloc();
      cache_writer->init(this, cur_ntodo);
      active_cache_writers++;
      state = CACHE_WRITE;

      //fall through

    case CACHE_WRITE:{
        if (active_cache_buffer >= MAX_KEEPALIVE_BUFFER && active_cache_writers > 1) {
          Debug("stuffer_temp", "outstandig buffer(%d) exceeds the " "limit.. throttling", active_cache_buffer);
          return EVENT_DONE;
        }

        int nwritten = cache_writer->addData(cur_ntodo);
        reader->consume(nwritten);
        cur_ntodo -= nwritten;
        active_cache_buffer += nwritten;

        if (cur_ntodo > 0) {
          if (nwritten > 0)
            cache_writer->mainEvent(VC_EVENT_READ_READY, NULL);
        } else {
          StufferCacheWriter *temp = cache_writer;
          cache_writer = 0;
          reset();
          temp->mainEvent(VC_EVENT_READ_COMPLETE, NULL);
          goto process_read_ready;
        }
      }
    }                           //switch(state);

    break;

  case EVENT_IMMEDIATE:{
      //one of the cache_writers called us back
      int nadded = (int) data;
      active_cache_buffer -= nadded;
      active_cache_writers--;
      ink_assert(active_cache_buffer >= 0 && active_cache_writers >= 0);
      goto process_read_ready;
    }

  case EVENT_INTERVAL:
    if (active_cache_writers > 0)
      this_ethread()->schedule_imm(this, ink_hrtime_from_msec(10));
    else
      free();
    return EVENT_DONE;

  default:
    ink_assert(!"unexpected event");
    free();
    return EVENT_DONE;
  }

  if (source_vc)
    source_vio->reenable();
  else {
    ink_assert(cur_ntodo < 0 || !reader->read_avail());
    Debug("stuffer_keepalive", "closing keepalive connection " "(read_avail: %d)", reader->read_avail());

    if (cache_writer) {
      cache_writer->mainEvent(VC_EVENT_READ_COMPLETE, NULL);
      cache_writer = 0;
    }
    free();
  }

  return EVENT_CONT;
}

inline void
StufferCacheWriter::initCacheLookupConfig()
{
  //we should be able to use a static variable and initialize only once.
  //The look up parameters are intialized in the same as it is done
  //in HttpSM::init(). Any changes there should come in here.
  HttpConfigParams *http_config_params = HttpConfig::acquire();

  cache_lookup_config.cache_global_user_agent_header = http_config_params->global_user_agent_header ? true : false;
  cache_lookup_config.cache_enable_default_vary_headers =
    http_config_params->cache_enable_default_vary_headers ? true : false;
  cache_lookup_config.cache_vary_default_text = http_config_params->cache_vary_default_text;
  cache_lookup_config.cache_vary_default_images = http_config_params->cache_vary_default_images;
  cache_lookup_config.cache_vary_default_other = http_config_params->cache_vary_default_other;

  HttpConfig::release(http_config_params);
}

void
StufferCacheWriter::free()
{
  if (url) {
    StufferURLPromise *p = stuffer_htable->lookup(url);
    if (p)
      p->free(true);
    xfree(url);
    url = 0;
  }

  ink_assert(!cache_vc);

  if (http_info.valid())
    http_info.destroy();
  http_parser_clear(&http_parser);

  free_MIOBuffer(buf);

  stuffer->mainEvent(EVENT_IMMEDIATE, (void *) nadded);

  mutex = NULL;
  stufferCacheWriterAllocator.free(this);
}

inline bool
responseIsNewer(HTTPHdr * old_resp, HTTPHdr * new_resp)
{
  time_t old_lm = old_resp->get_last_modified();
  time_t new_lm = new_resp->get_last_modified();

  /*debug only stuff: to be removed */
  //Debug("stuffer_cache", "new last modified : %d old : %d "
  //"(need to replace: %d)", new_lm, old_lm, new_lm > old_lm);

  if (new_lm > old_lm)
    return true;

  if ((old_lm = old_resp->get_expires()) && old_lm < ink_cluster_time())
    return true;

  return false;
}

int
StufferCacheWriter::mainEvent(int event, void *data)
{

  switch (event) {

  case VC_EVENT_READ_COMPLETE:{
      //Debug("stuffer_keepalive", "Writer got READ_COMPLETE");
      got_read_complete = 1;

      int nread_avail = reader->read_avail();
      ink_assert(nread_avail <= ntodo);
      ntodo = nread_avail;

      if (cache_vc)
        cache_vio->nbytes = cache_vio->ndone + nread_avail;
    }
    //fall through

  case VC_EVENT_READ_READY:
    //if (!got_read_complete)
    //Debug("stuffer_keepalive", "Writer got READ_READY");

    switch (state) {

    case PARSE_HEADERS:{

        int rc = parseHeaders();

        if (rc == PARSE_CONT) {
          if (got_read_complete)
            free();
        } else if (rc == PARSE_DONE &&
                   HttpTransactHeaders::does_server_allow_response_to_be_stored(&http_info.m_alt->m_response_hdr)) {
          URL u;
          HTTPHdr *request = &http_info.m_alt->m_request_hdr;
          request->url_get(&u);
          ink_time_t now = ink_cluster_time();
          http_info.request_sent_time_set(now);
          http_info.response_received_time_set(now);

          state = CACHE_READ_OPEN;
          cacheProcessor.open_read(this, &u, request, &cache_lookup_config, 0);
        } else {
          //rc == PARSE_ERROR || object_not_cacheable
          state = CACHE_WRITE;
          goto check_vc_n_break;
        }
        break;
      }

    check_vc_n_break:
    case CACHE_WRITE:
      if (cache_vc)
        cache_vio->reenable();
      else if (got_read_complete)
        free();
      else
        reader->consume(reader->read_avail());

      break;

    }                           //switch(state)

    break;

  case CACHE_EVENT_OPEN_READ:{
      //do some simple checks to see if we need to push the object
      //right now, just dont push it.
      //Debug("stuffer_cache", "open_read succeded (%s)\n", url);

      open_read_vc = (CacheVC *) data;
      CacheHTTPInfo *cached_http_info;
      open_read_vc->get_http_info(&cached_http_info);

      bool needs_update = responseIsNewer(&cached_http_info->m_alt->m_response_hdr,
                                          &http_info.m_alt->m_response_hdr);
      if (!needs_update) {
        open_read_vc->do_io_close();
        open_read_vc = 0;
        state = CACHE_WRITE;
        goto check_vc_n_break;
      }
      http_info.m_alt->m_response_hdr.field_delete(MIME_FIELD_SET_COOKIE, MIME_LEN_SET_COOKIE);
    }
  case CACHE_EVENT_OPEN_READ_FAILED:{
      //Debug("stuffer_cache", "open_read failed (%s)\n", url);

      URL u;
      HTTPHdr *request = &http_info.m_alt->m_request_hdr;
      request->url_get(&u);

      CacheHTTPInfo *cached_http_info = 0;
      if (open_read_vc)
        open_read_vc->get_http_info(&cached_http_info);

      state = CACHE_WRITE_OPEN;
      cacheProcessor.open_write(this, 0, &u, request, cached_http_info);
      break;
    }

  case CACHE_EVENT_OPEN_WRITE:
    state = CACHE_WRITE;

    if (ntodo > 0) {
      if (open_read_vc)
        open_read_vc->do_io_close();

      cache_vc = (CacheVC *) data;
      cache_vc->set_http_info(&http_info);

      cache_vio = cache_vc->do_io_write(this, ntodo, reader);

      INCREMENT_DYN_STAT(stuffer_total_objects_pushed);
      break;
    }

  case CACHE_EVENT_OPEN_WRITE_FAILED:
    if (open_read_vc)
      open_read_vc->do_io_close();
    state = CACHE_WRITE;
    goto check_vc_n_break;

  case VC_EVENT_WRITE_READY:
    break;

  case VC_EVENT_WRITE_COMPLETE:
    ink_assert(got_read_complete);
  default:
    cache_vc->do_io_close();
    cache_vc = 0;
    goto check_vc_n_break;
  }

  return EVENT_CONT;
}

int
StufferCacheWriter::parseHeaders()
{
  int ret = PARSE_CONT;
  int nbytes_used;

  if (parse_state == PARSE_START) {
    http_info.create();
    http_info.m_alt->m_request_hdr.create(HTTP_TYPE_REQUEST);
    http_info.m_alt->m_response_hdr.create(HTTP_TYPE_RESPONSE);
    //http_parser_clear(&http_parser);

    parse_state = PARSE_REQ;
  }

  if (parse_state == PARSE_REQ && reader->read_avail()) {
    HTTPHdr *request = &http_info.m_alt->m_request_hdr;
    ret = request->parse_req(&http_parser, reader, &nbytes_used, false);
    ntodo -= nbytes_used;

    if (ret == PARSE_DONE) {
      parse_state = PARSE_RESP;
      ret = PARSE_CONT;
      http_parser_clear(&http_parser);

      url = request->url_get()->string_get(NULL);
      Debug("stuffer_urls", "extracted url %s from the object", url);
    }
  }

  if (parse_state == PARSE_RESP && reader->read_avail() > 0) {
    ret = http_info.m_alt->m_response_hdr.parse_resp(&http_parser, reader, &nbytes_used, false);
    ntodo -= nbytes_used;
  }
  return ret;
}

ClassAllocator<StufferURLPromise> stufferURLPromiseAllocator("stufferURLPromiseAllocator");

void
StufferURLPromise::free(bool obj_pushed)
{
  if (overall_timeout)
    overall_timeout->cancel();
  if (cache_block_timeout)
    cache_block_timeout->cancel();

  if (head.cache_vc) {
    Debug("stuffer_cache", "waking up cache_vcs waiting on %s", url);
    head.cache_vc->stuffer_cache_reenable(obj_pushed ? EVENT_DONE : EVENT_CONT);
  }
  // inform the dynamically allocated ones
  while (head.next) {
    head.next->cache_vc->stuffer_cache_reenable(obj_pushed ? EVENT_DONE : EVENT_CONT);
    cache_obj_list *temp = head.next;
    head.next = temp->next;
    delete temp;
  }

  stuffer_htable->remove(this);
  xfree(url);
  stufferURLPromiseAllocator.free(this);
}

void
StufferURLPromise::add_waiter(CacheVC * cache_vc)
{
  if (!head.cache_vc)           // common case
    head.cache_vc = cache_vc;
  else {
    cache_obj_list *new_elem = NEW(new cache_obj_list);
    new_elem->cache_vc = cache_vc;
    new_elem->next = head.next;
    head.next = new_elem;
  }
  if (!cache_block_timeout)
    cache_block_timeout = this_ethread()->schedule_in(this, ink_hrtime_from_msec(STUFFER_CACHE_BLOCK_TIMEOUT_MSECS));
}

int
StufferURLPromise::mainEvent(int event, void *data)
{
  ink_assert(event == EVENT_INTERVAL);
  Debug("stuffer_timeouts", "%s timeout expired for promise", (data == overall_timeout) ? "overall" : "cache block");
  if (data == overall_timeout)
    overall_timeout = NULL;
  else if (data == cache_block_timeout)
    cache_block_timeout = NULL;
  free();
  return EVENT_DONE;
}

int
StufferCacheIncomingRequest(CacheVC * cache_vc)
{
  /* extract url out of this vc.
     use local buffer to avoid another xmalloc(), otherwise we could just
     use url_obj->string_get() */
#define BUF_SIZE 512
  char url[BUF_SIZE];
  int len, index = 0, offset = 0;

  /* check if this open_read is from a StufferCacheWriter */
  //if (dynamic_cast<StufferCacheWriter *>(cache_vc->_action.continuation))
  if (STUFFER_CACHE_WRITER(cache_vc->_action.continuation))
    return EVENT_CONT;

  URL *url_obj = cache_vc->request.url_get();

  if ((len = url_obj->length_get()) >= BUF_SIZE)
    return EVENT_CONT;
  url_obj->print(url, BUF_SIZE - 1, &index, &offset);
  url[len] = 0;

  StufferURLPromise *promise;
  ProxyMutex *mutex = cache_vc->mutex;  //for DYN_STATS

  MUTEX_TRY_LOCK(lock, stuffer_htable->mutex, this_ethread());

  if (!lock || ((promise = stuffer_htable->lookup(url)) == NULL)) {
    Debug("stuffer_cache", "informing cache: not expecting %s", url);
    INCREMENT_DYN_STAT(stuffer_url_lookup_misses);
    return EVENT_CONT;
  }

  INCREMENT_DYN_STAT(stuffer_open_read_blocks);
  Debug("stuffer_cache", "informing cache: %s is exptected", url);
  promise->add_waiter(cache_vc);
  return EVENT_DONE;
#undef BUF_SIZE
}

//simple hash_table implementation      
int
StufferHashTable::index(const char *s)
{
  int l = 1, len = strlen(s);
  unsigned int hash_value = 0, i = 0;

  while (l <= len) {
    i = (i << 8) | s[len - l];
    if (l % sizeof(int) == 0)
      hash_value ^= i;
    l++;
  }
  //we can neglect the first 3 letters in the worst case.. they are same

  return hash_value % size;
}

StufferURLPromise **
StufferHashTable::position(const char *url)
{
  StufferURLPromise **e = &array[index(url)];

  while (*e) {
    if (strcmp((*e)->url, url) == 0)
      return e;
    e = &(*e)->next;
  }
  return e;
}

void
StufferHashTable::add(char *url)
{
  StufferURLPromise **e = position(url);
  if (*e) {
    //right now we just neglect the URL
    xfree(url);
    return;
  }

  Debug("stuffer_urls", "adding promise %s to the table", url);
  StufferURLPromise *up = stufferURLPromiseAllocator.alloc();
  up->init(url);
  *e = up;
}

void
StufferHashTable::remove(StufferURLPromise * p)
{
  StufferURLPromise **e = position(p->url);
  ink_assert(p == *e);

  Debug("stuffer_urls", "removing promise %s from the list", p->url);
  *e = p->next;
}
