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

using namespace std::literals;

#include "P_SNIActionPerformer.h"

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
      Debug("ssl_sni", "%.*s is not a valid format", static_cast<int>(token.size()), token.data());
      break;
    } else {
      Debug("ssl_sni", "%.*s added to the ip_allow token %.*s", static_cast<int>(token.size()), token.data(),
            int(server_name.size()), server_name.data());
      ip_addrs.fill(r);
    }
  }
}

int
SNI_IpAllow::SNIAction(TLSSNISupport *snis, ActionItem::Context const &ctx) const
{
  // i.e, ip filtering is not required
  if (ip_addrs.count() == 0) {
    return SSL_TLSEXT_ERR_OK;
  }

  auto ssl_vc = dynamic_cast<SSLNetVConnection *>(snis);
  auto ip     = swoc::IPAddr(ssl_vc->get_remote_endpoint());

  // check the allowed ips
  if (ip_addrs.contains(ip)) {
    return SSL_TLSEXT_ERR_OK;
  } else {
    swoc::LocalBufferWriter<256> w;
    w.print("{} is not allowed - denying connection\0", ip);
    Debug("ssl_sni", "%s", w.data());
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
}

bool
SNI_IpAllow::TestClientSNIAction(char const *servrername, IpEndpoint const &ep, int &policy) const
{
  return ip_addrs.contains(swoc::IPAddr(ep));
}

TunnelDestination::bwf_map_type TunnelDestination::bwf_map;

TunnelDestination::TunnelDestination(YamlSNIConfig::Item const &item, int cg_count)
  : destination(item.tunnel_destination), type(item.tunnel_type), tunnel_prewarm(item.tunnel_prewarm), alpn_ids(item.tunnel_alpn)
{
  // Get a view of the port text. If there is a substitution there, @a port will end up empty while
  // rest (stuff after port) will be non-empty. If both are empty there is no port specified at all.
  std::string_view port_text, rest;
  swoc::IPEndpoint::tokenize(destination, nullptr, &port_text, &rest);
  dynamic_port_p = port_text.empty() && !rest.empty();

  Debug("ssl_sni", "port is %s", dynamic_port_p ? "dynamic" : "static");

  _fmt.reset();
  try {
    bool valid_p = true;
    auto fmt     = swoc::bwf::Format(destination);
    // Validate the format doesn't exceed the context.
    for (auto const &item : fmt) {
      if ((item._type != swoc::bwf::Spec::LITERAL_TYPE && !bwf_map.contains(item._name))) {
        valid_p = false;
        Error("Invalid substitution \"%.*s\" in SNI configuration", static_cast<int>(item._name.size()), item._name.data());
        break;
      } else if (item._idx >= cg_count) {
        valid_p = false;
        Error("Invalid capture group %d in SNI configuration", item._idx);
        break;
      }
    }
    if (valid_p) {
      _fmt.emplace(std::move(fmt));
    }
  } catch (std::exception const &e) {
    Error("Invalid destination \"%.*s\" in SNI configuration - %s", static_cast<int>(destination.size()), destination.data(),
          e.what());
  }
}

int
TunnelDestination::SNIAction(TLSSNISupport *snis, const ActionItem::Context &ctx) const
{
  // Set the netvc option?
  SSLNetVConnection *ssl_netvc = dynamic_cast<SSLNetVConnection *>(snis);
  const char *servername       = snis->get_sni_server_name();
  if (ssl_netvc && _fmt.has_value()) {
    // Two pass for performance. If the destination fits in the static buffer no allocation is
    // needed. Otherwise the string is used to allocate a sufficient large buffer.

    // Output generation lambda.
    auto print = [&](swoc::FixedBufferWriter &&w) -> std::tuple<size_t, std::string_view> {
      w.print_nfv(bwf_map.bind(BWContext{ctx, ssl_netvc}), (*_fmt).bind(), CaptureArgs{ctx._fqdn_wildcard_captured_groups});
      return {w.extent(), w.view()};
    };

    std::string sbuff;    // string buffer if needed.
    std::string_view dst; // result is put here.
    char buff[512];       // Fixed buffer, try this first.
    auto const BSIZE = sizeof(buff);

    // Print on stack buffer.
    auto &&[extent, view] = print(swoc::FixedBufferWriter{buff, BSIZE});
    if (extent <= BSIZE) {
      dst = view;
    } else { // overflow, resize the string and print there.
      sbuff.resize(extent);
      std::tie(extent, dst) = print(swoc::FixedBufferWriter{sbuff.data(), extent});
    }
    ssl_netvc->set_tunnel_destination(dst, type, dynamic_port_p, tunnel_prewarm);
    Debug("ssl_sni", "Destination now is [%.*s], fqdn [%s]", int(dst.size()), dst.data(), servername);

    if (type == SNIRoutingType::BLIND) {
      ssl_netvc->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
    }

    // ALPN
    for (int id : alpn_ids) {
      ssl_netvc->enableProtocol(id);
    }
  }

  return SSL_TLSEXT_ERR_OK;
}

void
TunnelDestination::static_initialization()
{
  bwf_map.assign(
    TunnelDestination::MAP_WITH_RECV_PORT_STR,
    [](swoc::BufferWriter &w, swoc::bwf::Spec const &spec, TunnelDestination::BWContext const &ctx) -> swoc::BufferWriter & {
      return bwformat(w, spec, ctx._vc->get_local_port());
    });
  bwf_map.assign(
    TunnelDestination::MAP_WITH_PROXY_PROTOCOL_PORT_STR,
    [](swoc::BufferWriter &w, swoc::bwf::Spec const &spec, TunnelDestination::BWContext const &ctx) -> swoc::BufferWriter & {
      return bwformat(w, spec, ctx._vc->get_proxy_protocol_dst_port());
    });
}

std::any
TunnelDestination::CaptureArgs::capture(unsigned int idx) const
{
  static const std::string_view EMPTY;
  auto n = _groups.size();
  return std::any{static_cast<std::string_view const &>(n ? _groups[idx - 1] : EMPTY)};
}

swoc::BufferWriter &
TunnelDestination::CaptureArgs::print(swoc::BufferWriter &w, const swoc::bwf::Spec &spec, unsigned int idx) const
{
  return bwformat(w, spec, idx ? _groups[idx - 1] : ""sv);
}

unsigned
TunnelDestination::CaptureArgs::count() const
{
  auto n = _groups.size();
  return n ? n + 1 : 0; // Standard 0th group not provided.
}

// Static initializations/
[[maybe_unused]] static bool TunnelDestination_INIT = []() -> bool {
  TunnelDestination::static_initialization();
  return true;
}();
