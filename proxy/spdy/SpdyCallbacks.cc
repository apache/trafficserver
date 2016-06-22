/** @file

  SpdyCallbacks.cc

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

#include "SpdyCallbacks.h"
#include "SpdyClientSession.h"
#include <arpa/inet.h>

void
spdy_callbacks_init(spdylay_session_callbacks *callbacks)
{
  memset(callbacks, 0, sizeof(spdylay_session_callbacks));

  callbacks->send_callback                     = spdy_send_callback;
  callbacks->recv_callback                     = spdy_recv_callback;
  callbacks->on_ctrl_recv_callback             = spdy_on_ctrl_recv_callback;
  callbacks->on_invalid_ctrl_recv_callback     = spdy_on_invalid_ctrl_recv_callback;
  callbacks->on_data_chunk_recv_callback       = spdy_on_data_chunk_recv_callback;
  callbacks->on_data_recv_callback             = spdy_on_data_recv_callback;
  callbacks->before_ctrl_send_callback         = spdy_before_ctrl_send_callback;
  callbacks->on_ctrl_send_callback             = spdy_on_ctrl_send_callback;
  callbacks->on_ctrl_not_send_callback         = spdy_on_ctrl_not_send_callback;
  callbacks->on_data_send_callback             = spdy_on_data_send_callback;
  callbacks->on_stream_close_callback          = spdy_on_stream_close_callback;
  callbacks->on_request_recv_callback          = spdy_on_request_recv_callback;
  callbacks->get_credential_proof              = spdy_get_credential_proof;
  callbacks->get_credential_ncerts             = spdy_get_credential_ncerts;
  callbacks->get_credential_cert               = spdy_get_credential_cert;
  callbacks->on_ctrl_recv_parse_error_callback = spdy_on_ctrl_recv_parse_error_callback;
  callbacks->on_unknown_ctrl_recv_callback     = spdy_on_unknown_ctrl_recv_callback;
}

void
spdy_prepare_status_response_and_clean_request(SpdyClientSession *sm, int stream_id, const char *status)
{
  SpdyRequest *req = sm->find_request(stream_id);
  if (!req) {
    Debug("spdy_error", "spdy_prepare_status_response_and_clean_request,"
                        "req object null for sm %" PRId64 ", stream_id %d",
          sm->sm_id, stream_id);
    return;
  }
  string date_str = http_date(time(0));
  const char **nv = new const char *[8 + req->headers.size() * 2 + 1];

  nv[0] = ":status";
  nv[1] = status;
  nv[2] = ":version";
  nv[3] = "HTTP/1.1";
  nv[4] = "server";
  nv[5] = SPDYD_SERVER;
  nv[6] = "date";
  nv[7] = date_str.c_str();

  for (size_t i = 0; i < req->headers.size(); ++i) {
    nv[8 + i * 2]     = req->headers[i].first.c_str();
    nv[8 + i * 2 + 1] = req->headers[i].second.c_str();
  }
  nv[8 + req->headers.size() * 2] = 0;

  int r = spdylay_submit_response(sm->session, stream_id, nv, NULL);
  TSAssert(r == 0);

  TSVIOReenable(sm->write_vio);
  delete[] nv;
  sm->cleanup_request(stream_id);
}

static void
spdy_show_data_frame(const char *head_str, spdylay_session * /*session*/, uint8_t flags, int32_t stream_id, int32_t length,
                     void *user_data)
{
  if (!is_debug_tag_set("spdy"))
    return;

  SpdyClientSession *sm = (SpdyClientSession *)user_data;

  Debug("spdy", "%s DATA frame (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, length:%d)", head_str, sm->sm_id, stream_id, flags,
        length);
}

static void
spdy_show_ctl_frame(const char *head_str, spdylay_session * /*session*/, spdylay_frame_type type, spdylay_frame *frame,
                    void *user_data)
{
  if (!is_debug_tag_set("spdy"))
    return;

  SpdyClientSession *sm = (SpdyClientSession *)user_data;
  switch (type) {
  case SPDYLAY_SYN_STREAM: {
    spdylay_syn_stream *f = (spdylay_syn_stream *)frame;
    Debug("spdy", "%s SYN_STREAM (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, length:%d)", head_str, sm->sm_id, f->stream_id,
          f->hd.flags, f->hd.length);
    int j, i;
    j = i = 0;
    while (f->nv[j]) {
      Debug("spdy", "    %s: %s", f->nv[j], f->nv[j + 1]);
      i++;
      j = 2 * i;
    }
  } break;
  case SPDYLAY_SYN_REPLY: {
    spdylay_syn_reply *f = (spdylay_syn_reply *)frame;
    Debug("spdy", "%s SYN_REPLY (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, length:%d)", head_str, sm->sm_id, f->stream_id,
          f->hd.flags, f->hd.length);
    int j, i;
    j = i = 0;
    while (f->nv[j]) {
      Debug("spdy", "    %s: %s", f->nv[j], f->nv[j + 1]);
      i++;
      j = 2 * i;
    }
  } break;
  case SPDYLAY_WINDOW_UPDATE: {
    spdylay_window_update *f = (spdylay_window_update *)frame;
    Debug("spdy", "%s WINDOW_UPDATE (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, delta_window_size:%u)", head_str, sm->sm_id,
          f->stream_id, f->hd.flags, f->delta_window_size);
  } break;
  case SPDYLAY_SETTINGS: {
    spdylay_settings *f = (spdylay_settings *)frame;
    Debug("spdy", "%s SETTINGS frame (sm_id:%" PRIu64 ", flag:%d, length:%d, niv:%zu)", head_str, sm->sm_id, f->hd.flags,
          f->hd.length, f->niv);
    for (size_t i = 0; i < f->niv; i++) {
      Debug("spdy", "    (%d:%d)", f->iv[i].settings_id, f->iv[i].value);
    }
  } break;
  case SPDYLAY_HEADERS: {
    spdylay_headers *f = (spdylay_headers *)frame;
    Debug("spdy", "%s HEADERS frame (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, length:%d)", head_str, sm->sm_id, f->stream_id,
          f->hd.flags, f->hd.length);
  } break;
  case SPDYLAY_RST_STREAM: {
    spdylay_rst_stream *f = (spdylay_rst_stream *)frame;
    Debug("spdy", "%s RST_STREAM (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, length:%d, code:%d)", head_str, sm->sm_id, f->stream_id,
          f->hd.flags, f->hd.length, f->status_code);
  } break;
  case SPDYLAY_GOAWAY: {
    spdylay_goaway *f = (spdylay_goaway *)frame;
    Debug("spdy", "%s GOAWAY frame (sm_id:%" PRIu64 ", last_good_stream_id:%d, flag:%d, length:%d", head_str, sm->sm_id,
          f->last_good_stream_id, f->hd.flags, f->hd.length);
  }
  default:
    break;
  }
  return;
}

static int
spdy_fetcher_launch(SpdyRequest *req)
{
  string url;
  int fetch_flags;
  const sockaddr *client_addr;
  SpdyClientSession *sm = req->spdy_sm;

  url         = req->scheme + "://" + req->host + req->path;
  client_addr = TSNetVConnRemoteAddrGet(reinterpret_cast<TSVConn>(sm->vc));

  req->url = url;
  Debug("spdy", "++++Request[%" PRIu64 ":%d] %s", sm->sm_id, req->stream_id, req->url.c_str());

  //
  // HTTP content should be dechunked before packed into SPDY.
  //
  fetch_flags = TS_FETCH_FLAGS_DECHUNK;

  // TS-2906: FetchSM sets requests are internal requests, we need to not do that for SPDY streams.
  fetch_flags |= TS_FETCH_FLAGS_NOT_INTERNAL_REQUEST;

  req->fetch_sm = TSFetchCreate((TSCont)sm, req->method.c_str(), url.c_str(), req->version.c_str(), client_addr, fetch_flags);
  TSFetchUserDataSet(req->fetch_sm, (void *)req);

  //
  // Set header list
  //
  for (size_t i = 0; i < req->headers.size(); i++) {
    if (*req->headers[i].first.c_str() == ':')
      continue;

    TSFetchHeaderAdd(req->fetch_sm, req->headers[i].first.c_str(), req->headers[i].first.size(), req->headers[i].second.c_str(),
                     req->headers[i].second.size());
  }

  TSFetchLaunch(req->fetch_sm);
  return 0;
}

ssize_t
spdy_send_callback(spdylay_session * /*session*/, const uint8_t *data, size_t length, int /*flags*/, void *user_data)
{
  SpdyClientSession *sm = (SpdyClientSession *)user_data;

  sm->total_size += length;
  TSIOBufferWrite(sm->resp_buffer, data, length);

  Debug("spdy", "----spdy_send_callback, length:%zu", length);

  return length;
}

ssize_t
spdy_recv_callback(spdylay_session * /*session*/, uint8_t *buf, size_t length, int /*flags*/, void *user_data)
{
  const char *start;
  TSIOBufferBlock blk, next_blk;
  int64_t already, blk_len, need, wavail;

  SpdyClientSession *sm = (SpdyClientSession *)user_data;

  already = 0;
  blk     = TSIOBufferReaderStart(sm->req_reader);

  while (blk) {
    wavail = length - already;

    next_blk = TSIOBufferBlockNext(blk);
    start    = TSIOBufferBlockReadStart(blk, sm->req_reader, &blk_len);

    need = blk_len > wavail ? wavail : blk_len;

    memcpy(&buf[already], start, need);
    already += need;

    if (already >= (int64_t)length)
      break;

    blk = next_blk;
  }

  TSIOBufferReaderConsume(sm->req_reader, already);

  // This is a bit of a hack. If we are reading out of the buffer the protocol probe acceptor gave us, then we have not
  // kicked off our own I/O yet. After consuming this data we will come back and do that.
  if (sm->read_vio) {
    TSVIOReenable(sm->read_vio);
  }

  if (!already)
    return SPDYLAY_ERR_WOULDBLOCK;

  return already;
}

static void
spdy_process_syn_stream_frame(SpdyClientSession *sm, SpdyRequest *req)
{
  bool acceptEncodingRecvd = false;
  // validate request headers
  for (size_t i = 0; i < req->headers.size(); ++i) {
    const std::string &field = req->headers[i].first;
    const std::string &value = req->headers[i].second;

    if (field == ":path")
      req->path = value;
    else if (field == ":method")
      req->method = value;
    else if (field == ":scheme")
      req->scheme = value;
    else if (field == ":version")
      req->version = value;
    else if (field == ":host")
      req->host = value;
    else if (field == "accept-encoding")
      acceptEncodingRecvd = true;
  }

  if (!req->path.size() || !req->method.size() || !req->scheme.size() || !req->version.size() || !req->host.size()) {
    spdy_prepare_status_response_and_clean_request(sm, req->stream_id, STATUS_400);
    return;
  }

  if (!acceptEncodingRecvd) {
    Debug("spdy", "Accept-Encoding header not received, adding gzip for method %s", req->method.c_str());
    req->headers.push_back(make_pair("accept-encoding", "gzip, deflate"));
  }

  spdy_fetcher_launch(req);
}

void
spdy_on_ctrl_recv_callback(spdylay_session *session, spdylay_frame_type type, spdylay_frame *frame, void *user_data)
{
  int stream_id;
  SpdyRequest *req;
  SpdyClientSession *sm = (SpdyClientSession *)user_data;

  spdy_show_ctl_frame("++++RECV", session, type, frame, user_data);

  switch (type) {
  case SPDYLAY_SYN_STREAM:
    stream_id = frame->syn_stream.stream_id;
    req       = SpdyRequest::alloc();
    req->init(sm, stream_id);
    req->append_nv(frame->syn_stream.nv);
    sm->req_map[stream_id] = req;
    sm->vc->add_to_active_queue();
    spdy_process_syn_stream_frame(sm, req);
    sm->transact_count++;
    break;

  case SPDYLAY_HEADERS:
    stream_id = frame->syn_stream.stream_id;
    req       = sm->find_request(stream_id);
    if (!req) {
      Error("spdy_on_ctrl_recv_callback, req object null on SPDYLAY_HEADERS for sm %" PRId64 ", stream_id %d", sm->sm_id,
            stream_id);
      return;
    }
    req->append_nv(frame->headers.nv);
    break;

  case SPDYLAY_WINDOW_UPDATE:
    TSVIOReenable(sm->write_vio);
    break;

  default:
    break;
  }
  return;
}

void
spdy_on_invalid_ctrl_recv_callback(spdylay_session * /*session*/, spdylay_frame_type /*type*/, spdylay_frame * /*frame*/,
                                   uint32_t /*status_code*/, void * /*user_data*/)
{
  // TODO
  return;
}

void
spdy_on_data_chunk_recv_callback(spdylay_session * /*session*/, uint8_t /*flags*/, int32_t stream_id, const uint8_t *data,
                                 size_t len, void *user_data)
{
  SpdyClientSession *sm = (SpdyClientSession *)user_data;
  SpdyRequest *req      = sm->find_request(stream_id);

  //
  // SpdyRequest has been deleted on error, drop this data;
  //
  if (!req)
    return;

  Debug("spdy", "++++Fetcher Append Data, len:%zu", len);
  TSFetchWriteData(req->fetch_sm, data, len);

  return;
}

unsigned
spdy_session_delta_window_size(SpdyClientSession *sm)
{
  unsigned sess_delta_window_size = 0;
  map<int, SpdyRequest *>::iterator iter    = sm->req_map.begin();
  map<int, SpdyRequest *>::iterator endIter = sm->req_map.end();
  for (; iter != endIter; ++iter) {
    SpdyRequest *req = iter->second;
    sess_delta_window_size += req->delta_window_size;
  }
  Debug("spdy", "----sm_id:%" PRId64 ", session delta_window_size:%u", sm->sm_id, sess_delta_window_size);
  return sess_delta_window_size;
}

void
spdy_on_data_recv_callback(spdylay_session *session, uint8_t flags, int32_t stream_id, int32_t length, void *user_data)
{
  SpdyClientSession *sm = (SpdyClientSession *)user_data;
  SpdyRequest *req      = sm->find_request(stream_id);

  spdy_show_data_frame("++++RECV", session, flags, stream_id, length, user_data);

  //
  // After SpdyRequest has been deleted on error, the corresponding
  // client might continue to send POST data, We should reenable
  // sm->write_vio so that WINDOW_UPDATE has a chance to be sent.
  //
  if (!req) {
    TSVIOReenable(sm->write_vio);
    return;
  }

  req->delta_window_size += length;

  Debug("spdy", "----sm_id:%" PRId64 ", stream_id:%d, delta_window_size:%u", sm->sm_id, stream_id, req->delta_window_size);

  if (spdy_session_delta_window_size(sm) >= spdy_initial_window_size / 2) {
    Debug("spdy", "----Reenable write_vio for WINDOW_UPDATE frame, delta_window_size:%u", req->delta_window_size);

    //
    // Need not to send WINDOW_UPDATE frame here, what we should
    // do is to reenable sm->write_vio, and than spdylay_session_send()
    // will be triggered and it'll send WINDOW_UPDATE frame automatically.
    //
    TSVIOReenable(sm->write_vio);

    req->delta_window_size = 0;
  }

  return;
}

void
spdy_before_ctrl_send_callback(spdylay_session * /*session*/, spdylay_frame_type /*type*/, spdylay_frame * /*frame*/,
                               void * /*user_data*/)
{
  // TODO
  return;
}

void
spdy_on_ctrl_send_callback(spdylay_session *session, spdylay_frame_type type, spdylay_frame *frame, void *user_data)
{
  spdy_show_ctl_frame("----SEND", session, type, frame, user_data);

  return;
}

void
spdy_on_ctrl_not_send_callback(spdylay_session * /*session*/, spdylay_frame_type /*type*/, spdylay_frame * /*frame*/,
                               int /*error_code*/, void * /*user_data*/)
{
  // TODO
  return;
}

void
spdy_on_data_send_callback(spdylay_session *session, uint8_t flags, int32_t stream_id, int32_t length, void *user_data)
{
  SpdyClientSession *sm = (SpdyClientSession *)user_data;

  spdy_show_data_frame("----SEND", session, flags, stream_id, length, user_data);

  TSVIOReenable(sm->read_vio);
  return;
}

void
spdy_on_stream_close_callback(spdylay_session * /*session*/, int32_t /*stream_id*/, spdylay_status_code /*status_code*/,
                              void * /*user_data*/)
{
  // TODO
  return;
}

ssize_t
spdy_get_credential_proof(spdylay_session * /*session*/, const spdylay_origin * /*origin*/, uint8_t * /*proof*/,
                          size_t /*prooflen*/, void * /*user_data*/)
{
  // TODO
  return 0;
}

ssize_t
spdy_get_credential_ncerts(spdylay_session * /*session*/, const spdylay_origin * /*origin*/, void * /*user_data*/)
{
  // TODO
  return 0;
}

ssize_t
spdy_get_credential_cert(spdylay_session * /*session*/, const spdylay_origin * /*origin*/, size_t /*idx*/, uint8_t * /*cert*/,
                         size_t /*certlen*/, void * /*user_data*/)
{
  // TODO
  return 0;
}

void
spdy_on_request_recv_callback(spdylay_session * /*session*/, int32_t /*stream_id*/, void * /*user_data*/)
{
  // TODO
  return;
}

void
spdy_on_ctrl_recv_parse_error_callback(spdylay_session * /*session*/, spdylay_frame_type /*type*/, const uint8_t * /*head*/,
                                       size_t /*headlen*/, const uint8_t * /*payload*/, size_t /*payloadlen*/, int /*error_code*/,
                                       void * /*user_data*/)
{
  // TODO
  return;
}

void
spdy_on_unknown_ctrl_recv_callback(spdylay_session * /*session*/, const uint8_t * /*head*/, size_t /*headlen*/,
                                   const uint8_t * /*payload*/, size_t /*payloadlen*/, void * /*user_data*/)
{
  // TODO
  return;
}
