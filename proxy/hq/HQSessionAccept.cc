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

#include "HQSessionAccept.h"

#include "P_Net.h"
#include "I_Machine.h"
#include "../IPAllow.h"
#include "QUICSimpleApp.h"

HQSessionAccept::HQSessionAccept(const HttpSessionAccept::Options &_o) : SessionAccept(nullptr), options(_o)
{
  SET_HANDLER(&HQSessionAccept::mainEvent);
}

HQSessionAccept::~HQSessionAccept()
{
}

bool
HQSessionAccept::accept(NetVConnection *netvc, MIOBuffer *iobuf, IOBufferReader *reader)
{
  sockaddr const *client_ip           = netvc->get_remote_addr();
  const AclRecord *session_acl_record = testIpAllowPolicy(client_ip);
  if (!session_acl_record) {
    ip_port_text_buffer ipb;
    Warning("QUIC client '%s' prohibited by ip-allow policy", ats_ip_ntop(client_ip, ipb, sizeof(ipb)));
    return false;
  }
  netvc->attributes = this->options.transport_type;

  if (is_debug_tag_set("quic_seq")) {
    ip_port_text_buffer ipb;

    Debug("quic_seq", "[HQSessionAccept:mainEvent %p] accepted connection from %s transport type = %d", netvc,
          ats_ip_nptop(client_ip, ipb, sizeof(ipb)), netvc->attributes);
  }

  new QUICSimpleApp(static_cast<QUICNetVConnection *>(netvc));

  return true;
}

int
HQSessionAccept::mainEvent(int event, void *data)
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

  return EVENT_CONT;
}
