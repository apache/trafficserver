/**

  @par License

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

#include "P_SSLCertLookup.h"
#include "iocore/net/SSLTypes.h"
#include "iocore/net/TLSBasicSupport.h"
#include "swoc/MemSpan.h"
#include "tscore/Diags.h"
#include "tscore/Ptr.h"

#ifdef OPENSSL_IS_AT_LEAST_OPENSSL3
#include <openssl/decoder.h>
#include <openssl/evp.h>
#endif
#define OPENSSL_THREAD_DEFINES
#if __has_include(<openssl/opensslconf.h>)
#include <openssl/opensslconf.h>
#endif
#include <openssl/ssl.h>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <utility>

class SSLNetVConnection;

using ssl_error_t = int;

/// Return the SSL Curve ID associated with the specified SSL connection
ssl_curve_id SSLGetCurveNID(SSL *ssl);

/// Return the TLS Group Name associated with the specified SSL connection.
std::string_view SSLGetGroupName(SSL *ssl);

/// Format TLS signature-scheme code points as dash-separated decimal values, excluding GREASE.
std::string SSLFormatSignatureAlgorithms(swoc::MemSpan<uint16_t const> algorithms);

/// Return the peer's offered TLS signature schemes in wire order.
std::string SSLGetOfferedSignatureAlgorithms(SSL *ssl);

/// Return the TLS signature scheme used locally to authenticate the current handshake.
std::string SSLGetNegotiatedSignatureAlgorithm(SSL *ssl);

/// Capture TLS signature information while the TLS library still exposes the selected scheme.
void SSLHandshakeInfoCallback(const SSL *ssl, int where, int ret);

SSL_SESSION *SSLSessionDup(SSL_SESSION *sess);

enum class SSLCertContextType;

struct SSLLoadingContext {
  SSL_CTX           *ctx;
  SSLCertContextType ctx_type;

  explicit SSLLoadingContext(SSL_CTX *c, SSLCertContextType ctx_type) : ctx(c), ctx_type(ctx_type) {}
};

// Create a new SSL server context fully configured (cert and keys are optional).
// Used by TS API (TSSslServerContextCreate and TSSslServerCertUpdate)
SSL_CTX *SSLCreateServerContext(const SSLConfigParams *params, const SSLMultiCertConfigParams *sslMultiCertSettings,
                                const char *cert_path = nullptr, const char *key_path = nullptr);

// Release SSL_CTX and the associated data. This works for both
// client and server contexts and gracefully accepts nullptr.
// Used by TS API (TSSslContextDestroy)
void SSLReleaseContext(SSL_CTX *ctx);

// Initialize the SSL library.
void SSLInitializeLibrary();

// Initialize SSL library based on configuration settings
void SSLPostConfigInitialize();

// Attach a SSL NetVC back pointer to a SSL session.
void SSLNetVCAttach(SSL *ssl, SSLNetVConnection *vc);

// Detach from SSL NetVC back pointer to a SSL session.
void SSLNetVCDetach(SSL *ssl);

// Return the SSLNetVConnection (if any) attached to this SSL session.
SSLNetVConnection *SSLNetVCAccess(const SSL *ssl);

void setClientCertLevel(SSL *ssl, uint8_t certLevel);
void setClientCertCACerts(SSL *ssl, const char *file, const char *dir);

/** The client credentials a connection trusts beyond the global X.509 settings: those of the sni.yaml
    verify_client action that covered it, or else the raw public keys proxy.config.ssl.CA.rpk.filename
    names.

    Built once when the configuration is loaded and shared by every connection it covers. Each
    connection holds a reference, so a reload during a handshake cannot free it.
 */
struct ClientTrust : public RefCountObjInHeap {
  ClientTrust(std::shared_ptr<const SSLRPKUtils::TrustedKeySet> keys, std::string digest, bool x509)
    : rpk_keys(std::move(keys)), rpk_digest(std::move(digest)), x509(x509)
  {
  }

  /// The raw public keys a client may present, or nullptr if the connection accepts none.
  std::shared_ptr<const SSLRPKUtils::TrustedKeySet> const rpk_keys;
  /// Identifies rpk_keys in the session id context, taken once when the set was loaded.
  std::string const rpk_digest;
  /// Whether an X.509 client certificate may authenticate at all. False where a host names only raw
  /// public keys; that set then replaces proxy.config.ssl.CA.cert.filename as well.
  bool const x509;
};

/** Attach @a trust to @a ssl's connection, in place of the global settings and of any trust attached
    earlier.

    @param ssl The connection the trust applies to.
    @param trust The trust to attach; the connection takes a reference to it.
    @return false if the trust could not be attached, or raw public keys were given to a build
      without raw public key support.
 */
bool setClientTrust(SSL *ssl, ClientTrust *trust);

/** Digest the client CA scope named by @a file and @a dir, for the session id context.

    @return The digest, or no value when neither location is set. A set location that cannot be read
      also yields no value, with @a failed set, so the caller refuses to resume rather than falling
      back to a different scope.
 */
std::optional<TLSBasicSupport::VerifyScopeDigest> digestClientCAScope(std::string_view file, std::string_view dir, bool &failed);
void                                              setTLSValidProtocols(SSL *ssl, unsigned long proto_mask, unsigned long max_mask);

// Helper functions to retrieve sni name or ip address from a SSL object
// Used as part of the lookup key into the origin server session cache
std::string get_sni_addr(SSL *ssl);

// Helper functions to retrieve server verify policy and properties from a SSL object
// Used as part of the lookup key into the origin server session cache
std::string get_verify_str(SSL *ssl);

namespace ssl
{
namespace detail
{
  struct X509Deleter {
    void
    operator()(X509 *p)
    {
      X509_free(p);
    }
  };

  struct BIODeleter {
    void
    operator()(BIO *p)
    {
      BIO_free(p);
    }
  };

  struct PKEYDeleter {
    void
    operator()(EVP_PKEY *p)
    {
      EVP_PKEY_free(p);
    }
  };

#ifdef OPENSSL_IS_AT_LEAST_OPENSSL3
  struct PKEYCTXDeleter {
    void
    operator()(EVP_PKEY_CTX *pctx)
    {
      EVP_PKEY_CTX_free(pctx);
    }
  };

  struct DecoderCTXDeleter {
    void
    operator()(OSSL_DECODER_CTX *dctx)
    {
      OSSL_DECODER_CTX_free(dctx);
    }
  };
#endif

} // namespace detail
} // namespace ssl

struct ats_wildcard_matcher {
  ats_wildcard_matcher()
  {
    if (!regex.compile(R"(^\*\.[^\*.]+)")) {
      Fatal("failed to compile TLS wildcard matching regex");
    }
  }

  ~ats_wildcard_matcher() {}
  bool
  match(const char *hostname) const
  {
    return regex.exec(hostname);
  }

private:
  Regex regex;
};

using scoped_X509 = std::unique_ptr<X509, ssl::detail::X509Deleter>;
using scoped_BIO  = std::unique_ptr<BIO, ssl::detail::BIODeleter>;
using scoped_PKEY = std::unique_ptr<EVP_PKEY, ssl::detail::PKEYDeleter>;
#ifdef OPENSSL_IS_AT_LEAST_OPENSSL3
using scoped_PKEY_CTX    = std::unique_ptr<EVP_PKEY_CTX, ssl::detail::PKEYCTXDeleter>;
using scoped_Decoder_CTX = std::unique_ptr<OSSL_DECODER_CTX, ssl::detail::DecoderCTXDeleter>;
#endif
