/** @file

  NetVConnection options class

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
#pragma once
#include "tscore/ink_inet.h"
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/net/YamlSNIConfig.h"

#include <cstdint>

struct NetVCOptions {
  using self = NetVCOptions; ///< Self reference type.

  /// Values for valid IP protocols.
  enum ip_protocol_t {
    USE_TCP, ///< TCP protocol.
    USE_UDP  ///< UDP protocol.
  };

  /// IP (TCP or UDP) protocol to use on socket.
  ip_protocol_t ip_proto;

  /** IP address family.

      This is used for inbound connections only if @c local_ip is not
      set, which is sometimes more convenient for the client. This
      defaults to @c AF_INET so if the client sets neither this nor @c
      local_ip then IPv4 is used.

      For outbound connections this is ignored and the family of the
      remote address used.

      @note This is (inconsistently) called "domain" and "protocol" in
      other places. "family" is used here because that's what the
      standard IP data structures use.

  */
  uint16_t ip_family;

  /** The set of ways in which the local address should be bound.

      The protocol is set by the contents of @a local_addr regardless
      of this value. @c ANY_ADDR will override only the address.

      @note The difference between @c INTF_ADDR and @c FOREIGN_ADDR is
      whether transparency is enabled on the socket. It is the
      client's responsibility to set this correctly based on whether
      the address in @a local_addr is associated with an interface on
      the local system ( @c INTF_ADDR ) or is owned by a foreign
      system ( @c FOREIGN_ADDR ).  A binding style of @c ANY_ADDR
      causes the value in @a local_addr to be ignored.

      The IP address and port are separate because most clients treat
      these independently. For the same reason @c IpAddr is used
      to be clear that it contains no port data.

      @see local_addr
      @see addr_binding
   */
  enum addr_bind_style {
    ANY_ADDR,    ///< Bind to any available local address (don't care, default).
    INTF_ADDR,   ///< Bind to interface address in @a local_addr.
    FOREIGN_ADDR ///< Bind to foreign address in @a local_addr.
  };

  /** Local address for the connection.

      For outbound connections this must have the same family as the
      remote address (which is not stored in this structure). For
      inbound connections the family of this value overrides @a
      ip_family if set.

      @note Ignored if @a addr_binding is @c ANY_ADDR.
      @see addr_binding
      @see ip_family
  */
  IpAddr local_ip;

  /** Local port for connection.
      Set to 0 for "don't care" (default).
  */
  uint16_t local_port;

  /// How to bind the local address.
  /// @note Default is @c ANY_ADDR.
  addr_bind_style addr_binding;

  /// Make the socket blocking on I/O (default: @c false)
  // TODO: make this const.  We don't use blocking
  bool f_blocking = false;
  /// Make socket block on connect (default: @c false)
  // TODO: make this const.  We don't use blocking
  bool f_blocking_connect = false;

  // Use TCP Fast Open on this socket. The connect(2) call will be omitted.
  bool f_tcp_fastopen = false;

  /// Control use of SOCKS.
  /// Set to @c NO_SOCKS to disable use of SOCKS. Otherwise SOCKS is
  /// used if available.
  unsigned char socks_support;
  /// Version of SOCKS to use.
  unsigned char socks_version;

  int socket_recv_bufsize;
  int socket_send_bufsize;

  /// Configuration options for sockets.
  /// @note These are not identical to internal socket options but
  /// specifically defined for configuration. These are mask values
  /// and so must be powers of 2.
  uint32_t sockopt_flags;
  /// Value for TCP no delay for @c sockopt_flags.
  static uint32_t const SOCK_OPT_NO_DELAY = 1;
  /// Value for keep alive for @c sockopt_flags.
  static uint32_t const SOCK_OPT_KEEP_ALIVE = 2;
  /// Value for linger on for @c sockopt_flags
  static uint32_t const SOCK_OPT_LINGER_ON = 4;
  /// Value for TCP Fast open @c sockopt_flags
  static uint32_t const SOCK_OPT_TCP_FAST_OPEN = 8;
  /// Value for SO_MARK @c sockopt_flags
  static uint32_t const SOCK_OPT_PACKET_MARK = 16;
  /// Value for IP_TOS @c sockopt_flags
  static uint32_t const SOCK_OPT_PACKET_TOS = 32;
  /// Value for TCP_NOTSENT_LOWAT @c sockopt_flags
  static uint32_t const SOCK_OPT_TCP_NOTSENT_LOWAT = 64;
  /// Value for SO_INCOMING_CPU @c sockopt_flags
  static uint32_t const SOCK_OPT_INCOMING_CPU = 128;

  uint32_t packet_mark;
  uint32_t packet_tos;
  uint32_t packet_notsent_lowat;

  EventType etype;

  /** ALPN protocol-lists. The format is OpenSSL protocol-lists format (vector of 8-bit length-prefixed, byte strings)
      https://www.openssl.org/docs/manmaster/man3/SSL_CTX_set_alpn_protos.html
   */
  std::string_view alpn_protos;
  /** Server name to use for SNI data on an outbound connection.
   */
  ats_scoped_str sni_servername;
  /** FQDN used to connect to the origin.  May be different
   * than sni_servername if pristine host headers are used
   */
  ats_scoped_str ssl_servername;

  /** Server host name from client's request to use for SNI data on an outbound connection.
   */
  ats_scoped_str sni_hostname;

  /**
   * Client certificate to use in response to OS's certificate request
   */
  ats_scoped_str ssl_client_cert_name;
  /*
   * File containing private key matching certificate
   */
  const char *ssl_client_private_key_name = nullptr;
  /*
   * File containing CA certs for verifying origin's cert
   */
  const char *ssl_client_ca_cert_name = nullptr;
  /*
   * Directory containing CA certs for verifying origin's cert
   */
  const char *ssl_client_ca_cert_path = nullptr;

  bool tls_upstream = false;

  unsigned char alpn_protocols_array[MAX_ALPN_STRING];
  int           alpn_protocols_array_size = 0;

  /**
   * Set to DISABLED, PERFMISSIVE, or ENFORCED
   * Controls how the server certificate verification is handled
   */
  YamlSNIConfig::Policy verifyServerPolicy = YamlSNIConfig::Policy::DISABLED;

  /**
   * Bit mask of which features of the server certificate should be checked
   * Currently SIGNATURE and NAME
   */
  YamlSNIConfig::Property verifyServerProperties = YamlSNIConfig::Property::NONE;

  /// Reset all values to defaults.
  void reset();

  void set_sock_param(int _recv_bufsize, int _send_bufsize, unsigned long _opt_flags, unsigned long _packet_mark = 0,
                      unsigned long _packet_tos = 0, unsigned long _packet_notsent_lowat = 0);

  NetVCOptions() { reset(); }
  ~NetVCOptions() {}

  /** Set the SNI server name.
      A local copy is made of @a name.
  */
  self &
  set_sni_servername(const char *name, size_t len)
  {
    IpEndpoint ip;

    // Literal IPv4 and IPv6 addresses are not permitted in "HostName".(rfc6066#section-3)
    if (name && len && ats_ip_pton(std::string_view(name, len), &ip) != 0) {
      sni_servername = ats_strndup(name, len);
    } else {
      sni_servername = nullptr;
    }
    return *this;
  }

  self &
  set_ssl_client_cert_name(const char *name)
  {
    if (name) {
      ssl_client_cert_name = ats_strdup(name);
    } else {
      ssl_client_cert_name = nullptr;
    }
    return *this;
  }

  self &
  set_ssl_servername(const char *name)
  {
    if (name) {
      ssl_servername = ats_strdup(name);
    } else {
      ssl_servername = nullptr;
    }
    return *this;
  }

  self &
  set_sni_hostname(const char *name, size_t len)
  {
    IpEndpoint ip;

    // Literal IPv4 and IPv6 addresses are not permitted in "HostName".(rfc6066#section-3)
    if (name && len && ats_ip_pton(std::string_view(name, len), &ip) != 0) {
      sni_hostname = ats_strndup(name, len);
    } else {
      sni_hostname = nullptr;
    }
    return *this;
  }

  self &
  operator=(self const &that)
  {
    if (&that != this) {
      /*
       * It is odd but necessary to null the scoped string pointer here
       * and then explicitly call release on them in the string assignments
       * below.
       * We a memcpy from that to this.  This will put that's string pointers into
       * this's memory.  Therefore we must first explicitly null out
       * this's original version of the string.  The release after the
       * memcpy removes the extra reference to that's copy of the string
       * Removing the release will eventually cause a double free crash
       */
      sni_servername       = nullptr; // release any current name.
      ssl_servername       = nullptr;
      sni_hostname         = nullptr;
      ssl_client_cert_name = nullptr;
      memcpy(static_cast<void *>(this), &that, sizeof(self));
      if (that.sni_servername) {
        sni_servername.release(); // otherwise we'll free the source string.
        this->sni_servername = ats_strdup(that.sni_servername);
      }
      if (that.ssl_servername) {
        ssl_servername.release(); // otherwise we'll free the source string.
        this->ssl_servername = ats_strdup(that.ssl_servername);
      }
      if (that.sni_hostname) {
        sni_hostname.release(); // otherwise we'll free the source string.
        this->sni_hostname = ats_strdup(that.sni_hostname);
      }
      if (that.ssl_client_cert_name) {
        this->ssl_client_cert_name.release(); // otherwise we'll free the source string.
        this->ssl_client_cert_name = ats_strdup(that.ssl_client_cert_name);
      }
    }
    return *this;
  }

  std::string_view get_family_string() const;

  std::string_view get_proto_string() const;

  /// @name Debugging
  //@{
  /// Convert @a s to its string equivalent.
  static const char *toString(addr_bind_style s);
  //@}

  // noncopyable
  NetVCOptions(const NetVCOptions &) = delete;
};
