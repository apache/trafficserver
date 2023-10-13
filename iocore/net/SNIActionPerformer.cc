/** @file

  Implementation of SNIActionPerformer

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

#include "swoc/swoc_file.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_std.h"
#include "swoc/bwf_ip.h"

#include "P_SNIActionPerformer.h"

#if TS_USE_QUIC == 1
#include "P_QUICNetVConnection.h"
#endif

int
ControlQUIC::SNIAction(SSL &ssl, const Context &ctx) const
{
#if TS_USE_QUIC == 1
  if (enable_quic) {
    return SSL_TLSEXT_ERR_OK;
  }

  // This action is only available for QUIC connections
  if (QUICSupport::getInstance(&ssl) == nullptr) {
    return SSL_TLSEXT_ERR_OK;
  }

  if (dbg_ctl_ssl_sni.on()) {
    if (auto snis = TLSSNISupport::getInstance(&ssl)) {
      const char *servername = snis->get_sni_server_name();
      Dbg(dbg_ctl_ssl_sni, "Rejecting handshake due to QUIC being disabled for fqdn [%s]", servername);
    }
  }

  return SSL_TLSEXT_ERR_ALERT_FATAL;
#else
  return SSL_TLSEXT_ERR_OK;
#endif
}

SNI_IpAllow::SNI_IpAllow(std::string &ip_allow_list, std::string const &servername)
{
  swoc::TextView content{ip_allow_list};
  if (content && content[0] == '@') {
    std::error_code ec;
    swoc::file::path path{content.remove_prefix(1)};
    if (path.is_relative()) {
      path = swoc::file::path(Layout::get()->sysconfdir) / path;
    }
    ip_allow_list = swoc::file::load(path, ec);
    if (ec) {
      swoc::LocalBufferWriter<1024> w;
      w.print("SNIConfig unable to load file {} - {}", path.string(), ec);
      Warning("%.*s", int(w.size()), w.data());
    }
  }
  this->load(ip_allow_list, servername);
}

void
SNI_IpAllow::load(swoc::TextView content, swoc::TextView server_name)
{
  static constexpr swoc::TextView delim{",\n"};

  while (!content.ltrim(delim).empty()) {
    swoc::TextView token{content.take_prefix_at(delim)};
    if (swoc::IPRange r; r.load(token)) {
      Dbg(dbg_ctl_ssl_sni, "%.*s is not a valid format", static_cast<int>(token.size()), token.data());
      break;
    } else {
      Dbg(dbg_ctl_ssl_sni, "%.*s added to the ip_allow token %.*s", static_cast<int>(token.size()), token.data(),
          int(server_name.size()), server_name.data());
      ip_addrs.fill(r);
    }
  }
}

int
SNI_IpAllow::SNIAction(SSL &ssl, ActionItem::Context const &ctx) const
{
  // i.e, ip filtering is not required
  if (ip_addrs.count() == 0) {
    return SSL_TLSEXT_ERR_OK;
  }

  auto ssl_vc = SSLNetVCAccess(&ssl);
  auto ip     = swoc::IPAddr(ssl_vc->get_remote_endpoint());

  // check the allowed ips
  if (ip_addrs.contains(ip)) {
    return SSL_TLSEXT_ERR_OK;
  } else {
    swoc::LocalBufferWriter<256> w;
    w.print("{} is not allowed - denying connection\0", ip);
    Dbg(dbg_ctl_ssl_sni, "%s", w.data());
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
}

bool
SNI_IpAllow::TestClientSNIAction(char const *servrername, IpEndpoint const &ep, int &policy) const
{
  return ip_addrs.contains(swoc::IPAddr(ep));
}
