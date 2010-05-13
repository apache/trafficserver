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

/****************************************************************************

   HttpClientSession.h

   Description:


 ****************************************************************************/

#ifndef _HTTP_CLIENT_NCA_H_
#define _HTTP_CLIENT_NCA_H_

#include "HttpClientSession.h"

class HttpNcaAccept:public Continuation
{
public:
  HttpNcaAccept();
  int mainEvent(int event, void *ncavc);
};

class HttpNcaClient:public HttpClientSession
{

public:
  static HttpNcaClient *allocate();
  void cleanup();
  virtual void destroy();

  HTTPHdr *get_request();

  void new_nca_session(NetVConnection * vc, HTTPHdr * request);
  void release(IOBufferReader * r);
  void attach_server_session(HttpServerSession * ssession);

private:
    HTTPHdr nca_request;
};

void start_NcaServer();

#endif
