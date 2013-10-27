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

#ifndef _I_REC_HTTP_H
#define _I_REC_HTTP_H

#include <ts/ink_inet.h>
#include <ts/ink_resolver.h>
#include <ts/Vec.h>

/// Load default inbound IP addresses from the configuration file.
void RecHttpLoadIp(
  char const* name,    ///< Name of value in configuration file.
  IpAddr& ip4, ///< [out] IPv4 address.
  IpAddr& ip6  ///< [out] Ipv6 address.
);

/** Description of an proxy port.

    This consolidates the options needed for proxy ports, both data
    and parsing. It provides a static global set of ports for
    convenience although it can be used with an externally provided
    set.

    Options are described by a colon separated list of keywords
    without spaces. The options are applied in left to right order. If
    options do not conflict the order is irrelevant.

    Current supported options (case insensitive):

    - ipv4 : Use IPv4.
    - ipv6 : Use IPv6.
    - ssl : SSL port.
    - compressed : Compressed data.
    - blind : Blind tunnel.
    - tr-in : Inbound transparent (ignored if @c full is set).
    - tr-out : Outbound transparent (ignored if @c full is set).
    - tr-full : Fully transparent (inbound and outbound). Equivalent to "tr-in:tr-out".
    - [number] : Port number.
    - fd[number] : File descriptor.
    - ip-in[IP addr] : Address to bind for inbound connections.
    - ip-out[IP addr]: Address to bind for outbound connections.

    For example, the string "ipv6:8080:full" means "Listen on port
    8080 using IPv6 and full transparency". This is the same as
    "8080:full:ipv6". The only option active by default is @c
    ipv4. All others must be explicitly enabled. The port number
    option is the only required option.

    If @c ip-in or @c ip-out is used, the address family must agree
    with the @c ipv4 or @c ipv6 option. If the address is IPv6, it
    must be enclosed with brackets '[]' to distinguish its colons from
    the value separating colons. An IPv4 may be enclosed in brackets
    for constistency.

    @note The previous notation is supported but deprecated.

    @internal This is intended to replace the current bifurcated
    processing that happens in Manager and Server. It also changes the
    syntax so that a useful set of options can be supported and easily
    extended as needed. Note that all options must start with a letter
    - starting with a digit is reserved for the port value. Options
    must not contain spaces or punctuation other than '-' and '_'.
 */
struct HttpProxyPort {
private:
  typedef HttpProxyPort self; ///< Self reference type.
public:
  /// Explicitly supported collection of proxy ports.
  typedef Vec<self> Group;

  /// Type of transport on the connection.
  enum TransportType {
    TRANSPORT_DEFAULT = 0, ///< Default (normal HTTP).
    TRANSPORT_COMPRESSED, ///< Compressed HTTP.
    TRANSPORT_BLIND_TUNNEL, ///< Blind tunnel (no processing).
    TRANSPORT_SSL, ///< SSL connection.
    TRANSPORT_PLUGIN /// < Protocol plugin connection
  };

  int m_fd; ///< Pre-opened file descriptor if present.
  TransportType m_type; ///< Type of connection.
  int m_port; ///< Port on which to listen.
  unsigned int m_family; ///< IP address family.
  /// True if inbound connects (from client) are transparent.
  bool m_inbound_transparent_p;
  /// True if outbound connections (to origin servers) are transparent.
  bool m_outbound_transparent_p;
  // True if transparent pass-through is enabled on this port.
  bool m_transparent_passthrough;
  /// Local address for inbound connections (listen address).
  IpAddr m_inbound_ip;
  /// Local address for outbound connections (to origin server).
  IpAddr m_outbound_ip4;
  /// Local address for outbound connections (to origin server).
  IpAddr m_outbound_ip6;
  /// Ordered preference for DNS resolution family ( @c FamilyPrefence )
  /// A value of @c PREFER_NONE indicates that entry and subsequent ones
  /// are invalid.
  HostResPreferenceOrder m_host_res_preference;
  /// Static preference list that is the default value.
  static HostResPreferenceOrder const DEFAULT_HOST_RES_PREFERENCE;

  /// Default constructor.
  HttpProxyPort();

  /** Select the local outbound address object.

      @return The IP address for @a family
  */
  IpAddr& outboundIp(
    uint16_t family ///< IP address family.
  );

  /// Check for SSL port.
  bool isSSL() const;

  /// Check for SSL port.
  bool isPlugin() const;

  /// Process options text.
  /// @a opts should not contain any whitespace, only the option string.
  /// This object's internal state is updated as specified by @a opts.
  /// @return @c true if a port option was successfully processed, @c false otherwise.
  bool processOptions(
    char const* opts ///< String containing the options.
  );

  /** Global instance.

      In general this data needs to be loaded only once. To support
      that a global instance is provided. If accessed, it will
      automatically load itself from the configuration data if not
      already loaded.
  */
  static Vec<self>& global();

  /// Check for SSL ports.
  /// @return @c true if any port in @a ports is an SSL port.
  static bool hasSSL(
		     Group const& ports ///< Ports to check.
		     );

  /// Check for SSL ports.
  /// @return @c true if any global port is an SSL port.
  static bool hasSSL();

  /** Load all relevant configuration data.

      This is hardwired to look up the appropriate values in the
      configuration files. It clears @a ports and then loads all found
      values in to it.

      @return @c true if at least one valid port description was
      found, @c false if none.
  */
  static bool loadConfig(
    Vec<self>& ports ///< Destination for found port data.
  );

  /** Load all relevant configuration data into the global ports.

      @return @c true if at least one valid port description was
      found, @c false if none.
  */
  static bool loadConfig();

  /** Load ports from a value string.

      Load ports from single string with port descriptors. Ports
      found are added to @a ports. @a value may safely be @c NULL or empty.

      @note This is used primarily internally but is available if needed.
      @return @c true if a valid port was found, @c false if none.
  */
  static bool loadValue(
    Vec<self>& ports, ///< Destination for found port data.
    char const* value ///< Source port data.
  );

  /** Load ports from a value string into the global ports.

      Load ports from single string of port descriptors into the
      global set of ports. @a value may safely be @c NULL or empty.

      @return @c true if a valid port was found, @c false if none.
  */
  static bool loadValue(
    char const* value ///< Source port data.
  );

  /// Load default value if @a ports is empty.
  /// @return @c true if the default was needed / loaded.
  static bool loadDefaultIfEmpty(
    Vec<self>& ports ///< Load target.
  );

  /// Load default value into the global set if it is empty.
  /// @return @c true if the default was needed / loaded.
  static bool loadDefaultIfEmpty();

  /** Find an HTTP port in @a ports.
      If @a family is specified then only ports for that family
      are checked.
      @return The port if found, @c NULL if not.
  */
  static self* findHttp(
			Group const& ports, ///< Group to search.
			uint16_t family = AF_UNSPEC  ///< Desired address family.
			);

  /** Find an HTTP port in the global ports.
      If @a family is specified then only ports for that family
      are checked.
      @return The port if found, @c NULL if not.
  */
  static self* findHttp(uint16_t family = AF_UNSPEC);

  /** Create text description to be used for inter-process access.
      Prints the file descriptor and then any options.

      @return The number of characters used for the description.
  */
  int print(
    char* out, ///< Output string.
    size_t n ///< Maximum output length.
  );

  static char const* const PORTS_CONFIG_NAME; ///< New unified port descriptor.

  /// Default value if no other values can be found.
  static char const* const DEFAULT_VALUE;

  // Keywords (lower case versions, but compares should be case insensitive)
  static char const* const OPT_FD_PREFIX; ///< Prefix for file descriptor value.
  static char const* const OPT_OUTBOUND_IP_PREFIX; ///< Prefix for inbound IP address.
  static char const* const OPT_INBOUND_IP_PREFIX; ///< Prefix for outbound IP address.
  static char const* const OPT_IPV6; ///< IPv6.
  static char const* const OPT_IPV4; ///< IPv4
  static char const* const OPT_TRANSPARENT_INBOUND; ///< Inbound transparent.
  static char const* const OPT_TRANSPARENT_OUTBOUND; ///< Outbound transparent.
  static char const* const OPT_TRANSPARENT_FULL; ///< Full transparency.
  static char const* const OPT_TRANSPARENT_PASSTHROUGH; ///< Pass-through non-HTTP.
  static char const* const OPT_SSL; ///< SSL (experimental)
  static char const* const OPT_PLUGIN; ///< Protocol Plugin handle (experimental)
  static char const* const OPT_BLIND_TUNNEL; ///< Blind tunnel.
  static char const* const OPT_COMPRESSED; ///< Compressed.
  static char const* const OPT_HOST_RES_PREFIX; ///< Set DNS family preference.

  static Vec<self>& m_global; ///< Global ("default") data.

protected:
  /// Process @a value for DNS resolution family preferences.
  void processFamilyPreferences(char const* value);
};

inline bool HttpProxyPort::isSSL() const { return TRANSPORT_SSL == m_type; }
inline bool HttpProxyPort::isPlugin() const { return TRANSPORT_PLUGIN == m_type; }

inline IpAddr&
HttpProxyPort::outboundIp(uint16_t family) {
  static IpAddr invalid; // dummy to make compiler happy about return.
  if (AF_INET == family) return m_outbound_ip4;
  else if (AF_INET6 == family) return m_outbound_ip6;
  ink_release_assert(!"Invalid family for outbound address on proxy port.");
  return invalid; // never happens but compiler insists.
}

inline bool
HttpProxyPort::loadValue(char const* value) {
  return self::loadValue(m_global, value);
}
inline bool
HttpProxyPort::loadConfig() {
  return self::loadConfig(m_global);
}
inline bool
HttpProxyPort::loadDefaultIfEmpty() {
  return self::loadDefaultIfEmpty(m_global);
}
inline Vec<HttpProxyPort>&
HttpProxyPort::global() {
  return m_global;
}
inline bool
HttpProxyPort::hasSSL() {
  return self::hasSSL(m_global);
}
inline HttpProxyPort* HttpProxyPort::findHttp(uint16_t family) {
  return self::findHttp(m_global, family);
}

#endif // I_REC_HTTP_H
