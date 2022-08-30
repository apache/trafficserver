/** @file

  HTTP configuration support.

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

#include <records/I_RecCore.h>
#include <records/I_RecHttp.h>
#include "tscore/ink_defs.h"
#include "tscore/TextBuffer.h"
#include "tscore/Tokenizer.h"
#include <strings.h>
#include "tscore/ink_inet.h"
#include <string_view>
#include <unordered_set>
#include <tscore/IpMapConf.h>

SessionProtocolNameRegistry globalSessionProtocolNameRegistry;

/* Protocol session well-known protocol names.
   These are also used for NPN setup.
*/

const char *const TS_ALPN_PROTOCOL_HTTP_0_9      = IP_PROTO_TAG_HTTP_0_9.data();
const char *const TS_ALPN_PROTOCOL_HTTP_1_0      = IP_PROTO_TAG_HTTP_1_0.data();
const char *const TS_ALPN_PROTOCOL_HTTP_1_1      = IP_PROTO_TAG_HTTP_1_1.data();
const char *const TS_ALPN_PROTOCOL_HTTP_2_0      = IP_PROTO_TAG_HTTP_2_0.data();
const char *const TS_ALPN_PROTOCOL_HTTP_3        = IP_PROTO_TAG_HTTP_3.data();
const char *const TS_ALPN_PROTOCOL_HTTP_QUIC     = IP_PROTO_TAG_HTTP_QUIC.data();
const char *const TS_ALPN_PROTOCOL_HTTP_3_D27    = IP_PROTO_TAG_HTTP_3_D27.data();
const char *const TS_ALPN_PROTOCOL_HTTP_QUIC_D27 = IP_PROTO_TAG_HTTP_QUIC_D27.data();

const char *const TS_ALPN_PROTOCOL_GROUP_HTTP  = "http";
const char *const TS_ALPN_PROTOCOL_GROUP_HTTP2 = "http2";

const char *const TS_PROTO_TAG_HTTP_1_0      = TS_ALPN_PROTOCOL_HTTP_1_0;
const char *const TS_PROTO_TAG_HTTP_1_1      = TS_ALPN_PROTOCOL_HTTP_1_1;
const char *const TS_PROTO_TAG_HTTP_2_0      = TS_ALPN_PROTOCOL_HTTP_2_0;
const char *const TS_PROTO_TAG_HTTP_3        = TS_ALPN_PROTOCOL_HTTP_3;
const char *const TS_PROTO_TAG_HTTP_QUIC     = TS_ALPN_PROTOCOL_HTTP_QUIC;
const char *const TS_PROTO_TAG_HTTP_3_D27    = TS_ALPN_PROTOCOL_HTTP_3_D27;
const char *const TS_PROTO_TAG_HTTP_QUIC_D27 = TS_ALPN_PROTOCOL_HTTP_QUIC_D27;
const char *const TS_PROTO_TAG_TLS_1_3       = IP_PROTO_TAG_TLS_1_3.data();
const char *const TS_PROTO_TAG_TLS_1_2       = IP_PROTO_TAG_TLS_1_2.data();
const char *const TS_PROTO_TAG_TLS_1_1       = IP_PROTO_TAG_TLS_1_1.data();
const char *const TS_PROTO_TAG_TLS_1_0       = IP_PROTO_TAG_TLS_1_0.data();
const char *const TS_PROTO_TAG_TCP           = IP_PROTO_TAG_TCP.data();
const char *const TS_PROTO_TAG_UDP           = IP_PROTO_TAG_UDP.data();
const char *const TS_PROTO_TAG_IPV4          = IP_PROTO_TAG_IPV4.data();
const char *const TS_PROTO_TAG_IPV6          = IP_PROTO_TAG_IPV6.data();

std::unordered_set<std::string_view> TSProtoTags;

// Precomputed indices for ease of use.
int TS_ALPN_PROTOCOL_INDEX_HTTP_0_9      = SessionProtocolNameRegistry::INVALID;
int TS_ALPN_PROTOCOL_INDEX_HTTP_1_0      = SessionProtocolNameRegistry::INVALID;
int TS_ALPN_PROTOCOL_INDEX_HTTP_1_1      = SessionProtocolNameRegistry::INVALID;
int TS_ALPN_PROTOCOL_INDEX_HTTP_2_0      = SessionProtocolNameRegistry::INVALID;
int TS_ALPN_PROTOCOL_INDEX_HTTP_3        = SessionProtocolNameRegistry::INVALID;
int TS_ALPN_PROTOCOL_INDEX_HTTP_QUIC     = SessionProtocolNameRegistry::INVALID;
int TS_ALPN_PROTOCOL_INDEX_HTTP_3_D27    = SessionProtocolNameRegistry::INVALID;
int TS_ALPN_PROTOCOL_INDEX_HTTP_QUIC_D27 = SessionProtocolNameRegistry::INVALID;

// Predefined protocol sets for ease of use.
SessionProtocolSet HTTP_PROTOCOL_SET;
SessionProtocolSet HTTP2_PROTOCOL_SET;
SessionProtocolSet DEFAULT_NON_TLS_SESSION_PROTOCOL_SET;
SessionProtocolSet DEFAULT_TLS_SESSION_PROTOCOL_SET;
SessionProtocolSet DEFAULT_QUIC_SESSION_PROTOCOL_SET;

static bool
mptcp_supported()
{
  ats_scoped_fd fd(::open("/proc/sys/net/mptcp/mptcp_enabled", O_RDONLY));
  int value = 0;

  if (fd) {
    TextBuffer buffer(16);

    buffer.slurp(fd.get());
    value = atoi(buffer.bufPtr());
  }

  return value != 0;
}

void
RecHttpLoadIp(const char *value_name, IpAddr &ip4, IpAddr &ip6)
{
  char value[1024];
  ip4.invalidate();
  ip6.invalidate();
  if (REC_ERR_OKAY == RecGetRecordString(value_name, value, sizeof(value))) {
    Tokenizer tokens(", ");
    int n_addrs = tokens.Initialize(value);
    for (int i = 0; i < n_addrs; ++i) {
      const char *host = tokens[i];
      IpEndpoint tmp4, tmp6;
      // For backwards compatibility we need to support the use of host names
      // for the address to bind.
      if (0 == ats_ip_getbestaddrinfo(host, &tmp4, &tmp6)) {
        if (ats_is_ip4(&tmp4)) {
          if (!ip4.isValid()) {
            ip4 = tmp4;
          } else {
            Warning("'%s' specifies more than one IPv4 address, ignoring %s.", value_name, host);
          }
        }
        if (ats_is_ip6(&tmp6)) {
          if (!ip6.isValid()) {
            ip6 = tmp6;
          } else {
            Warning("'%s' specifies more than one IPv6 address, ignoring %s.", value_name, host);
          }
        }
      } else {
        Warning("'%s' has an value '%s' that is not recognized as an IP address, ignored.", value_name, host);
      }
    }
  }
}

void
RecHttpLoadIpMap(const char *value_name, IpMap &ipmap)
{
  char value[1024];
  IpAddr laddr;
  IpAddr raddr;
  void *payload = nullptr;

  if (REC_ERR_OKAY == RecGetRecordString(value_name, value, sizeof(value))) {
    Debug("config", "RecHttpLoadIpMap: parsing the name [%s] and value [%s] to an IpMap", value_name, value);
    Tokenizer tokens(", ");
    int n_addrs = tokens.Initialize(value);
    for (int i = 0; i < n_addrs; ++i) {
      const char *val = tokens[i];

      Debug("config", "RecHttpLoadIpMap: marking the value [%s] to an IpMap entry", val);
      if (0 == ats_ip_range_parse(val, laddr, raddr)) {
        ipmap.fill(laddr, raddr, payload);
      }
    }
  }
  Debug("config", "RecHttpLoadIpMap: parsed %zu IpMap entries", ipmap.count());
}

const char *const HttpProxyPort::DEFAULT_VALUE = "8080";

const char *const HttpProxyPort::PORTS_CONFIG_NAME = "proxy.config.http.server_ports";

// "_PREFIX" means the option contains additional data.
// Each has a corresponding _LEN value that is the length of the option text.
// Options without _PREFIX are just flags with no additional data.

const char *const HttpProxyPort::OPT_FD_PREFIX          = "fd";
const char *const HttpProxyPort::OPT_OUTBOUND_IP_PREFIX = "ip-out";
const char *const HttpProxyPort::OPT_INBOUND_IP_PREFIX  = "ip-in";
const char *const HttpProxyPort::OPT_HOST_RES_PREFIX    = "ip-resolve";
const char *const HttpProxyPort::OPT_PROTO_PREFIX       = "proto";

const char *const HttpProxyPort::OPT_IPV6                    = "ipv6";
const char *const HttpProxyPort::OPT_IPV4                    = "ipv4";
const char *const HttpProxyPort::OPT_TRANSPARENT_INBOUND     = "tr-in";
const char *const HttpProxyPort::OPT_TRANSPARENT_OUTBOUND    = "tr-out";
const char *const HttpProxyPort::OPT_TRANSPARENT_FULL        = "tr-full";
const char *const HttpProxyPort::OPT_TRANSPARENT_PASSTHROUGH = "tr-pass";
const char *const HttpProxyPort::OPT_SSL                     = "ssl";
const char *const HttpProxyPort::OPT_PROXY_PROTO             = "pp";
const char *const HttpProxyPort::OPT_PLUGIN                  = "plugin";
const char *const HttpProxyPort::OPT_BLIND_TUNNEL            = "blind";
const char *const HttpProxyPort::OPT_COMPRESSED              = "compressed";
const char *const HttpProxyPort::OPT_MPTCP                   = "mptcp";
const char *const HttpProxyPort::OPT_QUIC                    = "quic";

// File local constants.
namespace
{
// Length values for _PREFIX options.
size_t const OPT_FD_PREFIX_LEN          = strlen(HttpProxyPort::OPT_FD_PREFIX);
size_t const OPT_OUTBOUND_IP_PREFIX_LEN = strlen(HttpProxyPort::OPT_OUTBOUND_IP_PREFIX);
size_t const OPT_INBOUND_IP_PREFIX_LEN  = strlen(HttpProxyPort::OPT_INBOUND_IP_PREFIX);
size_t const OPT_HOST_RES_PREFIX_LEN    = strlen(HttpProxyPort::OPT_HOST_RES_PREFIX);
size_t const OPT_PROTO_PREFIX_LEN       = strlen(HttpProxyPort::OPT_PROTO_PREFIX);

constexpr std::string_view TS_ALPN_PROTO_ID_OPENSSL_HTTP_0_9("\x8http/0.9");
constexpr std::string_view TS_ALPN_PROTO_ID_OPENSSL_HTTP_1_0("\x8http/1.0");
constexpr std::string_view TS_ALPN_PROTO_ID_OPENSSL_HTTP_1_1("\x8http/1.1");
constexpr std::string_view TS_ALPN_PROTO_ID_OPENSSL_HTTP_2("\x2h2");
constexpr std::string_view TS_ALPN_PROTO_ID_OPENSSL_HTTP_3("\x2h3");
} // namespace

namespace
{
// Solaris work around. On that OS the compiler will not let me use an
// instantiated instance of Vec<self> inside the class, even if
// static. So we have to declare it elsewhere and then import via
// reference. Might be a problem with Vec<> creating a fixed array
// rather than allocating on first use (compared to std::vector<>).
HttpProxyPort::Group GLOBAL_DATA;
} // namespace
HttpProxyPort::Group &HttpProxyPort::m_global = GLOBAL_DATA;

HttpProxyPort::HttpProxyPort() : m_fd(ts::NO_FD)

{
  m_host_res_preference = host_res_default_preference_order;
}

bool
HttpProxyPort::hasSSL(Group const &ports)
{
  return std::any_of(ports.begin(), ports.end(), [](HttpProxyPort const &port) { return port.isSSL(); });
}

bool
HttpProxyPort::hasQUIC(Group const &ports)
{
  bool zret = false;
  for (int i = 0, n = ports.size(); i < n && !zret; ++i) {
    if (ports[i].isQUIC()) {
      zret = true;
    }
  }
  return zret;
}

const HttpProxyPort *
HttpProxyPort::findHttp(Group const &ports, uint16_t family)
{
  bool check_family_p = ats_is_ip(family);
  const self *zret    = nullptr;
  for (int i = 0, n = ports.size(); i < n && !zret; ++i) {
    const self &p = ports[i];
    if (p.m_port &&                               // has a valid port
        TRANSPORT_DEFAULT == p.m_type &&          // is normal HTTP
        (!check_family_p || p.m_family == family) // right address family
    ) {
      zret = &p;
    };
  }
  return zret;
}

const char *
HttpProxyPort::checkPrefix(const char *src, char const *prefix, size_t prefix_len)
{
  const char *zret = nullptr;
  if (0 == strncasecmp(prefix, src, prefix_len)) {
    src += prefix_len;
    if ('-' == *src || '=' == *src) {
      ++src; // permit optional '-' or '='
    }
    zret = src;
  }
  return zret;
}

bool
HttpProxyPort::loadConfig(std::vector<self> &entries)
{
  char *text;
  bool found_p;

  text = REC_readString(PORTS_CONFIG_NAME, &found_p);
  if (found_p) {
    self::loadValue(entries, text);
  }
  ats_free(text);

  return 0 < entries.size();
}

bool
HttpProxyPort::loadDefaultIfEmpty(Group &ports)
{
  if (0 == ports.size()) {
    self::loadValue(ports, DEFAULT_VALUE);
  }

  return 0 < ports.size();
}

bool
HttpProxyPort::loadValue(std::vector<self> &ports, const char *text)
{
  unsigned old_port_length = ports.size(); // remember this.
  if (text && *text) {
    Tokenizer tokens(", ");
    int n_ports = tokens.Initialize(text);
    if (n_ports > 0) {
      for (int p = 0; p < n_ports; ++p) {
        const char *elt = tokens[p];
        HttpProxyPort entry;
        if (entry.processOptions(elt)) {
          ports.push_back(entry);
        } else {
          Warning("No valid definition was found in proxy port configuration element '%s'", elt);
        }
      }
    }
  }
  return ports.size() > old_port_length; // we added at least one port.
}

bool
HttpProxyPort::processOptions(const char *opts)
{
  bool zret           = false; // found a port?
  bool af_set_p       = false; // AF explicitly specified?
  bool host_res_set_p = false; // Host resolution order set explicitly?
  bool sp_set_p       = false; // Session protocol set explicitly?
  bool bracket_p      = false; // found an open bracket in the input?
  const char *value;           // Temp holder for value of a prefix option.
  IpAddr ip;                   // temp for loading IP addresses.
  std::vector<char *> values;  // Pointers to single option values.

  // Make a copy we can modify safely.
  size_t opts_len = strlen(opts) + 1;
  char *text      = static_cast<char *>(alloca(opts_len));
  memcpy(text, opts, opts_len);

  // Split the copy in to tokens.
  char *token = nullptr;
  for (char *spot = text; *spot; ++spot) {
    if (bracket_p) {
      if (']' == *spot) {
        bracket_p = false;
      }
    } else if (':' == *spot) {
      *spot = 0;
      token = nullptr;
    } else {
      if (!token) {
        token = spot;
        values.push_back(token);
      }
      if ('[' == *spot) {
        bracket_p = true;
      }
    }
  }
  if (bracket_p) {
    Warning("Invalid port descriptor '%s' - left bracket without closing right bracket", opts);
    return zret;
  }

  for (auto item : values) {
    if (isdigit(item[0])) { // leading digit -> port value
      char *ptr;
      int port = strtoul(item, &ptr, 10);
      if (ptr == item) {
        // really, this shouldn't happen, since we checked for a leading digit.
        Warning("Mangled port value '%s' in port configuration '%s'", item, opts);
      } else if (port <= 0 || 65536 <= port) {
        Warning("Port value '%s' out of range (1..65535) in port configuration '%s'", item, opts);
      } else {
        m_port = port;
        zret   = true;
      }
    } else if (nullptr != (value = this->checkPrefix(item, OPT_FD_PREFIX, OPT_FD_PREFIX_LEN))) {
      char *ptr; // tmp for syntax check.
      int fd = strtoul(value, &ptr, 10);
      if (ptr == value) {
        Warning("Mangled file descriptor value '%s' in port descriptor '%s'", item, opts);
      } else {
        m_fd = fd;
        zret = true;
      }
    } else if (nullptr != (value = this->checkPrefix(item, OPT_INBOUND_IP_PREFIX, OPT_INBOUND_IP_PREFIX_LEN))) {
      if (0 == ip.load(value)) {
        m_inbound_ip = ip;
      } else {
        Warning("Invalid IP address value '%s' in port descriptor '%s'", item, opts);
      }
    } else if (nullptr != (value = this->checkPrefix(item, OPT_OUTBOUND_IP_PREFIX, OPT_OUTBOUND_IP_PREFIX_LEN))) {
      if (0 == ip.load(value)) {
        this->outboundIp(ip.family()) = ip;
      } else {
        Warning("Invalid IP address value '%s' in port descriptor '%s'", item, opts);
      }
    } else if (0 == strcasecmp(OPT_COMPRESSED, item)) {
      m_type = TRANSPORT_COMPRESSED;
    } else if (0 == strcasecmp(OPT_BLIND_TUNNEL, item)) {
      m_type = TRANSPORT_BLIND_TUNNEL;
    } else if (0 == strcasecmp(OPT_IPV6, item)) {
      m_family = AF_INET6;
      af_set_p = true;
    } else if (0 == strcasecmp(OPT_IPV4, item)) {
      m_family = AF_INET;
      af_set_p = true;
    } else if (0 == strcasecmp(OPT_SSL, item)) {
      m_type = TRANSPORT_SSL;
#if TS_USE_QUIC == 1
    } else if (0 == strcasecmp(OPT_QUIC, item)) {
      m_type = TRANSPORT_QUIC;
#endif
    } else if (0 == strcasecmp(OPT_PLUGIN, item)) {
      m_type = TRANSPORT_PLUGIN;
    } else if (0 == strcasecmp(OPT_PROXY_PROTO, item)) {
      m_proxy_protocol = true;
    } else if (0 == strcasecmp(OPT_TRANSPARENT_INBOUND, item)) {
#if TS_USE_TPROXY
      m_inbound_transparent_p = true;
#else
      Warning("Transparency requested [%s] in port descriptor '%s' but TPROXY was not configured.", item, opts);
#endif
    } else if (0 == strcasecmp(OPT_TRANSPARENT_OUTBOUND, item)) {
#if TS_USE_TPROXY
      m_outbound_transparent_p = true;
#else
      Warning("Transparency requested [%s] in port descriptor '%s' but TPROXY was not configured.", item, opts);
#endif
    } else if (0 == strcasecmp(OPT_TRANSPARENT_FULL, item)) {
#if TS_USE_TPROXY
      m_inbound_transparent_p  = true;
      m_outbound_transparent_p = true;
#else
      Warning("Transparency requested [%s] in port descriptor '%s' but TPROXY was not configured.", item, opts);
#endif
    } else if (0 == strcasecmp(OPT_TRANSPARENT_PASSTHROUGH, item)) {
#if TS_USE_TPROXY
      m_transparent_passthrough = true;
#else
      Warning("Transparent pass-through requested [%s] in port descriptor '%s' but TPROXY was not configured.", item, opts);
#endif
    } else if (0 == strcasecmp(OPT_MPTCP, item)) {
      if (mptcp_supported()) {
        m_mptcp = true;
      } else {
        Warning("Multipath TCP requested [%s] in port descriptor '%s' but it is not supported by this host.", item, opts);
      }
    } else if (nullptr != (value = this->checkPrefix(item, OPT_HOST_RES_PREFIX, OPT_HOST_RES_PREFIX_LEN))) {
      this->processFamilyPreference(value);
      host_res_set_p = true;
    } else if (nullptr != (value = this->checkPrefix(item, OPT_PROTO_PREFIX, OPT_PROTO_PREFIX_LEN))) {
      this->processSessionProtocolPreference(value);
      sp_set_p = true;
    } else {
      Warning("Invalid option '%s' in proxy port descriptor '%s'", item, opts);
    }
  }

  bool in_ip_set_p = m_inbound_ip.isValid();

  if (af_set_p) {
    if (in_ip_set_p && m_family != m_inbound_ip.family()) {
      std::string_view iname{ats_ip_family_name(m_inbound_ip.family())};
      std::string_view fname{ats_ip_family_name(m_family)};
      Warning("Invalid port descriptor '%s' - the inbound address family [%.*s] is not the same type as the explicit family value "
              "[%.*s]",
              opts, static_cast<int>(iname.size()), iname.data(), static_cast<int>(fname.size()), fname.data());
      zret = false;
    }
  } else if (in_ip_set_p) {
    m_family = m_inbound_ip.family(); // set according to address.
  }

  // If the port is outbound transparent only CLIENT host resolution is possible.
  if (m_outbound_transparent_p) {
    if (host_res_set_p &&
        (m_host_res_preference[0] != HOST_RES_PREFER_CLIENT || m_host_res_preference[1] != HOST_RES_PREFER_NONE)) {
      Warning("Outbound transparent port '%s' requires the IP address resolution ordering '%s,%s'. "
              "This is set automatically and does not need to be set explicitly.",
              opts, HOST_RES_PREFERENCE_STRING[HOST_RES_PREFER_CLIENT], HOST_RES_PREFERENCE_STRING[HOST_RES_PREFER_NONE]);
    }
    m_host_res_preference[0] = HOST_RES_PREFER_CLIENT;
    m_host_res_preference[1] = HOST_RES_PREFER_NONE;
  }

  // Transparent pass-through requires tr-in
  if (m_transparent_passthrough && !m_inbound_transparent_p) {
    Warning("Port descriptor '%s' has transparent pass-through enabled without inbound transparency, this will be ignored.", opts);
    m_transparent_passthrough = false;
  }

  // Set the default session protocols.
  if (!sp_set_p) {
    if (this->isSSL()) {
      m_session_protocol_preference = DEFAULT_TLS_SESSION_PROTOCOL_SET;
    } else if (this->isQUIC()) {
      m_session_protocol_preference = DEFAULT_QUIC_SESSION_PROTOCOL_SET;
    } else {
      m_session_protocol_preference = DEFAULT_NON_TLS_SESSION_PROTOCOL_SET;
    }
  }

  return zret;
}

void
HttpProxyPort::processFamilyPreference(const char *value)
{
  parse_host_res_preference(value, m_host_res_preference);
}

void
HttpProxyPort::processSessionProtocolPreference(const char *value)
{
  m_session_protocol_preference.markAllOut();
  globalSessionProtocolNameRegistry.markIn(value, m_session_protocol_preference);
}

void
SessionProtocolNameRegistry::markIn(const char *value, SessionProtocolSet &sp_set)
{
  int n; // # of tokens
  Tokenizer tokens(" ;|,:");

  n = tokens.Initialize(value);

  for (int i = 0; i < n; ++i) {
    const char *elt = tokens[i];

    /// Check special cases
    if (0 == strcasecmp(elt, TS_ALPN_PROTOCOL_GROUP_HTTP)) {
      sp_set.markIn(HTTP_PROTOCOL_SET);
    } else if (0 == strcasecmp(elt, TS_ALPN_PROTOCOL_GROUP_HTTP2)) {
      sp_set.markIn(HTTP2_PROTOCOL_SET);
    } else { // user defined - register and mark.
      int idx = globalSessionProtocolNameRegistry.toIndex(TextView{elt, strlen(elt)});
      sp_set.markIn(idx);
    }
  }
}

int
HttpProxyPort::print(char *out, size_t n)
{
  size_t zret = 0; // # of chars printed so far.
  ip_text_buffer ipb;
  bool need_colon_p = false;

  if (m_inbound_ip.isValid()) {
    zret += snprintf(out + zret, n - zret, "%s=[%s]", OPT_INBOUND_IP_PREFIX, m_inbound_ip.toString(ipb, sizeof(ipb)));
    need_colon_p = true;
  }
  if (zret >= n) {
    return n;
  }

  if (m_outbound_ip4.isValid()) {
    if (need_colon_p) {
      out[zret++] = ':';
    }
    zret += snprintf(out + zret, n - zret, "%s=[%s]", OPT_OUTBOUND_IP_PREFIX, m_outbound_ip4.toString(ipb, sizeof(ipb)));
    need_colon_p = true;
  }
  if (zret >= n) {
    return n;
  }

  if (m_outbound_ip6.isValid()) {
    if (need_colon_p) {
      out[zret++] = ':';
    }
    zret += snprintf(out + zret, n - zret, "%s=[%s]", OPT_OUTBOUND_IP_PREFIX, m_outbound_ip6.toString(ipb, sizeof(ipb)));
    need_colon_p = true;
  }
  if (zret >= n) {
    return n;
  }

  if (0 != m_port) {
    if (need_colon_p) {
      out[zret++] = ':';
    }
    zret += snprintf(out + zret, n - zret, "%d", m_port);
    need_colon_p = true;
  }
  if (zret >= n) {
    return n;
  }

  if (ts::NO_FD != m_fd) {
    if (need_colon_p) {
      out[zret++] = ':';
    }
    zret += snprintf(out + zret, n - zret, "fd=%d", m_fd);
  }
  if (zret >= n) {
    return n;
  }

  // After this point, all of these options require other options which we've already
  // generated so all of them need a leading colon and we can stop checking for that.

  if (AF_INET6 == m_family) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_IPV6);
  }
  if (zret >= n) {
    return n;
  }

  if (TRANSPORT_BLIND_TUNNEL == m_type) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_BLIND_TUNNEL);
  } else if (TRANSPORT_SSL == m_type) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_SSL);
  } else if (TRANSPORT_QUIC == m_type) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_QUIC);
  } else if (TRANSPORT_PLUGIN == m_type) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_PLUGIN);
  } else if (TRANSPORT_COMPRESSED == m_type) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_COMPRESSED);
  }
  if (zret >= n) {
    return n;
  }

  if (m_proxy_protocol) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_PROXY_PROTO);
  }

  if (m_outbound_transparent_p && m_inbound_transparent_p) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_TRANSPARENT_FULL);
  } else if (m_inbound_transparent_p) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_TRANSPARENT_INBOUND);
  } else if (m_outbound_transparent_p) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_TRANSPARENT_OUTBOUND);
  }

  if (m_mptcp) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_MPTCP);
  }

  if (m_transparent_passthrough) {
    zret += snprintf(out + zret, n - zret, ":%s", OPT_TRANSPARENT_PASSTHROUGH);
  }

  /* Don't print the IP resolution preferences if the port is outbound
   * transparent (which means the preference order is forced) or if
   * the order is the same as the default.
   */
  if (!m_outbound_transparent_p && m_host_res_preference != host_res_default_preference_order) {
    zret += snprintf(out + zret, n - zret, ":%s=", OPT_HOST_RES_PREFIX);
    zret += ts_host_res_order_to_string(m_host_res_preference, out + zret, n - zret);
  }

  // session protocol options - look for condensed options first
  // first two cases are the defaults so if those match, print nothing.
  SessionProtocolSet sp_set = m_session_protocol_preference; // need to modify so copy.
  need_colon_p              = true;                          // for listing case, turned off if we do a special case.
  if (sp_set == DEFAULT_NON_TLS_SESSION_PROTOCOL_SET && !this->isSSL()) {
    sp_set.markOut(DEFAULT_NON_TLS_SESSION_PROTOCOL_SET);
  } else if (sp_set == DEFAULT_TLS_SESSION_PROTOCOL_SET && this->isSSL()) {
    sp_set.markOut(DEFAULT_TLS_SESSION_PROTOCOL_SET);
  } else if (sp_set == DEFAULT_QUIC_SESSION_PROTOCOL_SET && this->isQUIC()) {
    sp_set.markOut(DEFAULT_QUIC_SESSION_PROTOCOL_SET);
  }

  // pull out groups.
  if (sp_set.contains(HTTP_PROTOCOL_SET)) {
    zret += snprintf(out + zret, n - zret, ":%s=%s", OPT_PROTO_PREFIX, TS_ALPN_PROTOCOL_GROUP_HTTP);
    sp_set.markOut(HTTP_PROTOCOL_SET);
    need_colon_p = false;
  }
  if (sp_set.contains(HTTP2_PROTOCOL_SET)) {
    if (need_colon_p) {
      zret += snprintf(out + zret, n - zret, ":%s=", OPT_PROTO_PREFIX);
    } else {
      out[zret++] = ';';
    }
    zret += snprintf(out + zret, n - zret, "%s", TS_ALPN_PROTOCOL_GROUP_HTTP2);
    sp_set.markOut(HTTP2_PROTOCOL_SET);
    need_colon_p = false;
  }
  // now enumerate what's left.
  if (!sp_set.isEmpty()) {
    if (need_colon_p) {
      zret += snprintf(out + zret, n - zret, ":%s=", OPT_PROTO_PREFIX);
    }
    bool sep_p = !need_colon_p;
    for (int k = 0; k < SessionProtocolSet::MAX; ++k) {
      if (sp_set.contains(k)) {
        auto name{globalSessionProtocolNameRegistry.nameFor(k)};
        zret += snprintf(out + zret, n - zret, "%s%.*s", sep_p ? ";" : "", static_cast<int>(name.size()), name.data());
        sep_p = true;
      }
    }
  }

  return std::min(zret, n);
}

void
ts_host_res_global_init()
{
  // Global configuration values.
  host_res_default_preference_order = HOST_RES_DEFAULT_PREFERENCE_ORDER;
  char *ip_resolve                  = REC_ConfigReadString("proxy.config.hostdb.ip_resolve");
  if (ip_resolve) {
    parse_host_res_preference(ip_resolve, host_res_default_preference_order);
  }
  ats_free(ip_resolve);
}

// Whatever executable uses librecords must call this.
void
ts_session_protocol_well_known_name_indices_init()
{
  // register all the well known protocols and get the indices set.
  TS_ALPN_PROTOCOL_INDEX_HTTP_0_9   = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{TS_ALPN_PROTOCOL_HTTP_0_9});
  TS_ALPN_PROTOCOL_INDEX_HTTP_1_0   = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{TS_ALPN_PROTOCOL_HTTP_1_0});
  TS_ALPN_PROTOCOL_INDEX_HTTP_1_1   = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{TS_ALPN_PROTOCOL_HTTP_1_1});
  TS_ALPN_PROTOCOL_INDEX_HTTP_2_0   = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{TS_ALPN_PROTOCOL_HTTP_2_0});
  TS_ALPN_PROTOCOL_INDEX_HTTP_3     = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{TS_ALPN_PROTOCOL_HTTP_3});
  TS_ALPN_PROTOCOL_INDEX_HTTP_3_D27 = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{TS_ALPN_PROTOCOL_HTTP_3_D27});
  TS_ALPN_PROTOCOL_INDEX_HTTP_QUIC  = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{TS_ALPN_PROTOCOL_HTTP_QUIC});
  TS_ALPN_PROTOCOL_INDEX_HTTP_QUIC_D27 =
    globalSessionProtocolNameRegistry.toIndexConst(std::string_view{TS_ALPN_PROTOCOL_HTTP_QUIC_D27});

  // Now do the predefined protocol sets.
  HTTP_PROTOCOL_SET.markIn(TS_ALPN_PROTOCOL_INDEX_HTTP_0_9);
  HTTP_PROTOCOL_SET.markIn(TS_ALPN_PROTOCOL_INDEX_HTTP_1_0);
  HTTP_PROTOCOL_SET.markIn(TS_ALPN_PROTOCOL_INDEX_HTTP_1_1);
  HTTP2_PROTOCOL_SET.markIn(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0);

  DEFAULT_TLS_SESSION_PROTOCOL_SET.markAllIn();
  DEFAULT_TLS_SESSION_PROTOCOL_SET.markOut(TS_ALPN_PROTOCOL_INDEX_HTTP_3);
  DEFAULT_TLS_SESSION_PROTOCOL_SET.markOut(TS_ALPN_PROTOCOL_INDEX_HTTP_QUIC);

  DEFAULT_QUIC_SESSION_PROTOCOL_SET.markIn(TS_ALPN_PROTOCOL_INDEX_HTTP_3);
  DEFAULT_QUIC_SESSION_PROTOCOL_SET.markIn(TS_ALPN_PROTOCOL_INDEX_HTTP_QUIC);
  DEFAULT_QUIC_SESSION_PROTOCOL_SET.markIn(TS_ALPN_PROTOCOL_INDEX_HTTP_3_D27);
  DEFAULT_QUIC_SESSION_PROTOCOL_SET.markIn(TS_ALPN_PROTOCOL_INDEX_HTTP_QUIC_D27);

  DEFAULT_NON_TLS_SESSION_PROTOCOL_SET = HTTP_PROTOCOL_SET;

  TSProtoTags.insert(TS_PROTO_TAG_HTTP_1_0);
  TSProtoTags.insert(TS_PROTO_TAG_HTTP_1_1);
  TSProtoTags.insert(TS_PROTO_TAG_HTTP_2_0);
  TSProtoTags.insert(TS_PROTO_TAG_HTTP_3);
  TSProtoTags.insert(TS_PROTO_TAG_HTTP_QUIC);
  TSProtoTags.insert(TS_PROTO_TAG_HTTP_3_D27);
  TSProtoTags.insert(TS_PROTO_TAG_HTTP_QUIC_D27);
  TSProtoTags.insert(TS_PROTO_TAG_TLS_1_3);
  TSProtoTags.insert(TS_PROTO_TAG_TLS_1_2);
  TSProtoTags.insert(TS_PROTO_TAG_TLS_1_1);
  TSProtoTags.insert(TS_PROTO_TAG_TLS_1_0);
  TSProtoTags.insert(TS_PROTO_TAG_TCP);
  TSProtoTags.insert(TS_PROTO_TAG_UDP);
  TSProtoTags.insert(TS_PROTO_TAG_IPV4);
  TSProtoTags.insert(TS_PROTO_TAG_IPV6);
}

const char *
RecNormalizeProtoTag(const char *tag)
{
  auto findResult = TSProtoTags.find(tag);
  return findResult == TSProtoTags.end() ? nullptr : findResult->data();
}

/**
   Convert TS_ALPN_PROTOCOL_INDEX_* into OpenSSL ALPN Wire Format

   https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_alpn_protos.html

   TODO: support dynamic generation of wire format
 */
std::string_view
SessionProtocolNameRegistry::convert_openssl_alpn_wire_format(int index)
{
  if (index == TS_ALPN_PROTOCOL_INDEX_HTTP_0_9) {
    return TS_ALPN_PROTO_ID_OPENSSL_HTTP_0_9;
  } else if (index == TS_ALPN_PROTOCOL_INDEX_HTTP_1_0) {
    return TS_ALPN_PROTO_ID_OPENSSL_HTTP_1_0;
  } else if (index == TS_ALPN_PROTOCOL_INDEX_HTTP_1_1) {
    return TS_ALPN_PROTO_ID_OPENSSL_HTTP_1_1;
  } else if (index == TS_ALPN_PROTOCOL_INDEX_HTTP_2_0) {
    return TS_ALPN_PROTO_ID_OPENSSL_HTTP_2;
  } else if (index == TS_ALPN_PROTOCOL_INDEX_HTTP_3) {
    return TS_ALPN_PROTO_ID_OPENSSL_HTTP_3;
  }

  return {};
}

int
SessionProtocolNameRegistry::toIndex(ts::TextView name)
{
  int zret = this->indexFor(name);
  if (INVALID == zret) {
    if (m_n < MAX) {
      // Localize the name by copying it in to the arena.
      auto text = m_arena.alloc(name.size() + 1).rebind<char>();
      memcpy(text.data(), name.data(), name.size());
      text.end()[-1] = '\0';
      m_names[m_n]   = text.view();
      zret           = m_n++;
    } else {
      ink_release_assert(!"Session protocol name registry overflow");
    }
  }
  return zret;
}

int
SessionProtocolNameRegistry::toIndexConst(TextView name)
{
  int zret = this->indexFor(name);
  if (INVALID == zret) {
    if (m_n < MAX) {
      m_names[m_n] = name;
      zret         = m_n++;
    } else {
      ink_release_assert(!"Session protocol name registry overflow");
    }
  }
  return zret;
}

int
SessionProtocolNameRegistry::indexFor(TextView name) const
{
  const ts::TextView *end = m_names.begin() + m_n;
  auto spot               = std::find(m_names.begin(), end, name);
  if (spot != end) {
    return static_cast<int>(spot - m_names.begin());
  }
  return INVALID;
}

ts::TextView
SessionProtocolNameRegistry::nameFor(int idx) const
{
  return 0 <= idx && idx < m_n ? m_names[idx] : TextView{};
}
