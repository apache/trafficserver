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

#include "proxy/http/HttpSessionAccept.h"
#include "proxy/IPAllow.h"
#include "proxy/http/Http1ClientSession.h"
#include "iocore/utils/Machine.h"

namespace
{
DbgCtl dbg_ctl_http_seq{"http_seq"};

} // end anonymous namespace

bool
HttpSessionAccept::accept(NetVConnection *netvc, MIOBuffer *iobuf, IOBufferReader *reader)
{
  sockaddr const     *client_ip;
  IpAllow::ACL        acl;
  ip_port_text_buffer ipb;

  client_ip = netvc->get_remote_addr();

  if (ats_is_ip(client_ip)) {
    acl = IpAllow::match(client_ip, IpAllow::match_key_t::SRC_ADDR);
    if (!acl.isValid()) { // if there's no ACL, it's a hard deny.
      Warning("client '%s' prohibited by ip-allow policy", ats_ip_ntop(client_ip, ipb, sizeof(ipb)));
      return false;
    }
  } else {
    // If IP address is not available (e.g. UDS), IP-based ACLs are not relevant
    acl = IpAllow::makeAllowAllACL();
  }

  // Set the transport type if not already set
  if (HttpProxyPort::TRANSPORT_NONE == netvc->attributes) {
    netvc->attributes = transport_type;
  }

  if (dbg_ctl_http_seq.on()) {
    Dbg(dbg_ctl_http_seq, "[HttpSessionAccept:mainEvent %p] accepted connection from %s transport type = %d", netvc,
        ats_ip_nptop(client_ip, ipb, sizeof(ipb)), netvc->attributes);
  }

  Http1ClientSession *new_session = THREAD_ALLOC_INIT(http1ClientSessionAllocator, this_ethread());

  new_session->accept_options = static_cast<Options *>(this);
  new_session->acl            = std::move(acl);

  // Pin session to current ET_NET thread
  new_session->setThreadAffinity(this_ethread());
  new_session->new_connection(netvc, iobuf, reader);

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
    // FIX: add time to user_agent_hangup
    Metrics::Counter::increment(http_rsb.ua_counts_errors_pre_accept_hangups);
    // Metrics::Counter::increment(http_rsb.ua_msecs_errors_pre_accept_hangups, 0); // ToDo: Weird, but we added 0 here before
  }

  ink_abort("HTTP accept received fatal error: errno = %d", -(static_cast<int>((intptr_t)data)));
  return EVENT_CONT;
}
