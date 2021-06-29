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

#include "Http3SessionAccept.h"
#include "P_QUICNetVConnection.h"

#include "P_Net.h"
#include "I_Machine.h"
#include "IPAllow.h"

#include "Http09App.h"
#include "Http3App.h"

Http3SessionAccept::Http3SessionAccept(const HttpSessionAccept::Options &_o) : SessionAccept(nullptr), options(_o)
{
  SET_HANDLER(&Http3SessionAccept::mainEvent);
}

Http3SessionAccept::~Http3SessionAccept() {}

bool
Http3SessionAccept::accept(NetVConnection *netvc, MIOBuffer *iobuf, IOBufferReader *reader)
{
  sockaddr const *client_ip = netvc->get_remote_addr();
  IpAllow::ACL session_acl  = IpAllow::match(client_ip, IpAllow::SRC_ADDR);
  if (!session_acl.isValid()) {
    ip_port_text_buffer ipb;
    Warning("QUIC client '%s' prohibited by ip-allow policy", ats_ip_ntop(client_ip, ipb, sizeof(ipb)));
    return false;
  }

  netvc->attributes = this->options.transport_type;

  QUICNetVConnection *qvc = static_cast<QUICNetVConnection *>(netvc);

  if (is_debug_tag_set("http3")) {
    ip_port_text_buffer ipb;

    Debug("http3", "[%s] accepted connection from %s transport type = %d", qvc->cids().data(),
          ats_ip_nptop(client_ip, ipb, sizeof(ipb)), netvc->attributes);
  }

  std::string_view alpn = qvc->negotiated_application_name();

  if (IP_PROTO_TAG_HTTP_QUIC.compare(alpn) == 0 || IP_PROTO_TAG_HTTP_QUIC_D27.compare(alpn) == 0) {
    Debug("http3", "[%s] start HTTP/0.9 app (ALPN=%.*s)", qvc->cids().data(), static_cast<int>(alpn.length()), alpn.data());

    new Http09App(qvc, std::move(session_acl), this->options);
  } else if (IP_PROTO_TAG_HTTP_3.compare(alpn) == 0 || IP_PROTO_TAG_HTTP_3_D27.compare(alpn) == 0) {
    Debug("http3", "[%s] start HTTP/3 app (ALPN=%.*s)", qvc->cids().data(), static_cast<int>(alpn.length()), alpn.data());

    Http3App *app = new Http3App(qvc, std::move(session_acl), this->options);
    app->start();
  } else {
    ink_abort("Negotiated App Name is unknown");
  }

  return true;
}

int
Http3SessionAccept::mainEvent(int event, void *data)
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
