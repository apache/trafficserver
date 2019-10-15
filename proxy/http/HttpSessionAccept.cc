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

#include "HttpSessionAccept.h"
#include "IPAllow.h"
#include "Http1ClientSession.h"
#include "I_Machine.h"

bool
HttpSessionAccept::accept(NetVConnection *netvc, MIOBuffer *iobuf, IOBufferReader *reader)
{
  sockaddr const *client_ip = netvc->get_remote_addr();
  IpAllow::ACL acl;
  ip_port_text_buffer ipb;

  acl = IpAllow::match(client_ip, IpAllow::SRC_ADDR);
  if (!acl.isValid()) { // if there's no ACL, it's a hard deny.
    Warning("client '%s' prohibited by ip-allow policy", ats_ip_ntop(client_ip, ipb, sizeof(ipb)));
    return false;
  }

  // Set the transport type if not already set
  if (HttpProxyPort::TRANSPORT_NONE == netvc->attributes) {
    netvc->attributes = transport_type;
  }

  if (is_debug_tag_set("http_seq")) {
    Debug("http_seq", "[HttpSessionAccept:mainEvent %p] accepted connection from %s transport type = %d", netvc,
          ats_ip_nptop(client_ip, ipb, sizeof(ipb)), netvc->attributes);
  }

  Http1ClientSession *new_session = THREAD_ALLOC_INIT(http1ClientSessionAllocator, this_ethread());

  new_session->accept_options = static_cast<Options *>(this);
  new_session->acl            = std::move(acl);

  new_session->new_connection(netvc, iobuf, reader);

  new_session->trans.upstream_outbound_options = *new_session->accept_options;

  return true;
}

int
HttpSessionAccept::mainEvent(int event, void *data)
{
  NetVConnection *netvc;
  ink_release_assert(event == NET_EVENT_ACCEPT || event == EVENT_ERROR);
  ink_release_assert((event == NET_EVENT_ACCEPT) ? (data != nullptr) : (1));

  if (event == NET_EVENT_ACCEPT) {
    netvc = static_cast<NetVConnection *>(data);
    if (!this->accept(netvc, nullptr, nullptr)) {
      netvc->do_io_close();
    }
    return EVENT_CONT;
  }

  /////////////////
  // EVENT_ERROR //
  /////////////////
  if (((long)data) == -ECONNABORTED) {
    /////////////////////////////////////////////////
    // Under Solaris, when accept() fails and sets //
    // errno to EPROTO, it means the client has    //
    // sent a TCP reset before the connection has  //
    // been accepted by the server...  Note that   //
    // in 2.5.1 with the Internet Server Supplement//
    // and also in 2.6 the errno for this case has //
    // changed from EPROTO to ECONNABORTED.        //
    /////////////////////////////////////////////////

    // FIX: add time to user_agent_hangup
    HTTP_SUM_DYN_STAT(http_ua_msecs_counts_errors_pre_accept_hangups_stat, 0);
  }

  ink_abort("HTTP accept received fatal error: errno = %d", -(static_cast<int>((intptr_t)data)));
  return EVENT_CONT;
}
