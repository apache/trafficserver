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

#include "Http2SessionAccept.h"
#include "Http2ClientSession.h"
#include "I_Machine.h"
#include "Error.h"

Http2SessionAccept::Http2SessionAccept(const HttpSessionAccept::Options &_o) : SessionAccept(NULL), options(_o)
{
  SET_HANDLER(&Http2SessionAccept::mainEvent);
}

Http2SessionAccept::~Http2SessionAccept()
{
}

void
Http2SessionAccept::accept(NetVConnection *netvc, MIOBuffer *iobuf, IOBufferReader *reader)
{
  // XXX we need to refactor the ACL checks from HttpSessionAccept so that we can invoke them here, and also in
  // the SPDY protocol layer ...
  // Warning("skipping access control checks for HTTP/2 connection");

  netvc->attributes = this->options.transport_type;

  const sockaddr *client_ip = netvc->get_remote_addr();
  if (is_debug_tag_set("http2_seq")) {
    ip_port_text_buffer ipb;

    Debug("http2_seq", "[HttpSessionAccept2:mainEvent %p] accepted connection from %s transport type = %d", netvc,
          ats_ip_nptop(client_ip, ipb, sizeof(ipb)), netvc->attributes);
  }

  // XXX Allocate a Http2ClientSession
  Http2ClientSession *new_session = THREAD_ALLOC_INIT(http2ClientSessionAllocator, this_ethread());

  new_session->new_connection(netvc, iobuf, reader, false /* backdoor */);
}

int
Http2SessionAccept::mainEvent(int event, void *data)
{
  ink_release_assert(event == NET_EVENT_ACCEPT || event == EVENT_ERROR);
  ink_release_assert((event == NET_EVENT_ACCEPT) ? (data != 0) : (1));

  if (event == NET_EVENT_ACCEPT) {
    this->accept(static_cast<NetVConnection *>(data), NULL, NULL);
    return EVENT_CONT;
  }

  // XXX We should hoist the error handling so that all the protocols generate the statistics
  // without code duplication.
  if (((long)data) == -ECONNABORTED) {
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_errors_pre_accept_hangups_stat, 0);
  }

  MachineFatal("HTTP/2 accept received fatal error: errno = %d", -((int)(intptr_t)data));
  return EVENT_CONT;
}
