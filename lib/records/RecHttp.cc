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
#include <ts/ink_defs.h>
#include <ts/Tokenizer.h>
#include <strings.h>

SessionProtocolNameRegistry globalSessionProtocolNameRegistry;

/* Protocol session well-known protocol names.
   These are also used for NPN setup.
*/

const char *const TS_NPN_PROTOCOL_HTTP_0_9    = "http/0.9";
const char *const TS_NPN_PROTOCOL_HTTP_1_0    = "http/1.0";
const char *const TS_NPN_PROTOCOL_HTTP_1_1    = "http/1.1";
const char *const TS_NPN_PROTOCOL_HTTP_2_0_14 = "h2-14";  // Last H2 interrop draft. TODO: Should be removed later
const char *const TS_NPN_PROTOCOL_HTTP_2_0    = "h2";     // HTTP/2 over TLS
const char *const TS_NPN_PROTOCOL_SPDY_1      = "spdy/1"; // obsolete
const char *const TS_NPN_PROTOCOL_SPDY_2      = "spdy/2";
const char *const TS_NPN_PROTOCOL_SPDY_3      = "spdy/3";
const char *const TS_NPN_PROTOCOL_SPDY_3_1    = "spdy/3.1";

const char *const TS_NPN_PROTOCOL_GROUP_HTTP  = "http";
const char *const TS_NPN_PROTOCOL_GROUP_HTTP2 = "http2";
const char *const TS_NPN_PROTOCOL_GROUP_SPDY  = "spdy";

// Precomputed indices for ease of use.
int TS_NPN_PROTOCOL_INDEX_HTTP_0_9 = SessionProtocolNameRegistry::INVALID;
int TS_NPN_PROTOCOL_INDEX_HTTP_1_0 = SessionProtocolNameRegistry::INVALID;
int TS_NPN_PROTOCOL_INDEX_HTTP_1_1 = SessionProtocolNameRegistry::INVALID;
int TS_NPN_PROTOCOL_INDEX_HTTP_2_0 = SessionProtocolNameRegistry::INVALID;
int TS_NPN_PROTOCOL_INDEX_SPDY_1   = SessionProtocolNameRegistry::INVALID;
int TS_NPN_PROTOCOL_INDEX_SPDY_2   = SessionProtocolNameRegistry::INVALID;
int TS_NPN_PROTOCOL_INDEX_SPDY_3   = SessionProtocolNameRegistry::INVALID;
int TS_NPN_PROTOCOL_INDEX_SPDY_3_1 = SessionProtocolNameRegistry::INVALID;

// Predefined protocol sets for ease of use.
SessionProtocolSet HTTP_PROTOCOL_SET;
SessionProtocolSet SPDY_PROTOCOL_SET;
SessionProtocolSet HTTP2_PROTOCOL_SET;
SessionProtocolSet DEFAULT_NON_TLS_SESSION_PROTOCOL_SET;
SessionProtocolSet DEFAULT_TLS_SESSION_PROTOCOL_SET;

void
RecHttpLoadIp(char const *value_name, IpAddr &ip4, IpAddr &ip6)
{
  char value[1024];
  ip4.invalidate();
  ip6.invalidate();
  if (REC_ERR_OKAY == RecGetRecordString(value_name, value, sizeof(value))) {
    Tokenizer tokens(", ");
    int n_addrs = tokens.Initialize(value);
    for (int i = 0; i < n_addrs; ++i) {
      char const *host = tokens[i];
      IpEndpoint tmp4, tmp6;
      // For backwards compatibility we need to support the use of host names
      // for the address to bind.
      if (0 == ats_ip_getbestaddrinfo(host, &tmp4, &tmp6)) {
        if (ats_is_ip4(&tmp4)) {
          if (!ip4.isValid())
            ip4 = tmp4;
          else
            Warning("'%s' specifies more than one IPv4 address, ignoring %s.", value_name, host);
        }
        if (ats_is_ip6(&tmp6)) {
          if (!ip6.isValid())
            ip6 = tmp6;
          else
            Warning("'%s' specifies more than one IPv6 address, ignoring %s.", value_name, host);
        }
      } else {
        Warning("'%s' has an value '%s' that is not recognized as an IP address, ignored.", value_name, host);
      }
    }
  }
}

char const *const HttpProxyPort::DEFAULT_VALUE = "8080";

char const *const HttpProxyPort::PORTS_CONFIG_NAME = "proxy.config.http.server_ports";

// "_PREFIX" means the option contains additional data.
// Each has a corresponding _LEN value that is the length of the option text.
// Options without _PREFIX are just flags with no additional data.

char const *const HttpProxyPort::OPT_FD_PREFIX          = "fd";
char const *const HttpProxyPort::OPT_OUTBOUND_IP_PREFIX = "ip-out";
char const *const HttpProxyPort::OPT_INBOUND_IP_PREFIX  = "ip-in";
char const *const HttpProxyPort::OPT_HOST_RES_PREFIX    = "ip-resolve";
char const *const HttpProxyPort::OPT_PROTO_PREFIX       = "proto";

char const *const HttpProxyPort::OPT_IPV6                    = "ipv6";
char const *const HttpProxyPort::OPT_IPV4                    = "ipv4";
char const *const HttpProxyPort::OPT_TRANSPARENT_INBOUND     = "tr-in";
char const *const HttpProxyPort::OPT_TRANSPARENT_OUTBOUND    = "tr-out";
char const *const HttpProxyPort::OPT_TRANSPARENT_FULL        = "tr-full";
char const *const HttpProxyPort::OPT_TRANSPARENT_PASSTHROUGH = "tr-pass";
char const *const HttpProxyPort::OPT_SSL                     = "ssl";
char const *const HttpProxyPort::OPT_PLUGIN                  = "plugin";
char const *const HttpProxyPort::OPT_BLIND_TUNNEL            = "blind";
char const *const HttpProxyPort::OPT_COMPRESSED              = "compressed";

// File local constants.
namespace
{
// Length values for _PREFIX options.
size_t const OPT_FD_PREFIX_LEN          = strlen(HttpProxyPort::OPT_FD_PREFIX);
size_t const OPT_OUTBOUND_IP_PREFIX_LEN = strlen(HttpProxyPort::OPT_OUTBOUND_IP_PREFIX);
size_t const OPT_INBOUND_IP_PREFIX_LEN  = strlen(HttpProxyPort::OPT_INBOUND_IP_PREFIX);
size_t const OPT_HOST_RES_PREFIX_LEN    = strlen(HttpProxyPort::OPT_HOST_RES_PREFIX);
size_t const OPT_PROTO_PREFIX_LEN       = strlen(HttpProxyPort::OPT_PROTO_PREFIX);
}

namespace
{
// Solaris work around. On that OS the compiler will not let me use an
// instantiated instance of Vec<self> inside the class, even if
// static. So we have to declare it elsewhere and then import via
// reference. Might be a problem with Vec<> creating a fixed array
// rather than allocating on first use (compared to std::vector<>).
HttpProxyPort::Group GLOBAL_DATA;
}
HttpProxyPort::Group &HttpProxyPort::m_global = GLOBAL_DATA;

HttpProxyPort::HttpProxyPort()
  : m_fd(ts::NO_FD),
    m_type(TRANSPORT_DEFAULT),
    m_port(0),
    m_family(AF_INET),
    m_inbound_transparent_p(false),
    m_outbound_transparent_p(false),
    m_transparent_passthrough(false)
{
  memcpy(m_host_res_preference, host_res_default_preference_order, sizeof(m_host_res_preference));
}

bool
HttpProxyPort::hasSSL(Group const &ports)
{
  bool zret = false;
  for (int i = 0, n = ports.length(); i < n && !zret; ++i) {
    if (ports[i].isSSL())
      zret = true;
  }
  return zret;
}

HttpProxyPort *
HttpProxyPort::findHttp(Group const &ports, uint16_t family)
{
  bool check_family_p = ats_is_ip(family);
  self *zret          = 0;
  for (int i = 0, n = ports.length(); i < n && !zret; ++i) {
    HttpProxyPort &p = ports[i];
    if (p.m_port &&                               // has a valid port
        TRANSPORT_DEFAULT == p.m_type &&          // is normal HTTP
        (!check_family_p || p.m_family == family) // right address family
        )
      zret = &p;
    ;
  }
  return zret;
}

char const *
HttpProxyPort::checkPrefix(char const *src, char const *prefix, size_t prefix_len)
{
  char const *zret = 0;
  if (0 == strncasecmp(prefix, src, prefix_len)) {
    src += prefix_len;
    if ('-' == *src || '=' == *src)
      ++src; // permit optional '-' or '='
    zret = src;
  }
  return zret;
}

bool
HttpProxyPort::loadConfig(Vec<self> &entries)
{
  char *text;
  bool found_p;

  text = REC_readString(PORTS_CONFIG_NAME, &found_p);
  if (found_p)
    self::loadValue(entries, text);
  ats_free(text);

  return 0 < entries.length();
}

bool
HttpProxyPort::loadDefaultIfEmpty(Group &ports)
{
  if (0 == ports.length())
    self::loadValue(ports, DEFAULT_VALUE);

  return 0 < ports.length();
}

bool
HttpProxyPort::loadValue(Vec<self> &ports, char const *text)
{
  unsigned old_port_length = ports.length(); // remember this.
  if (text && *text) {
    Tokenizer tokens(", ");
    int n_ports = tokens.Initialize(text);
    if (n_ports > 0) {
      for (int p = 0; p < n_ports; ++p) {
        char const *elt = tokens[p];
        HttpProxyPort entry;
        if (entry.processOptions(elt))
          ports.push_back(entry);
        else
          Warning("No valid definition was found in proxy port configuration element '%s'", elt);
      }
    }
  }
  return ports.length() > old_port_length; // we added at least one port.
}

bool
HttpProxyPort::processOptions(char const *opts)
{
  bool zret           = false; // found a port?
  bool af_set_p       = false; // AF explicitly specified?
  bool host_res_set_p = false; // Host resolution order set explicitly?
  bool sp_set_p       = false; // Session protocol set explicitly?
  bool bracket_p      = false; // found an open bracket in the input?
  char const *value;           // Temp holder for value of a prefix option.
  IpAddr ip;                   // temp for loading IP addresses.
  Vec<char *> values;          // Pointers to single option values.

  // Make a copy we can modify safely.
  size_t opts_len = strlen(opts) + 1;
  char *text      = static_cast<char *>(alloca(opts_len));
  memcpy(text, opts, opts_len);

  // Split the copy in to tokens.
  char *token = 0;
  for (char *spot = text; *spot; ++spot) {
    if (bracket_p) {
      if (']' == *spot)
        bracket_p = false;
    } else if (':' == *spot) {
      *spot = 0;
      token = 0;
    } else {
      if (!token) {
        token = spot;
        values.push_back(token);
      }
      if ('[' == *spot)
        bracket_p = true;
    }
  }
  if (bracket_p) {
    Warning("Invalid port descriptor '%s' - left bracket without closing right bracket", opts);
    return zret;
  }

  for (int i = 0, n_items = values.length(); i < n_items; ++i) {
    char const *item = values[i];
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
    } else if (0 != (value = this->checkPrefix(item, OPT_FD_PREFIX, OPT_FD_PREFIX_LEN))) {
      char *ptr; // tmp for syntax check.
      int fd = strtoul(value, &ptr, 10);
      if (ptr == value) {
        Warning("Mangled file descriptor value '%s' in port descriptor '%s'", item, opts);
      } else {
        m_fd = fd;
        zret = true;
      }
    } else if (0 != (value = this->checkPrefix(item, OPT_INBOUND_IP_PREFIX, OPT_INBOUND_IP_PREFIX_LEN))) {
      if (0 == ip.load(value))
        m_inbound_ip = ip;
      else
        Warning("Invalid IP address value '%s' in port descriptor '%s'", item, opts);
    } else if (0 != (value = this->checkPrefix(item, OPT_OUTBOUND_IP_PREFIX, OPT_OUTBOUND_IP_PREFIX_LEN))) {
      if (0 == ip.load(value))
        this->outboundIp(ip.family()) = ip;
      else
        Warning("Invalid IP address value '%s' in port descriptor '%s'", item, opts);
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
    } else if (0 == strcasecmp(OPT_PLUGIN, item)) {
      m_type = TRANSPORT_PLUGIN;
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
    } else if (0 != (value = this->checkPrefix(item, OPT_HOST_RES_PREFIX, OPT_HOST_RES_PREFIX_LEN))) {
      this->processFamilyPreference(value);
      host_res_set_p = true;
    } else if (0 != (value = this->checkPrefix(item, OPT_PROTO_PREFIX, OPT_PROTO_PREFIX_LEN))) {
      this->processSessionProtocolPreference(value);
      sp_set_p = true;
    } else {
      Warning("Invalid option '%s' in proxy port configuration '%s'", item, opts);
    }
  }

  bool in_ip_set_p = m_inbound_ip.isValid();

  if (af_set_p) {
    if (in_ip_set_p && m_family != m_inbound_ip.family()) {
      Warning(
        "Invalid port descriptor '%s' - the inbound adddress family [%s] is not the same type as the explicit family value [%s]",
        opts, ats_ip_family_name(m_inbound_ip.family()), ats_ip_family_name(m_family));
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
  if (!sp_set_p)
    m_session_protocol_preference = this->isSSL() ? DEFAULT_TLS_SESSION_PROTOCOL_SET : DEFAULT_NON_TLS_SESSION_PROTOCOL_SET;

  return zret;
}

void
HttpProxyPort::processFamilyPreference(char const *value)
{
  parse_host_res_preference(value, m_host_res_preference);
}

void
HttpProxyPort::processSessionProtocolPreference(char const *value)
{
  m_session_protocol_preference.markAllOut();
  globalSessionProtocolNameRegistry.markIn(value, m_session_protocol_preference);
}

void
SessionProtocolNameRegistry::markIn(char const *value, SessionProtocolSet &sp_set)
{
  int n; // # of tokens
  Tokenizer tokens(" ;|,:");

  n = tokens.Initialize(value);

  for (int i = 0; i < n; ++i) {
    char const *elt = tokens[i];

    /// Check special cases
    if (0 == strcasecmp(elt, TS_NPN_PROTOCOL_GROUP_HTTP)) {
      sp_set.markIn(HTTP_PROTOCOL_SET);
    } else if (0 == strcasecmp(elt, TS_NPN_PROTOCOL_GROUP_SPDY)) {
      sp_set.markIn(SPDY_PROTOCOL_SET);
    } else if (0 == strcasecmp(elt, TS_NPN_PROTOCOL_GROUP_HTTP2)) {
      sp_set.markIn(HTTP2_PROTOCOL_SET);
    } else { // user defined - register and mark.
      int idx = globalSessionProtocolNameRegistry.toIndex(elt);
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
  if (zret >= n)
    return n;

  if (m_outbound_ip4.isValid()) {
    if (need_colon_p)
      out[zret++] = ':';
    zret += snprintf(out + zret, n - zret, "%s=[%s]", OPT_OUTBOUND_IP_PREFIX, m_outbound_ip4.toString(ipb, sizeof(ipb)));
    need_colon_p = true;
  }
  if (zret >= n)
    return n;

  if (m_outbound_ip6.isValid()) {
    if (need_colon_p)
      out[zret++] = ':';
    zret += snprintf(out + zret, n - zret, "%s=[%s]", OPT_OUTBOUND_IP_PREFIX, m_outbound_ip6.toString(ipb, sizeof(ipb)));
    need_colon_p = true;
  }
  if (zret >= n)
    return n;

  if (0 != m_port) {
    if (need_colon_p)
      out[zret++] = ':';
    zret += snprintf(out + zret, n - zret, "%d", m_port);
    need_colon_p = true;
  }
  if (zret >= n)
    return n;

  if (ts::NO_FD != m_fd) {
    if (need_colon_p)
      out[zret++] = ':';
    zret += snprintf(out + zret, n - zret, "fd=%d", m_fd);
  }
  if (zret >= n)
    return n;

  // After this point, all of these options require other options which we've already
  // generated so all of them need a leading colon and we can stop checking for that.

  if (AF_INET6 == m_family)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_IPV6);
  if (zret >= n)
    return n;

  if (TRANSPORT_BLIND_TUNNEL == m_type)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_BLIND_TUNNEL);
  else if (TRANSPORT_SSL == m_type)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_SSL);
  else if (TRANSPORT_PLUGIN == m_type)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_PLUGIN);
  else if (TRANSPORT_COMPRESSED == m_type)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_COMPRESSED);
  if (zret >= n)
    return n;

  if (m_outbound_transparent_p && m_inbound_transparent_p)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_TRANSPARENT_FULL);
  else if (m_inbound_transparent_p)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_TRANSPARENT_INBOUND);
  else if (m_outbound_transparent_p)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_TRANSPARENT_OUTBOUND);

  if (m_transparent_passthrough)
    zret += snprintf(out + zret, n - zret, ":%s", OPT_TRANSPARENT_PASSTHROUGH);

  /* Don't print the IP resolution preferences if the port is outbound
   * transparent (which means the preference order is forced) or if
   * the order is the same as the default.
   */
  if (!m_outbound_transparent_p &&
      0 != memcmp(m_host_res_preference, host_res_default_preference_order, sizeof(m_host_res_preference))) {
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
  }

  // pull out groups.
  if (sp_set.contains(HTTP_PROTOCOL_SET)) {
    zret += snprintf(out + zret, n - zret, ":%s=%s", OPT_PROTO_PREFIX, TS_NPN_PROTOCOL_GROUP_HTTP);
    sp_set.markOut(HTTP_PROTOCOL_SET);
    need_colon_p = false;
  }
  if (sp_set.contains(SPDY_PROTOCOL_SET)) {
    if (need_colon_p)
      zret += snprintf(out + zret, n - zret, ":%s=", OPT_PROTO_PREFIX);
    else
      out[zret++] = ';';
    zret += snprintf(out + zret, n - zret, TS_NPN_PROTOCOL_GROUP_SPDY);
    sp_set.markOut(SPDY_PROTOCOL_SET);
    need_colon_p = false;
  }
  if (sp_set.contains(HTTP2_PROTOCOL_SET)) {
    if (need_colon_p)
      zret += snprintf(out + zret, n - zret, ":%s=", OPT_PROTO_PREFIX);
    else
      out[zret++] = ';';
    zret += snprintf(out + zret, n - zret, "%s", TS_NPN_PROTOCOL_GROUP_HTTP2);
    sp_set.markOut(HTTP2_PROTOCOL_SET);
    need_colon_p = false;
  }
  // now enumerate what's left.
  if (!sp_set.isEmpty()) {
    if (need_colon_p)
      zret += snprintf(out + zret, n - zret, ":%s=", OPT_PROTO_PREFIX);
    bool sep_p = !need_colon_p;
    for (int k = 0; k < SessionProtocolSet::MAX; ++k) {
      if (sp_set.contains(k)) {
        zret += snprintf(out + zret, n - zret, "%s%s", sep_p ? ";" : "", globalSessionProtocolNameRegistry.nameFor(k));
        sep_p = true;
      }
    }
  }

  return min(zret, n);
}

void
ts_host_res_global_init()
{
  // Global configuration values.
  memcpy(host_res_default_preference_order, HOST_RES_DEFAULT_PREFERENCE_ORDER, sizeof(host_res_default_preference_order));

  char *ip_resolve = REC_ConfigReadString("proxy.config.hostdb.ip_resolve");
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
  TS_NPN_PROTOCOL_INDEX_HTTP_0_9 = globalSessionProtocolNameRegistry.toIndexConst(TS_NPN_PROTOCOL_HTTP_0_9);
  TS_NPN_PROTOCOL_INDEX_HTTP_1_0 = globalSessionProtocolNameRegistry.toIndexConst(TS_NPN_PROTOCOL_HTTP_1_0);
  TS_NPN_PROTOCOL_INDEX_HTTP_1_1 = globalSessionProtocolNameRegistry.toIndexConst(TS_NPN_PROTOCOL_HTTP_1_1);
  TS_NPN_PROTOCOL_INDEX_HTTP_2_0 = globalSessionProtocolNameRegistry.toIndexConst(TS_NPN_PROTOCOL_HTTP_2_0);
  TS_NPN_PROTOCOL_INDEX_SPDY_1   = globalSessionProtocolNameRegistry.toIndexConst(TS_NPN_PROTOCOL_SPDY_1);
  TS_NPN_PROTOCOL_INDEX_SPDY_2   = globalSessionProtocolNameRegistry.toIndexConst(TS_NPN_PROTOCOL_SPDY_2);
  TS_NPN_PROTOCOL_INDEX_SPDY_3   = globalSessionProtocolNameRegistry.toIndexConst(TS_NPN_PROTOCOL_SPDY_3);
  TS_NPN_PROTOCOL_INDEX_SPDY_3_1 = globalSessionProtocolNameRegistry.toIndexConst(TS_NPN_PROTOCOL_SPDY_3_1);

  // Now do the predefined protocol sets.
  HTTP_PROTOCOL_SET.markIn(TS_NPN_PROTOCOL_INDEX_HTTP_0_9);
  HTTP_PROTOCOL_SET.markIn(TS_NPN_PROTOCOL_INDEX_HTTP_1_0);
  HTTP_PROTOCOL_SET.markIn(TS_NPN_PROTOCOL_INDEX_HTTP_1_1);
  HTTP2_PROTOCOL_SET.markIn(TS_NPN_PROTOCOL_INDEX_HTTP_2_0);
  SPDY_PROTOCOL_SET.markIn(TS_NPN_PROTOCOL_INDEX_SPDY_3);
  SPDY_PROTOCOL_SET.markIn(TS_NPN_PROTOCOL_INDEX_SPDY_3_1);

  DEFAULT_TLS_SESSION_PROTOCOL_SET.markAllIn();

  // Don't enable HTTP/2 by default until it is stable.
  int http2_enabled = 0;
  REC_ReadConfigInteger(http2_enabled, "proxy.config.http2.enabled");
  if (!http2_enabled) {
    DEFAULT_TLS_SESSION_PROTOCOL_SET.markOut(HTTP2_PROTOCOL_SET);
  }

  DEFAULT_NON_TLS_SESSION_PROTOCOL_SET = HTTP_PROTOCOL_SET;
}

SessionProtocolNameRegistry::SessionProtocolNameRegistry() : m_n(0)
{
  memset(m_names, 0, sizeof(m_names));
  memset(&m_flags, 0, sizeof(m_flags));
}

SessionProtocolNameRegistry::~SessionProtocolNameRegistry()
{
  for (size_t i = 0; i < m_n; ++i) {
    if (m_flags[i] & F_ALLOCATED)
      ats_free(const_cast<char *>(m_names[i])); // blech - ats_free won't take a char const*
  }
}

int
SessionProtocolNameRegistry::toIndex(char const *name)
{
  int zret = this->indexFor(name);
  if (INVALID == zret) {
    if (m_n < static_cast<size_t>(MAX)) {
      m_names[m_n] = ats_strdup(name);
      m_flags[m_n] = F_ALLOCATED;
      zret         = m_n++;
    } else {
      ink_release_assert(!"Session protocol name registry overflow");
    }
  }
  return zret;
}

int
SessionProtocolNameRegistry::toIndexConst(char const *name)
{
  int zret = this->indexFor(name);
  if (INVALID == zret) {
    if (m_n < static_cast<size_t>(MAX)) {
      m_names[m_n] = name;
      zret         = m_n++;
    } else {
      ink_release_assert(!"Session protocol name registry overflow");
    }
  }
  return zret;
}

int
SessionProtocolNameRegistry::indexFor(char const *name) const
{
  for (size_t i = 0; i < m_n; ++i) {
    if (0 == strcasecmp(name, m_names[i]))
      return i;
  }
  return INVALID;
}

char const *
SessionProtocolNameRegistry::nameFor(int idx) const
{
  return 0 <= idx && idx < static_cast<int>(m_n) ? m_names[idx] : 0;
}
