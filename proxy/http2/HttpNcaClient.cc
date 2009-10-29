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

   HttpNcaClient.cc

   Description:

   
 ****************************************************************************/

#include "HttpNcaClient.h"
#include "NcaProcessor.h"
#include "Main.h"
#include "HttpServerSession.h"

void
start_NcaServer()
{
  ncaProcessor.main_accept(NEW(new HttpNcaAccept));
}

HttpNcaAccept::HttpNcaAccept():Continuation(NULL)
{
  SET_HANDLER(HttpNcaAccept::mainEvent);
}

int
HttpNcaAccept::mainEvent(int event, void *vc)
{
  int attr = SERVER_PORT_NCA;

  ink_release_assert(event == NET_EVENT_ACCEPT);
  NcaVConnection *nca_vc = (NcaVConnection *) vc;
  nca_vc->set_data(NET_DATA_ATTRIBUTES, &attr);
  nca_vc->attributes = SERVER_PORT_NCA;
  HttpNcaClient *new_nca = HttpNcaClient::allocate();
  new_nca->new_nca_session(nca_vc, &nca_vc->nca_request);

  return EVENT_CONT;
}

HttpNcaClient *
HttpNcaClient::allocate()
{
  return NEW(new HttpNcaClient);
}

void
HttpNcaClient::cleanup()
{
  this->HttpClientSession::cleanup();
}

void
HttpNcaClient::destroy()
{
  this->cleanup();
  delete this;
}

HTTPHdr *
HttpNcaClient::get_request()
{
  return &nca_request;
}

void
HttpNcaClient::new_nca_session(NetVConnection * vc, HTTPHdr * request)
{

  nca_request.copy_shallow(request);
  new_connection(vc, false);
}

void
HttpNcaClient::release(IOBufferReader * r)
{
  this->do_io(VIO::CLOSE);
}

void
HttpNcaClient::attach_server_session(HttpServerSession * ssession)
{
  if (ssession) {
    ssession->release();
  }
}
