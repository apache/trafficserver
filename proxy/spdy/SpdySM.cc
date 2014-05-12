/** @file

  SpdySM.cc

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

#include "SpdySM.h"
#include "I_Net.h"

ClassAllocator<SpdySM> spdySMAllocator("SpdySMAllocator");
ClassAllocator<SpdyRequest> spdyRequestAllocator("SpdyRequestAllocator");

static int spdy_main_handler(TSCont contp, TSEvent event, void *edata);
static int spdy_start_handler(TSCont contp, TSEvent event, void *edata);
static int spdy_default_handler(TSCont contp, TSEvent event, void *edata);
static int spdy_process_read(TSEvent event, SpdySM *sm);
static int spdy_process_write(TSEvent event, SpdySM *sm);
static int spdy_process_fetch(TSEvent event, SpdySM *sm, void *edata);
static int spdy_process_fetch_header(TSEvent event, SpdySM *sm, TSFetchSM fetch_sm);
static int spdy_process_fetch_body(TSEvent event, SpdySM *sm, TSFetchSM fetch_sm);
static uint64_t g_sm_id;
static uint64_t g_sm_cnt;

void
SpdyRequest::clear()
{
  if (fetch_sm)
    TSFetchDestroy(fetch_sm);

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
SpdySM::init(NetVConnection * netvc)
{
  int version, r;

  atomic_inc(g_sm_cnt);

  this->vc = netvc;
  this->req_map.clear();

  // XXX this has to die ... TS-2793
  UnixNetVConnection * unixvc = reinterpret_cast<UnixNetVConnection *>(netvc);

  if (unixvc->selected_next_protocol == TS_NPN_PROTOCOL_SPDY_3_1)
    version = SPDYLAY_PROTO_SPDY3_1;
  else if (unixvc->selected_next_protocol == TS_NPN_PROTOCOL_SPDY_3)
    version = SPDYLAY_PROTO_SPDY3;
  else if (unixvc->selected_next_protocol == TS_NPN_PROTOCOL_SPDY_2)
    version = SPDYLAY_PROTO_SPDY2;
  else
    version = SPDYLAY_PROTO_SPDY3;

  r = spdylay_session_server_new(&session, version,
                                 &SPDY_CFG.spdy.callbacks, this);
  ink_release_assert(r == 0);
  sm_id = atomic_inc(g_sm_id);
  total_size = 0;
  start_time = TShrtime();

  ink_assert(this->contp == NULL);
  this->contp = TSContCreate(spdy_main_handler, TSMutexCreate());
  TSContDataSet(this->contp, this);

  this->vc->set_inactivity_timeout(HRTIME_SECONDS(SPDY_CFG.accept_no_activity_timeout));
  this->current_handler = &spdy_start_handler;
}

void
SpdySM::clear()
{
  uint64_t nr_pending;
  int last_event = event;
  //
  // SpdyRequest depends on SpdySM,
  // we should delete it firstly to avoid race.
  //
  map<int, SpdyRequest*>::iterator iter = req_map.begin();
  map<int, SpdyRequest*>::iterator endIter = req_map.end();
  for(; iter != endIter; ++iter) {
    SpdyRequest *req = iter->second;
    if (req) {
      req->clear();
      spdyRequestAllocator.free(req);
    } else {
      Error("req null in SpdSM::clear");
    }
  }
  req_map.clear();

  if (vc) {
    TSVConnClose(reinterpret_cast<TSVConn>(vc));
    vc = NULL;
  }

  if (contp) {
    TSContDestroy(contp);
    contp = NULL;
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

  nr_pending = atomic_dec(g_sm_cnt);
  Debug("spdy-free", "****Delete SpdySM[%" PRIu64 "], last event:%d, nr_pending:%" PRIu64,
        sm_id, last_event, --nr_pending);
}

void
spdy_sm_create(NetVConnection * netvc, MIOBuffer * iobuf, IOBufferReader * reader)
{
  SpdySM  *sm;

  sm = spdySMAllocator.alloc();
  sm->init(netvc);

  sm->req_buffer = iobuf ? reinterpret_cast<TSIOBuffer>(iobuf) : TSIOBufferCreate();
  sm->req_reader = reader ? reinterpret_cast<TSIOBufferReader>(reader) : TSIOBufferReaderAlloc(sm->req_buffer);

  sm->resp_buffer = TSIOBufferCreate();
  sm->resp_reader = TSIOBufferReaderAlloc(sm->resp_buffer);

  TSContSchedule(sm->contp, 0, TS_THREAD_POOL_DEFAULT);       // schedule now
}

static int
spdy_main_handler(TSCont contp, TSEvent event, void *edata)
{
  SpdySM          *sm;
  SpdySMHandler   spdy_current_handler;

  sm = (SpdySM*)TSContDataGet(contp);
  spdy_current_handler = sm->current_handler;

  return (*spdy_current_handler) (contp, event, edata);
}

static int
spdy_start_handler(TSCont contp, TSEvent /*event*/, void * /*data*/)
{
  int     r;
  spdylay_settings_entry entry;

  SpdySM  *sm = (SpdySM*)TSContDataGet(contp);

  if (TSIOBufferReaderAvail(sm->req_reader) > 0) {
    spdy_process_read(TS_EVENT_VCONN_WRITE_READY, sm);
  }

  sm->read_vio = (TSVIO)sm->vc->do_io_read(reinterpret_cast<Continuation *>(contp), INT64_MAX, reinterpret_cast<MIOBuffer *>(sm->req_buffer));
  sm->write_vio = (TSVIO)sm->vc->do_io_write(reinterpret_cast<Continuation *>(contp), INT64_MAX, reinterpret_cast<IOBufferReader *>(sm->resp_reader));

  sm->current_handler = &spdy_default_handler;

  /* send initial settings frame */
  entry.settings_id = SPDYLAY_SETTINGS_MAX_CONCURRENT_STREAMS;
  entry.value = SPDY_CFG.spdy.max_concurrent_streams;
  entry.flags = SPDYLAY_ID_FLAG_SETTINGS_NONE;

  r = spdylay_submit_settings(sm->session, SPDYLAY_FLAG_SETTINGS_NONE, &entry, 1);
  TSAssert(r == 0);

  TSVIOReenable(sm->write_vio);
  return 0;
}

static int
spdy_default_handler(TSCont contp, TSEvent event, void *edata)
{
  int ret = 0;
  bool from_fetch = false;
  SpdySM  *sm = (SpdySM*)TSContDataGet(contp);
  sm->event = event;

  if (edata == sm->read_vio) {
    Debug("spdy", "++++[READ EVENT]");
    if (event != TS_EVENT_VCONN_READ_READY &&
        event != TS_EVENT_VCONN_READ_COMPLETE) {
      ret = -1;
      goto out;
    }
    ret = spdy_process_read(event, sm);
  } else if (edata == sm->write_vio) {
    Debug("spdy", "----[WRITE EVENT]");
    if (event != TS_EVENT_VCONN_WRITE_READY &&
        event != TS_EVENT_VCONN_WRITE_COMPLETE) {
      ret = -1;
      goto out;
    }
    ret = spdy_process_write(event, sm);
  } else {
    from_fetch = true;
    ret = spdy_process_fetch(event, sm, edata);
  }

  Debug("spdy-event", "++++SpdySM[%" PRIu64 "], EVENT:%d, ret:%d, nr_pending:%" PRIu64,
        sm->sm_id, event, ret, g_sm_cnt);
out:
  if (ret) {
    sm->clear();
    spdySMAllocator.free(sm);
  } else if (!from_fetch) {
    sm->vc->set_inactivity_timeout(HRTIME_SECONDS(SPDY_CFG.no_activity_timeout_in));
  }

  return 0;
}

static int
spdy_process_read(TSEvent /* event ATS_UNUSED */, SpdySM *sm)
{
  return spdylay_session_recv(sm->session);
}

static int
spdy_process_write(TSEvent /* event ATS_UNUSED */, SpdySM *sm)
{
  int ret;

  ret = spdylay_session_send(sm->session);

  if (TSIOBufferReaderAvail(sm->resp_reader) > 0)
    TSVIOReenable(sm->write_vio);
  else {
    Debug("spdy", "----TOTAL SEND (sm_id:%" PRIu64 ", total_size:%" PRIu64 ", total_send:%" PRId64 ")",
          sm->sm_id, sm->total_size, TSVIONDoneGet(sm->write_vio));

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
spdy_process_fetch(TSEvent event, SpdySM *sm, void *edata)
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
    else
      req->fetch_sm = NULL;
    break;
  }

  if (ret) {
    spdy_prepare_status_response(sm, req->stream_id, STATUS_500);
    sm->req_map.erase(req->stream_id);
    req->clear();
    spdyRequestAllocator.free(req);
  }

  return 0;
}

static int
spdy_process_fetch_header(TSEvent /*event*/, SpdySM *sm, TSFetchSM fetch_sm)
{
  int ret;
  SpdyRequest *req = (SpdyRequest *)TSFetchUserDataGet(fetch_sm);
  SpdyNV spdy_nv(fetch_sm);

  Debug("spdy", "----spdylay_submit_syn_reply");
  ret = spdylay_submit_syn_reply(sm->session,
                                 SPDYLAY_CTRL_FLAG_NONE, req->stream_id,
                                 spdy_nv.nv);

  TSVIOReenable(sm->write_vio);
  return ret;
}

static ssize_t
spdy_read_fetch_body_callback(spdylay_session * /*session*/, int32_t stream_id,
                              uint8_t *buf, size_t length, int *eof,
                              spdylay_data_source *source, void *user_data)
{

  static int g_call_cnt;
  int64_t already;

  SpdySM *sm = (SpdySM *)user_data;
  SpdyRequest *req = (SpdyRequest *)source->ptr;

  //
  // req has been deleted, ignore this data.
  //
  if (req != sm->req_map[stream_id]) {
    Debug("spdy", "    stream_id:%d, call:%d, req has been deleted, return 0",
          stream_id, g_call_cnt);
    *eof = 1;
    return 0;
  }

  already = TSFetchReadData(req->fetch_sm, buf, length);

  Debug("spdy", "    stream_id:%d, call:%d, length:%ld, already:%" PRId64,
        stream_id, g_call_cnt, length, already);
  if (SPDY_CFG.spdy.verbose)
    MD5_Update(&req->recv_md5, buf, already);

  TSVIOReenable(sm->write_vio);
  g_call_cnt++;

  req->fetch_data_len += already;
  if (already < (int64_t)length) {
    if (req->event == TS_FETCH_EVENT_EXT_BODY_DONE) {
      TSHRTime end_time = TShrtime();
      Debug("spdy", "----Request[%" PRIu64 ":%d] %s %lld %d", sm->sm_id, req->stream_id,
            req->url.c_str(), (end_time - req->start_time)/TS_HRTIME_MSECOND,
            req->fetch_data_len);
      unsigned char digest[MD5_DIGEST_LENGTH];
      if (SPDY_CFG.spdy.verbose ) {
        MD5_Final(digest, &req->recv_md5);
        Debug("spdy", "----recv md5sum: ");
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
          Debug("spdy", "%02x", digest[i]);
        }
      }
      *eof = 1;
      sm->req_map.erase(stream_id);
      req->clear();
      spdyRequestAllocator.free(req);
    } else if (already == 0) {
      req->need_resume_data = true;
      return SPDYLAY_ERR_DEFERRED;
    }
  }

  return already;
}

static int
spdy_process_fetch_body(TSEvent event, SpdySM *sm, TSFetchSM fetch_sm)
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
    ret = spdylay_submit_data(sm->session, req->stream_id,
                              SPDYLAY_DATA_FLAG_FIN, &data_prd);
  } else if (req->need_resume_data) {
    Debug("spdy", "----spdylay_session_resume_data");
    ret = spdylay_session_resume_data(sm->session, req->stream_id);
    if (ret == SPDYLAY_ERR_INVALID_ARGUMENT)
      ret = 0;
  }

  TSVIOReenable(sm->write_vio);
  return ret;
}
