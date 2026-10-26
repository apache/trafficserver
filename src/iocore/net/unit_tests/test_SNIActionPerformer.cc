/** @file

  Catch based unit tests for the SNI actions that act on the connection.

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

#include "../SNIActionPerformer.h"
#include "../P_SSLNetVConnection.h"
#include "../P_SSLUtils.h"
#include "../P_UnixNetVConnection.h"
#include "iocore/net/TLSALPNSupport.h"
#include "iocore/net/TLSSNISupport.h"
#include "iocore/net/TLSTunnelSupport.h"
#include "proxy/IPAllow.h"
#include "records/RecHttp.h"
#include "tscore/ink_inet.h"

#if TS_USE_QUIC == 1
#include "../P_QUICNetVConnection.h"
#endif

#include <catch2/catch_test_macros.hpp>

#include <openssl/ssl.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>

#if TS_USE_QUIC == 1
// The look-alike connections below stand in for QUICNetVConnection, so it must have the same shape.
static_assert(std::is_convertible_v<QUICNetVConnection *, TLSSNISupport *>);
static_assert(std::is_convertible_v<QUICNetVConnection *, NetVConnection *>);
static_assert(!std::is_base_of_v<SSLNetVConnection, QUICNetVConnection>);
#endif

namespace
{
IpEndpoint
make_endpoint(const char *ip)
{
  IpEndpoint ep;

  REQUIRE(ats_ip_pton(ip, &ep) == 0);
  return ep;
}

// A connection that binds TLSSNISupport and ALPNSupport but is not an SSLNetVConnection, the way
// QUICNetVConnection is.
class NonSSLNetVConnection : public UnixNetVConnection, public ALPNSupport, public TLSSNISupport
{
public:
  void
  set_peer(const char *ip)
  {
    IpEndpoint ep = make_endpoint(ip);

    set_remote_addr(&ep.sa);
    got_remote_addr = true;
  }

  void
  bind_to(SSL *ssl)
  {
    ALPNSupport::bind(ssl, this);
    TLSSNISupport::bind(ssl, this);
  }

protected:
  in_port_t
  _get_local_port() override
  {
    return 443;
  }
};

// The same, but also binding TLSTunnelSupport, so nothing but the connection type stops a tunnel.
class NonSSLTunnelNetVConnection : public NonSSLNetVConnection, public TLSTunnelSupport
{
public:
  void
  bind_to(SSL *ssl)
  {
    NonSSLNetVConnection::bind_to(ssl);
    TLSTunnelSupport::bind(ssl, this);
  }
};

class PeerSSLNetVConnection : public SSLNetVConnection
{
public:
  void
  set_peer(const char *ip)
  {
    IpEndpoint ep = make_endpoint(ip);

    set_remote_addr(&ep.sa);
    got_remote_addr = true;
  }

  // What _bindSSLObject() binds for the slots the actions read.
  void
  bind_to(SSL *ssl)
  {
    this->ssl = ssl;
    SSLNetVCAttach(ssl, this);
    ALPNSupport::bind(ssl, this);
    TLSSNISupport::bind(ssl, this);
    TLSTunnelSupport::bind(ssl, this);
  }

  // The SSL object belongs to the test.
  ~PeerSSLNetVConnection() override { this->ssl = nullptr; }
};

// TLSSNISupport bound on its own, with no connection behind it.
class DetachedSNISupport : public TLSSNISupport
{
protected:
  in_port_t
  _get_local_port() override
  {
    return 443;
  }
};

struct SSLDeleter {
  void
  operator()(SSL *ssl) const
  {
    SSL_free(ssl);
  }
};

struct SSLCtxDeleter {
  void
  operator()(SSL_CTX *ctx) const
  {
    SSL_CTX_free(ctx);
  }
};

class SSLObject
{
public:
  SSLObject()
  {
    // The support classes reserve their ex_data slots here, and they assert on a second call.
    [[maybe_unused]] static bool const initialized = (SSLInitializeLibrary(), true);

    _ctx.reset(SSL_CTX_new(TLS_server_method()));
    REQUIRE(_ctx != nullptr);
    _ssl.reset(SSL_new(_ctx.get()));
    REQUIRE(_ssl != nullptr);
  }

  SSL *
  get() const
  {
    return _ssl.get();
  }

  SSL &
  operator*() const
  {
    return *_ssl;
  }

private:
  std::unique_ptr<SSL_CTX, SSLCtxDeleter> _ctx;
  std::unique_ptr<SSL, SSLDeleter>        _ssl;
};

// IpAllow::subjects is process-wide; set it for one test and put it back afterwards.
class ScopedAclSubjects
{
public:
  ScopedAclSubjects(std::initializer_list<uint8_t> subjects)
  {
    std::copy(std::begin(IpAllow::subjects), std::end(IpAllow::subjects), std::begin(_saved));
    std::fill(std::begin(IpAllow::subjects), std::end(IpAllow::subjects), IpAllow::Subject::MAX_SUBJECTS);
    std::copy(subjects.begin(), subjects.end(), std::begin(IpAllow::subjects));
  }

  ~ScopedAclSubjects() { std::copy(std::begin(_saved), std::end(_saved), std::begin(IpAllow::subjects)); }

private:
  uint8_t _saved[IpAllow::Subject::MAX_SUBJECTS];
};

const std::string         server_name{"allowed.example.com"};
const ActionItem::Context no_context{};
} // end anonymous namespace

TEST_CASE("SNI_IpAllow checks the peer of a connection that is not an SSLNetVConnection", "[sni][ip_allow]")
{
  ScopedAclSubjects subjects{IpAllow::Subject::PEER};
  std::string       allow_list{"10.1.0.0/16,2001:db8:1::/48"};
  SNI_IpAllow       action{allow_list, server_name};
  SSLObject         ssl;
  auto              vc = std::make_unique<NonSSLNetVConnection>();

  vc->bind_to(ssl.get());

  SECTION("an address in the list is accepted")
  {
    vc->set_peer("10.1.2.3");
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
  }

  SECTION("an address outside the list is refused")
  {
    vc->set_peer("10.2.0.1");
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }

  SECTION("an IPv6 address in the list is accepted")
  {
    vc->set_peer("2001:db8:1::5");
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
  }

  SECTION("an IPv6 address outside the list is refused")
  {
    vc->set_peer("2001:db8:2::5");
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }
}

TEST_CASE("SNI_IpAllow takes the PROXY protocol source of a connection that is not an SSLNetVConnection", "[sni][ip_allow]")
{
  ScopedAclSubjects subjects{IpAllow::Subject::PROXY, IpAllow::Subject::PEER};
  std::string       allow_list{"10.1.0.0/16"};
  SNI_IpAllow       action{allow_list, server_name};
  SSLObject         ssl;
  auto              vc = std::make_unique<NonSSLNetVConnection>();

  vc->bind_to(ssl.get());

  SECTION("an allowed PROXY source is accepted from a peer outside the list")
  {
    vc->set_peer("192.0.2.1");
    vc->set_proxy_protocol_info(
      ProxyProtocol{ProxyProtocolVersion::V1, AF_INET, make_endpoint("10.1.2.3"), make_endpoint("192.0.2.100")});
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
  }

  SECTION("a PROXY source outside the list is refused from an allowed peer")
  {
    vc->set_peer("10.1.2.3");
    vc->set_proxy_protocol_info(
      ProxyProtocol{ProxyProtocolVersion::V1, AF_INET, make_endpoint("192.0.2.9"), make_endpoint("192.0.2.100")});
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }
}

TEST_CASE("SNI_IpAllow takes the copied PROXY source as the peer of a connection that is not an SSLNetVConnection",
          "[sni][ip_allow]")
{
  ScopedAclSubjects subjects{IpAllow::Subject::PEER};
  std::string       allow_list{"10.1.0.0/16"};
  SNI_IpAllow       action{allow_list, server_name};
  SSLObject         ssl;
  auto              vc = std::make_unique<NonSSLNetVConnection>();

  vc->bind_to(ssl.get());
  vc->set_is_proxy_protocol(true, true);

  SECTION("an allowed source is accepted from a peer outside the list")
  {
    vc->set_peer("192.0.2.1");
    vc->set_proxy_protocol_info(
      ProxyProtocol{ProxyProtocolVersion::V1, AF_INET, make_endpoint("10.1.2.3"), make_endpoint("192.0.2.100")});
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
  }

  SECTION("a source outside the list is refused from an allowed peer")
  {
    vc->set_peer("10.1.2.3");
    vc->set_proxy_protocol_info(
      ProxyProtocol{ProxyProtocolVersion::V1, AF_INET, make_endpoint("192.0.2.9"), make_endpoint("192.0.2.100")});
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }
}

TEST_CASE("SNI_IpAllow falls back to the peer when PROXY is the only subject", "[sni][ip_allow]")
{
  ScopedAclSubjects subjects{IpAllow::Subject::PROXY};
  std::string       allow_list{"10.1.0.0/16"};
  SNI_IpAllow       action{allow_list, server_name};
  SSLObject         ssl;
  auto              vc = std::make_unique<NonSSLNetVConnection>();

  vc->bind_to(ssl.get());

  SECTION("an allowed peer without a PROXY header is accepted")
  {
    vc->set_peer("10.1.2.3");
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
  }

  SECTION("a refused peer without a PROXY header is refused")
  {
    vc->set_peer("10.2.0.1");
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }

  SECTION("an allowed peer behind an addressless PROXY v2 header is accepted")
  {
    vc->set_peer("10.1.2.3");
    vc->set_proxy_protocol_info(ProxyProtocol{ProxyProtocolVersion::V2, AF_UNSPEC, IpEndpoint{}, IpEndpoint{}});
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
  }

  SECTION("a refused peer behind an addressless PROXY v2 header is refused")
  {
    vc->set_peer("10.2.0.1");
    vc->set_proxy_protocol_info(ProxyProtocol{ProxyProtocolVersion::V2, AF_UNSPEC, IpEndpoint{}, IpEndpoint{}});
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }
}

TEST_CASE("SNI_IpAllow with an empty list needs no connection", "[sni][ip_allow]")
{
  std::string allow_list{""};
  SNI_IpAllow action{allow_list, server_name};
  SSLObject   ssl;

  CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
}

TEST_CASE("SNI_IpAllow refuses when it cannot find the connection", "[sni][ip_allow]")
{
  ScopedAclSubjects subjects{IpAllow::Subject::PEER};
  std::string       allow_list{"0.0.0.0/0,::/0"};
  SNI_IpAllow       action{allow_list, server_name};
  SSLObject         ssl;

  SECTION("nothing is bound to the SSL object")
  {
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }

  SECTION("the bound TLSSNISupport is not a connection")
  {
    DetachedSNISupport snis;

    TLSSNISupport::bind(ssl.get(), &snis);
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
    TLSSNISupport::unbind(ssl.get());
  }
}

TEST_CASE("SNI_IpAllow still checks the peer of an SSLNetVConnection", "[sni][ip_allow]")
{
  ScopedAclSubjects subjects{IpAllow::Subject::PEER};
  std::string       allow_list{"10.1.0.0/16"};
  SNI_IpAllow       action{allow_list, server_name};
  SSLObject         ssl;
  auto              vc = std::make_unique<PeerSSLNetVConnection>();

  vc->bind_to(ssl.get());

  SECTION("an address in the list is accepted")
  {
    vc->set_peer("10.1.2.3");
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
  }

  SECTION("an address outside the list is refused")
  {
    vc->set_peer("10.2.0.1");
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }
}

TEST_CASE("VerifyClient refuses a connection that is not an SSLNetVConnection when it has to verify", "[sni][verify_client]")
{
  SSLObject ssl;
  auto      vc = std::make_unique<NonSSLNetVConnection>();

  vc->bind_to(ssl.get());

  SECTION("STRICT is refused")
  {
    VerifyClient action{uint8_t{2}, "", ""};
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }

  SECTION("MODERATE is refused")
  {
    VerifyClient action{uint8_t{1}, "", ""};
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }

  SECTION("NONE is applied over a stricter default")
  {
    VerifyClient action{uint8_t{0}, "", ""};

    SSL_set_verify(ssl.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
    CHECK(SSL_get_verify_mode(ssl.get()) == SSL_VERIFY_NONE);
  }
}

TEST_CASE("VerifyClient still applies the level to an SSLNetVConnection", "[sni][verify_client]")
{
  SSLObject ssl;
  auto      vc = std::make_unique<PeerSSLNetVConnection>();

  vc->bind_to(ssl.get());

  SECTION("STRICT")
  {
    VerifyClient action{uint8_t{2}, "", ""};
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
    CHECK(SSL_get_verify_mode(ssl.get()) == (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE));
  }

  SECTION("MODERATE")
  {
    VerifyClient action{uint8_t{1}, "", ""};
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
    CHECK(SSL_get_verify_mode(ssl.get()) == (SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE));
  }

  SECTION("NONE")
  {
    VerifyClient action{uint8_t{0}, "", ""};

    SSL_set_verify(ssl.get(), SSL_VERIFY_PEER, nullptr);
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
    CHECK(SSL_get_verify_mode(ssl.get()) == SSL_VERIFY_NONE);
  }
}

TEST_CASE("VerifyClient with nothing bound to the SSL object", "[sni][verify_client]")
{
  SSLObject ssl;

  SECTION("STRICT is refused")
  {
    VerifyClient action{uint8_t{2}, "", ""};
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }

  SECTION("NONE is applied")
  {
    VerifyClient action{uint8_t{0}, "", ""};

    SSL_set_verify(ssl.get(), SSL_VERIFY_PEER, nullptr);
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
    CHECK(SSL_get_verify_mode(ssl.get()) == SSL_VERIFY_NONE);
  }
}

TEST_CASE("TunnelDestination refuses a connection that is not an SSLNetVConnection", "[sni][tunnel_route]")
{
  SSLObject ssl;

  SECTION("without TLS tunnel support")
  {
    auto              vc = std::make_unique<NonSSLNetVConnection>();
    TunnelDestination action{"tunnel.example.com:443", SNIRoutingType::BLIND, YamlSNIConfig::TunnelPreWarm::UNSET, {}};

    vc->bind_to(ssl.get());
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }

  SECTION("with TLS tunnel support")
  {
    auto              vc = std::make_unique<NonSSLTunnelNetVConnection>();
    TunnelDestination action{"tunnel.example.com:443", SNIRoutingType::FORWARD, YamlSNIConfig::TunnelPreWarm::UNSET, {}};

    vc->bind_to(ssl.get());
    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
    CHECK_FALSE(vc->has_tunnel_destination());
  }

  SECTION("nothing is bound to the SSL object")
  {
    TunnelDestination action{"tunnel.example.com:443", SNIRoutingType::BLIND, YamlSNIConfig::TunnelPreWarm::UNSET, {}};

    CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_ALERT_FATAL);
  }
}

TEST_CASE("TunnelDestination still routes an SSLNetVConnection", "[sni][tunnel_route]")
{
  SSLObject         ssl;
  auto              vc = std::make_unique<PeerSSLNetVConnection>();
  TunnelDestination action{"tunnel.example.com:443", SNIRoutingType::BLIND, YamlSNIConfig::TunnelPreWarm::UNSET, {}};

  vc->bind_to(ssl.get());

  CHECK(action.SNIAction(*ssl, no_context) == SSL_TLSEXT_ERR_OK);
  CHECK(vc->attributes == HttpProxyPort::TRANSPORT_BLIND_TUNNEL);
  CHECK(vc->has_tunnel_destination());
  CHECK(vc->get_tunnel_host() == "tunnel.example.com");
}
