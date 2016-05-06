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
#include "ProxyClientSession.h"

class SpdyClientSession;
typedef int (*SpdyClientSessionHandler)(TSCont contp, TSEvent event, void *data);

class SpdyRequest
{
public:
  SpdyRequest()
    : event(0),
      spdy_sm(NULL),
      stream_id(-1),
      start_time(0),
      fetch_sm(NULL),
      has_submitted_data(false),
      need_resume_data(false),
      fetch_data_len(0),
      delta_window_size(0),
      fetch_body_completed(false)
  {
  }

  SpdyRequest(SpdyClientSession *sm, int id)
    : event(0),
      spdy_sm(NULL),
      stream_id(-1),
      start_time(0),
      fetch_sm(NULL),
      has_submitted_data(false),
      need_resume_data(false),
      fetch_data_len(0),
      delta_window_size(0),
      fetch_body_completed(false)
  {
    init(sm, id);
  }

  void init(SpdyClientSession *sm, int id);
  void clear();

  static SpdyRequest *alloc();
  void destroy();

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
  vector<pair<string, string>> headers;

  string url;
  string host;
  string path;
  string scheme;
  string method;
  string version;

  MD5_CTX recv_md5;
};

extern ClassAllocator<SpdyRequest> spdyRequestAllocator;

// class SpdyClientSession : public Continuation, public PluginIdentity
class SpdyClientSession : public ProxyClientSession, public PluginIdentity
{
public:
  typedef ProxyClientSession super; ///< Parent type.
  SpdyClientSession()
    : sm_id(0),
      version(spdy::SessionVersion::SESSION_VERSION_3_1),
      total_size(0),
      start_time(0),
      vc(NULL),
      req_buffer(NULL),
      req_reader(NULL),
      resp_buffer(NULL),
      resp_reader(NULL),
      read_vio(NULL),
      write_vio(NULL),
      event(0),
      session(NULL)
  {
  }

  void init(NetVConnection *netvc);
  void clear();
  void destroy();

  static SpdyClientSession *alloc();

  VIO *
  do_io_read(Continuation *, int64_t, MIOBuffer *)
  {
    // Due to spdylay, SPDY does not exercise do_io_read
    ink_release_assert(false);
    return NULL;
  }
  VIO *
  do_io_write(Continuation *, int64_t, IOBufferReader *, bool)
  {
    // Due to spdylay, SPDY does not exercise do_io_write
    ink_release_assert(false);
    return NULL;
  }

  void
  start()
  {
    ink_release_assert(false);
  }

  void do_io_close(int lerrno = -1);
  void
  do_io_shutdown(ShutdownHowTo_t howto)
  {
    ink_release_assert(false);
  }
  NetVConnection *
  get_netvc() const
  {
    return vc;
  }
  void
  release_netvc()
  {
    vc = NULL;
  }
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor);

  int
  get_transact_count() const
  {
    return this->transact_count;
  }
  void
  release(ProxyClientTransaction *)
  { /* TBD */
  }

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
  int transact_count;

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
      req->destroy();
      this->req_map.erase(streamId);
    }
    if (req_map.empty() == true) {
      vc->add_to_keep_alive_queue();
    }
  }

private:
  int state_session_start(int event, void *edata);
  int state_session_readwrite(int event, void *edata);
};

#endif
