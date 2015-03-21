/** @file

  SpdyClientSession.h

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

#ifndef __P_SPDY_SM_H__
#define __P_SPDY_SM_H__

#include "SpdyDefs.h"
#include "SpdyCommon.h"
#include "SpdyCallbacks.h"
#include <openssl/md5.h>
#include "Plugin.h"

class SpdyClientSession;
typedef int (*SpdyClientSessionHandler)(TSCont contp, TSEvent event, void *data);

class SpdyRequest
{
public:
  SpdyRequest()
    : spdy_sm(NULL), stream_id(-1), fetch_sm(NULL), has_submitted_data(false), need_resume_data(false), fetch_data_len(0),
      delta_window_size(0), fetch_body_completed(false)
  {
  }

  SpdyRequest(SpdyClientSession *sm, int id)
    : spdy_sm(NULL), stream_id(-1), fetch_sm(NULL), has_submitted_data(false), need_resume_data(false), fetch_data_len(0),
      delta_window_size(0), fetch_body_completed(false)
  {
    init(sm, id);
  }

  ~SpdyRequest() { clear(); }

  void init(SpdyClientSession *sm, int id);
  void clear();

  void
  append_nv(char **nv)
  {
    for (int i = 0; nv[i]; i += 2) {
      headers.push_back(make_pair(nv[i], nv[i + 1]));
    }
  }

public:
  int event;
  SpdyClientSession *spdy_sm;
  int stream_id;
  TSHRTime start_time;
  TSFetchSM fetch_sm;
  bool has_submitted_data;
  bool need_resume_data;
  int fetch_data_len;
  unsigned delta_window_size;
  bool fetch_body_completed;
  vector<pair<string, string> > headers;

  string url;
  string host;
  string path;
  string scheme;
  string method;
  string version;

  MD5_CTX recv_md5;
};

extern ClassAllocator<SpdyRequest> spdyRequestAllocator;

class SpdyClientSession : public Continuation, public PluginIdentity
{
public:
  SpdyClientSession() : Continuation(NULL) {}

  ~SpdyClientSession() { clear(); }

  void init(NetVConnection *netvc, spdy::SessionVersion vers);
  void clear();

  int64_t sm_id;
  spdy::SessionVersion version;
  uint64_t total_size;
  TSHRTime start_time;

  NetVConnection *vc;

  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  TSVIO read_vio;
  TSVIO write_vio;

  int event;
  spdylay_session *session;

  map<int32_t, SpdyRequest *> req_map;

  virtual char const *getPluginTag() const;
  virtual int64_t getPluginId() const;

  SpdyRequest *
  find_request(int streamId)
  {
    map<int32_t, SpdyRequest *>::iterator iter = this->req_map.find(streamId);
    return ((iter == this->req_map.end()) ? NULL : iter->second);
  }

  void
  cleanup_request(int streamId)
  {
    SpdyRequest *req = this->find_request(streamId);
    if (req) {
      req->clear();
      spdyRequestAllocator.free(req);
      this->req_map.erase(streamId);
    }
    if (req_map.empty() == true) {
      vc->add_to_keep_alive_lru();
    }
  }

private:
  int state_session_start(int event, void *edata);
  int state_session_readwrite(int event, void *edata);
};

void spdy_cs_create(NetVConnection *netvc, spdy::SessionVersion vers, MIOBuffer *iobuf, IOBufferReader *reader);

#endif
