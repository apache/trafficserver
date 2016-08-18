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
#include "Error.h"

void
HttpSessionAccept::accept(NetVConnection *netvc, MIOBuffer *iobuf, IOBufferReader *reader)
{
  sockaddr const *client_ip   = netvc->get_remote_addr();
  const AclRecord *acl_record = NULL;
  ip_port_text_buffer ipb;

  // The backdoor port is now only bound to "localhost", so no
  // reason to check for if it's incoming from "localhost" or not.
  if (backdoor) {
    acl_record = IpAllow::AllMethodAcl();
  } else {
    acl_record = testIpAllowPolicy(client_ip);
    if (!acl_record) {
      ////////////////////////////////////////////////////
      // if client address forbidden, close immediately //
      ////////////////////////////////////////////////////
      Warning("client '%s' prohibited by ip-allow policy", ats_ip_ntop(client_ip, ipb, sizeof(ipb)));
      netvc->do_io_close();
      return;
    }
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

  // copy over session related data.
  new_session->f_outbound_transparent    = f_outbound_transparent;
  new_session->f_transparent_passthrough = f_transparent_passthrough;
  new_session->outbound_ip4              = outbound_ip4;
  new_session->outbound_ip6              = outbound_ip6;
  new_session->outbound_port             = outbound_port;
  new_session->host_res_style            = ats_host_res_from(client_ip->sa_family, host_res_preference);
  new_session->acl_record                = acl_record;

  new_session->new_connection(netvc, iobuf, reader, backdoor);

  return;
}

int
HttpSessionAccept::mainEvent(int event, void *data)
{
  ink_release_assert(event == NET_EVENT_ACCEPT || event == EVENT_ERROR);
  ink_release_assert((event == NET_EVENT_ACCEPT) ? (data != 0) : (1));

  if (event == NET_EVENT_ACCEPT) {
    this->accept(static_cast<NetVConnection *>(data), NULL, NULL);
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

  MachineFatal("HTTP accept received fatal error: errno = %d", -((int)(intptr_t)data));
  return EVENT_CONT;
}
