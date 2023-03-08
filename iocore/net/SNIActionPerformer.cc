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

#include "P_SNIActionPerformer.h"
#include "swoc/swoc_file.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_std.h"

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
  IpAddr addr1;
  IpAddr addr2;
  static constexpr swoc::TextView delim{",\n"};
  static void *MARK{reinterpret_cast<void *>(1)};

  while (!content.ltrim(delim).empty()) {
    swoc::TextView list{content.take_prefix_at(delim)};
    if (0 != ats_ip_range_parse(list, addr1, addr2)) {
      Debug("ssl_sni", "%.*s is not a valid format", static_cast<int>(list.size()), list.data());
      break;
    } else {
      Debug("ssl_sni", "%.*s added to the ip_allow list %.*s", static_cast<int>(list.size()), list.data(), int(server_name.size()),
            server_name.data());
      ip_map.fill(IpEndpoint().assign(addr1), IpEndpoint().assign(addr2), MARK);
    }
  }
}

int
SNI_IpAllow::SNIAction(TLSSNISupport *snis, ActionItem::Context const &ctx) const
{
  // i.e, ip filtering is not required
  if (ip_map.count() == 0) {
    return SSL_TLSEXT_ERR_OK;
  }

  auto ssl_vc = dynamic_cast<SSLNetVConnection *>(snis);
  auto ip     = ssl_vc->get_remote_endpoint();

  // check the allowed ips
  if (ip_map.contains(ip)) {
    return SSL_TLSEXT_ERR_OK;
  } else {
    char buff[256];
    ats_ip_ntop(&ip.sa, buff, sizeof(buff));
    Debug("ssl_sni", "%s is not allowed. Denying connection", buff);
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
}

bool
SNI_IpAllow::TestClientSNIAction(char const *servrername, IpEndpoint const &ep, int &policy) const
{
  return ip_map.contains(ep);
}
