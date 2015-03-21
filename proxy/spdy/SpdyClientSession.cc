/** @file

  SpdyClientSession.cc

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

#include "SpdyClientSession.h"
#include "I_Net.h"

static ClassAllocator<SpdyClientSession> spdyClientSessionAllocator("spdyClientSessionAllocator");
ClassAllocator<SpdyRequest> spdyRequestAllocator("spdyRequestAllocator");

#if TS_HAS_SPDY
#include "SpdyClientSession.h"

static const spdylay_proto_version versmap[] = {
  SPDYLAY_PROTO_SPDY2,   // SPDY_VERSION_2
  SPDYLAY_PROTO_SPDY3,   // SPDY_VERSION_3
  SPDYLAY_PROTO_SPDY3_1, // SPDY_VERSION_3_1
};

static char const *const npnmap[] = {TS_NPN_PROTOCOL_SPDY_2, TS_NPN_PROTOCOL_SPDY_3, TS_NPN_PROTOCOL_SPDY_3_1};

#endif
static int spdy_process_read(TSEvent event, SpdyClientSession *sm);
static int spdy_process_write(TSEvent event, SpdyClientSession *sm);
static int spdy_process_fetch(TSEvent event, SpdyClientSession *sm, void *edata);
static int spdy_process_fetch_header(TSEvent event, SpdyClientSession *sm, TSFetchSM fetch_sm);
static int spdy_process_fetch_body(TSEvent event, SpdyClientSession *sm, TSFetchSM fetch_sm);
static uint64_t g_sm_id = 1;

void
SpdyRequest::init(SpdyClientSession *sm, int id)
{
  spdy_sm = sm;
  stream_id = id;
  headers.clear();

  MD5_Init(&recv_md5);
  start_time = TShrtime();

  SPDY_INCREMENT_THREAD_DYN_STAT(SPDY_STAT_CURRENT_CLIENT_STREAM_COUNT, sm->mutex->thread_holding);
}

void
SpdyRequest::clear()
{
  if (!spdy_sm)
    return; // this object wasn't initialized.

  SPDY_DECREMENT_THREAD_DYN_STAT(SPDY_STAT_CURRENT_CLIENT_STREAM_COUNT, spdy_sm->mutex->thread_holding);

  if (fetch_sm) {
    TSFetchDestroy(fetch_sm);
    fetch_sm = NULL;
  }

  vector<pair<string, string> >().swap(headers);

  std::string().swap(url);
  std::string().swap(host);
  std::string().swap(path);
  std::string().swap(scheme);
  std::string().swap(method);
  std::string().swap(version);

  Debug("spdy", "****Delete Request[%" PRIu64 ":%d]", spdy_sm->sm_id, stream_id);
}

void
SpdyClientSession::init(NetVConnection *netvc, spdy::SessionVersion vers)
{
  int r;

  this->mutex = new_ProxyMutex();
  this->vc = netvc;
  this->req_map.clear();
  this->version = vers;

  r = spdylay_session_server_new(&session, versmap[vers], &spdy_callbacks, this);

  // A bit ugly but we need a thread and I don't want to wait until the
  // session start event in case of a time out generating a decrement
  // with no increment. It seems a lesser thing to have the thread counts
  // a little off but globally consistent.
  SPDY_INCREMENT_THREAD_DYN_STAT(SPDY_STAT_CURRENT_CLIENT_SESSION_COUNT, netvc->mutex->thread_holding);
  SPDY_INCREMENT_THREAD_DYN_STAT(SPDY_STAT_TOTAL_CLIENT_CONNECTION_COUNT, netvc->mutex->thread_holding);

  ink_release_assert(r == 0);
  sm_id = atomic_inc(g_sm_id);
  total_size = 0;
  start_time = TShrtime();

  this->vc->set_inactivity_timeout(HRTIME_SECONDS(spdy_accept_no_activity_timeout));
  vc->add_to_keep_alive_lru();
  SET_HANDLER(&SpdyClientSession::state_session_start);
}

void
SpdyClientSession::clear()
{
  int last_event = event;

  SPDY_DECREMENT_THREAD_DYN_STAT(SPDY_STAT_CURRENT_CLIENT_SESSION_COUNT, this->mutex->thread_holding);

  //
  // SpdyRequest depends on SpdyClientSession,
  // we should delete it firstly to avoid race.
  //
  map<int, SpdyRequest *>::iterator iter = req_map.begin();
  map<int, SpdyRequest *>::iterator endIter = req_map.end();
  for (; iter != endIter; ++iter) {
    SpdyRequest *req = iter->second;
    if (req) {
      req->clear();
      spdyRequestAllocator.free(req);
    } else {
      Error("req null in SpdSM::clear");
    }
  }
  req_map.clear();

  this->mutex = NULL;

  if (vc) {
    TSVConnClose(reinterpret_cast<TSVConn>(vc));
    vc = NULL;
  }


  if (req_reader) {
    TSIOBufferReaderFree(req_reader);
    req_reader = NULL;
  }

  if (req_buffer) {
    TSIOBufferDestroy(req_buffer);
    req_buffer = NULL;
  }

  if (resp_reader) {
    TSIOBufferReaderFree(resp_reader);
    resp_reader = NULL;
  }

  if (resp_buffer) {
    TSIOBufferDestroy(resp_buffer);
    resp_buffer = NULL;
  }

  if (session) {
    spdylay_session_del(session);
    session = NULL;
  }

  Debug("spdy-free", "****Delete SpdyClientSession[%" PRIu64 "], last event:%d" PRIu64, sm_id, last_event);
}

void
spdy_cs_create(NetVConnection *netvc, spdy::SessionVersion vers, MIOBuffer *iobuf, IOBufferReader *reader)
{
  SpdyClientSession *sm;

  sm = spdyClientSessionAllocator.alloc();
  sm->init(netvc, vers);

  sm->req_buffer = iobuf ? reinterpret_cast<TSIOBuffer>(iobuf) : TSIOBufferCreate();
  sm->req_reader = reader ? reinterpret_cast<TSIOBufferReader>(reader) : TSIOBufferReaderAlloc(sm->req_buffer);

  sm->resp_buffer = TSIOBufferCreate();
  sm->resp_reader = TSIOBufferReaderAlloc(sm->resp_buffer);

  eventProcessor.schedule_imm(sm, ET_NET);
}

int
SpdyClientSession::state_session_start(int /* event */, void * /* edata */)
{
  const spdylay_settings_entry entries[] = {
    {SPDYLAY_SETTINGS_MAX_CONCURRENT_STREAMS, SPDYLAY_ID_FLAG_SETTINGS_NONE, spdy_max_concurrent_streams},
    {SPDYLAY_SETTINGS_INITIAL_WINDOW_SIZE, SPDYLAY_ID_FLAG_SETTINGS_NONE, spdy_initial_window_size}};
  int r;

  if (TSIOBufferReaderAvail(this->req_reader) > 0) {
    spdy_process_read(TS_EVENT_VCONN_WRITE_READY, this);
  }

  this->read_vio = (TSVIO) this->vc->do_io_read(this, INT64_MAX, reinterpret_cast<MIOBuffer *>(this->req_buffer));
  this->write_vio = (TSVIO) this->vc->do_io_write(this, INT64_MAX, reinterpret_cast<IOBufferReader *>(this->resp_reader));

  SET_HANDLER(&SpdyClientSession::state_session_readwrite);

  r = spdylay_submit_settings(this->session, SPDYLAY_FLAG_SETTINGS_NONE, entries, countof(entries));
  ink_assert(r == 0);

  if (this->version >= spdy::SESSION_VERSION_3_1 && spdy_initial_window_size > (1 << 16)) {
    int32_t delta = (spdy_initial_window_size - SPDYLAY_INITIAL_WINDOW_SIZE);

    r = spdylay_submit_window_update(this->session, 0, delta);
    ink_assert(r == 0);
  }

  TSVIOReenable(this->write_vio);
  return EVENT_CONT;
}

int
SpdyClientSession::state_session_readwrite(int event, void *edata)
{
  int ret = 0;
  bool from_fetch = false;

  this->event = event;

  if (edata == this->read_vio) {
    Debug("spdy", "++++[READ EVENT]");
    if (event != TS_EVENT_VCONN_READ_READY && event != TS_EVENT_VCONN_READ_COMPLETE) {
      ret = -1;
      goto out;
    }
    ret = spdy_process_read((TSEvent)event, this);
  } else if (edata == this->write_vio) {
    Debug("spdy", "----[WRITE EVENT]");
    if (event != TS_EVENT_VCONN_WRITE_READY && event != TS_EVENT_VCONN_WRITE_COMPLETE) {
      ret = -1;
      goto out;
    }
    ret = spdy_process_write((TSEvent)event, this);
  } else {
    from_fetch = true;
    ret = spdy_process_fetch((TSEvent)event, this, edata);
  }

  Debug("spdy-event", "++++SpdyClientSession[%" PRIu64 "], EVENT:%d, ret:%d", this->sm_id, event, ret);
out:
  if (ret) {
    this->clear();
    spdyClientSessionAllocator.free(this);
  } else if (!from_fetch) {
    this->vc->set_inactivity_timeout(HRTIME_SECONDS(spdy_no_activity_timeout_in));
  }

  return EVENT_CONT;
}

int64_t
SpdyClientSession::getPluginId() const
{
  return sm_id;
}

char const *
SpdyClientSession::getPluginTag() const
{
  return npnmap[this->version];
}


static int
spdy_process_read(TSEvent /* event ATS_UNUSED */, SpdyClientSession *sm)
{
  return spdylay_session_recv(sm->session);
}

static int
spdy_process_write(TSEvent /* event ATS_UNUSED */, SpdyClientSession *sm)
{
  int ret;

  ret = spdylay_session_send(sm->session);

  if (TSIOBufferReaderAvail(sm->resp_reader) > 0)
    TSVIOReenable(sm->write_vio);
  else {
    Debug("spdy", "----TOTAL SEND (sm_id:%" PRIu64 ", total_size:%" PRIu64 ", total_send:%" PRId64 ")", sm->sm_id, sm->total_size,
          TSVIONDoneGet(sm->write_vio));

    //
    // We should reenable read_vio when no data to be written,
    // otherwise it could lead to hang issue when client POST
    // data is waiting to be read.
    //
    TSVIOReenable(sm->read_vio);
  }

  return ret;
}

static int
spdy_process_fetch(TSEvent event, SpdyClientSession *sm, void *edata)
{
  int ret = -1;
  TSFetchSM fetch_sm = (TSFetchSM)edata;
  SpdyRequest *req = (SpdyRequest *)TSFetchUserDataGet(fetch_sm);

  switch ((int)event) {
  case TS_FETCH_EVENT_EXT_HEAD_DONE:
    Debug("spdy", "----[FETCH HEADER DONE]");
    ret = spdy_process_fetch_header(event, sm, fetch_sm);
    break;

  case TS_FETCH_EVENT_EXT_BODY_READY:
    Debug("spdy", "----[FETCH BODY READY]");
    ret = spdy_process_fetch_body(event, sm, fetch_sm);
    break;

  case TS_FETCH_EVENT_EXT_BODY_DONE:
    Debug("spdy", "----[FETCH BODY DONE]");
    req->fetch_body_completed = true;
    ret = spdy_process_fetch_body(event, sm, fetch_sm);
    break;

  default:
    Debug("spdy", "----[FETCH ERROR]");
    if (req->fetch_body_completed)
      ret = 0; // Ignore fetch errors after FETCH BODY DONE
    else {
      Debug("spdy_error",
            "spdy_process_fetch fetch error, fetch_sm %p, ret %d for sm_id %" PRId64 ", stream_id %u, req time %" PRId64 ", url %s",
            req->fetch_sm, ret, sm->sm_id, req->stream_id, req->start_time, req->url.c_str());
      req->fetch_sm = NULL;
    }
    break;
  }

  if (ret) {
    Debug("spdy_error", "spdy_process_fetch sending STATUS_500, fetch_sm %p, ret %d for sm_id %" PRId64
                        ", stream_id %u, req time %" PRId64 ", url %s",
          req->fetch_sm, ret, sm->sm_id, req->stream_id, req->start_time, req->url.c_str());
    spdy_prepare_status_response_and_clean_request(sm, req->stream_id, STATUS_500);
  }

  return 0;
}

static int
spdy_process_fetch_header(TSEvent /*event*/, SpdyClientSession *sm, TSFetchSM fetch_sm)
{
  int ret = -1;
  SpdyRequest *req = (SpdyRequest *)TSFetchUserDataGet(fetch_sm);

  SpdyNV spdy_nv(fetch_sm);

  if (!spdy_nv.is_valid_response()) {
    Debug("spdy_error", "----spdy_process_fetch_header, invalid http response");
    return -1;
  }

  Debug("spdy", "----spdylay_submit_syn_reply");
  if (sm->session) {
    ret = spdylay_submit_syn_reply(sm->session, SPDYLAY_CTRL_FLAG_NONE, req->stream_id, spdy_nv.nv);
  } else {
    Error("spdy_process_fetch_header, sm->session NULL, sm_id %" PRId64 ", fetch_sm %p,"
          "stream_id %d, req_time %" PRId64 ", url %s",
          sm->sm_id, fetch_sm, req->stream_id, req->start_time, req->url.c_str());
  }

  TSVIOReenable(sm->write_vio);
  return ret;
}

static ssize_t
spdy_read_fetch_body_callback(spdylay_session * /*session*/, int32_t stream_id, uint8_t *buf, size_t length, int *eof,
                              spdylay_data_source *source, void *user_data)
{
  static int g_call_cnt;
  int64_t already;

  SpdyClientSession *sm = (SpdyClientSession *)user_data;
  SpdyRequest *req = (SpdyRequest *)source->ptr;

  //
  // req has been deleted, ignore this data.
  //
  if (req != sm->find_request(stream_id)) {
    Debug("spdy", "    stream_id:%d, call:%d, req has been deleted, return 0", stream_id, g_call_cnt);
    *eof = 1;
    return 0;
  }

  already = TSFetchReadData(req->fetch_sm, buf, length);

  Debug("spdy", "    stream_id:%d, call:%d, length:%ld, already:%" PRId64, stream_id, g_call_cnt, length, already);
  if (is_debug_tag_set("spdy"))
    MD5_Update(&req->recv_md5, buf, already);

  TSVIOReenable(sm->write_vio);
  g_call_cnt++;

  req->fetch_data_len += already;
  if (already < (int64_t)length) {
    if (req->event == TS_FETCH_EVENT_EXT_BODY_DONE) {
      TSHRTime end_time = TShrtime();
      SPDY_SUM_THREAD_DYN_STAT(SPDY_STAT_TOTAL_TRANSACTIONS_TIME, sm->mutex->thread_holding, end_time - req->start_time);
      Debug("spdy", "----Request[%" PRIu64 ":%d] %s %lld %d", sm->sm_id, req->stream_id, req->url.c_str(),
            (end_time - req->start_time) / TS_HRTIME_MSECOND, req->fetch_data_len);
      if (is_debug_tag_set("spdy")) {
        unsigned char digest[MD5_DIGEST_LENGTH];
        MD5_Final(digest, &req->recv_md5);
        char md5_strbuf[MD5_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
          snprintf(md5_strbuf + (i * 2), 3 /* null byte counts towards the limit */, "%02x", digest[i]);
        }
        Debug("spdy", "----recv md5sum: %s", md5_strbuf);
      }
      *eof = 1;
      sm->cleanup_request(stream_id);
    } else if (already == 0) {
      req->need_resume_data = true;
      return SPDYLAY_ERR_DEFERRED;
    }
  }

  return already;
}

static int
spdy_process_fetch_body(TSEvent event, SpdyClientSession *sm, TSFetchSM fetch_sm)
{
  int ret = 0;
  spdylay_data_provider data_prd;
  SpdyRequest *req = (SpdyRequest *)TSFetchUserDataGet(fetch_sm);
  req->event = event;

  data_prd.source.ptr = (void *)req;
  data_prd.read_callback = spdy_read_fetch_body_callback;

  if (!req->has_submitted_data) {
    req->has_submitted_data = true;
    Debug("spdy", "----spdylay_submit_data");
    ret = spdylay_submit_data(sm->session, req->stream_id, SPDYLAY_DATA_FLAG_FIN, &data_prd);
  } else if (req->need_resume_data) {
    Debug("spdy", "----spdylay_session_resume_data");
    ret = spdylay_session_resume_data(sm->session, req->stream_id);
    if (ret == SPDYLAY_ERR_INVALID_ARGUMENT)
      ret = 0;
  }

  TSVIOReenable(sm->write_vio);
  return ret;
}
