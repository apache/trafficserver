/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>

#include "ts/ts.h"

namespace ats_plugin
{
class Server;
class FCGIClientRequest;
class ServerIntercept;
struct ServerConnectionInfo;
struct InterceptIOChannel {
  TSVIO vio;
  TSIOBuffer iobuf;
  TSIOBufferReader reader;
  int total_bytes_written;
  bool readEnable;
  InterceptIOChannel();
  ~InterceptIOChannel();

  void read(TSVConn vc, TSCont contp);
  void write(TSVConn vc, TSCont contp);
  void phpWrite(TSVConn vc, TSCont contp, unsigned char *buf, int data_size, bool endflag);
};

class ServerConnection
{
public:
  ServerConnection(Server *server, TSEventFunc funcp);
  ~ServerConnection();

  enum State { INITIATED, READY, INUSE, COMPLETE, CLOSED };

  void
  setState(State state)
  {
    _state = state;
  }

  State
  getState()
  {
    return _state;
  }

  void
  setRequestId(uint requestId)
  {
    _requestId = requestId;
  }

  uint
  requestId()
  {
    return _requestId;
  }

  uint
  maxRequests()
  {
    return _max_requests;
  }

  uint
  requestCount()
  {
    return _req_count;
  }

  void createFCGIClient(ServerIntercept *intercept);
  void releaseFCGIClient();
  FCGIClientRequest *
  fcgiRequest()
  {
    return _fcgiRequest;
  }

  TSCont &
  contp()
  {
    return _contp;
  }

public:
  TSVConn vc_;
  std::string clientData, clientRequestBody, serverResponse;
  InterceptIOChannel readio;
  InterceptIOChannel writeio;
  FCGIClientRequest *_fcgiRequest;
  void createConnection();

private:
  State _state;
  Server *_server;
  TSEventFunc _funcp;
  TSCont _contp;
  ServerConnectionInfo *_sConnInfo;
  uint _requestId, _max_requests, _req_count;
};
} // namespace ats_plugin
