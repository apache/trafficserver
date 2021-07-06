/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "P_Net.h"
#include "I_EventSystem.h"
#include "I_NetVConnection.h"
#include "P_QUICNetProcessor.h"

#include "QUICApplication.h"
#include "Http3App.h"

// TODO: add quic version option
// TODO: add host header option (also should be used for SNI)
struct QUICClientConfig {
  char addr[1024]       = "127.0.0.1";
  char output[1024]     = {0};
  char port[16]         = "4433";
  char path[1018]       = "/";
  char server_name[128] = "";
  char debug_tags[1024] = "quic|vv_quic_crypto|http3|qpack";
  int close             = false;
  int reset             = false;
  int http0_9           = true;
  int http3             = false;
};

class RespHandler : public Continuation
{
public:
  RespHandler(const QUICClientConfig *config, IOBufferReader *reader, std::function<void()> on_complete);
  int main_event_handler(int event, Event *data);
  void set_read_vio(VIO *vio);

private:
  const QUICClientConfig *_config = nullptr;
  const char *_filename           = nullptr;
  IOBufferReader *_reader         = nullptr;
  VIO *_read_vio                  = nullptr;
  std::function<void()> _on_complete;
};

class QUICClient : public Continuation
{
public:
  QUICClient(const QUICClientConfig *config);
  ~QUICClient();

  int start(int, void *);
  int state_http_server_open(int event, void *data);

private:
  const QUICClientConfig *_config    = nullptr;
  struct addrinfo *_remote_addr_info = nullptr;
  HttpSessionAccept::Options options;
};

class Http09ClientApp : public QUICApplication
{
public:
  Http09ClientApp(QUICNetVConnection *qvc, const QUICClientConfig *config);

  void start();
  int main_event_handler(int event, Event *data);

private:
  void _do_http_request();

  const QUICClientConfig *_config = nullptr;
  const char *_filename           = nullptr;
};

class Http3ClientApp : public Http3App
{
public:
  using super = Http3App;

  Http3ClientApp(QUICNetVConnection *qvc, IpAllow::ACL &&session_acl, const HttpSessionAccept::Options &options,
                 const QUICClientConfig *config);
  ~Http3ClientApp();

  void start() override;

private:
  void _do_http_request();

  RespHandler *_resp_handler      = nullptr;
  const QUICClientConfig *_config = nullptr;

  MIOBuffer *_req_buf  = nullptr;
  MIOBuffer *_resp_buf = nullptr;
};
