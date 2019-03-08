/**

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

#define OPENSSL_THREAD_DEFINES

// BoringSSL does not have this include file
#ifndef OPENSSL_IS_BORINGSSL
#include <openssl/opensslconf.h>
#endif
#include <openssl/ssl.h>

#include "tscore/ink_config.h"
#include "tscore/Diags.h"

#include "P_SSLClientUtils.h"
#include "P_SSLCertLookup.h"

struct SSLConfigParams;
class SSLNetVConnection;

typedef int ssl_error_t;

/**
   @brief Gather user provided settings from ssl_multicert.config in to this single struct
 */
struct SSLMultiCertConfigParams {
  SSLMultiCertConfigParams() : opt(SSLCertContext::OPT_NONE)
  {
    REC_ReadConfigInt32(session_ticket_enabled, "proxy.config.ssl.server.session_ticket.enable");
  }

  int session_ticket_enabled; ///< session ticket enabled
  ats_scoped_str addr;        ///< IPv[64] address to match
  ats_scoped_str cert;        ///< certificate
  ats_scoped_str first_cert;  ///< the first certificate name when multiple cert files are in 'ssl_cert_name'
  ats_scoped_str ca;          ///< CA public certificate
  ats_scoped_str key;         ///< Private key
  ats_scoped_str dialog;      ///< Private key dialog
  ats_scoped_str servername;  ///< Destination server
  SSLCertContext::Option opt; ///< SSLCertContext special handling option
};

/**
    @brief Load SSL certificates from ssl_multicert.config and setup SSLCertLookup for SSLCertificateConfig
 */
class SSLMultiCertConfigLoader
{
public:
  SSLMultiCertConfigLoader(const SSLConfigParams *p) : _params(p) {}
  virtual ~SSLMultiCertConfigLoader(){};

  bool load(SSLCertLookup *lookup);

  virtual SSL_CTX *default_server_ssl_ctx();
  virtual SSL_CTX *init_server_ssl_ctx(std::vector<X509 *> &certList, const SSLMultiCertConfigParams *sslMultCertSettings);

  static bool load_certs(SSL_CTX *ctx, std::vector<X509 *> &certList, const SSLConfigParams *params,
                         const SSLMultiCertConfigParams *ssl_multi_cert_params);
  static bool set_session_id_context(SSL_CTX *ctx, const SSLConfigParams *params,
                                     const SSLMultiCertConfigParams *sslMultCertSettings);

  static bool index_certificate(SSLCertLookup *lookup, SSLCertContext const &cc, X509 *cert, const char *certname);
  static int check_server_cert_now(X509 *cert, const char *certname);
  static void clear_pw_references(SSL_CTX *ssl_ctx);

protected:
  const SSLConfigParams *_params;

private:
  virtual SSL_CTX *_store_ssl_ctx(SSLCertLookup *lookup, const SSLMultiCertConfigParams *ssl_multi_cert_params);
  virtual void _set_handshake_callbacks(SSL_CTX *ctx);
};

// Create a new SSL server context fully configured.
// Used by TS API (TSSslServerContextCreate)
SSL_CTX *SSLCreateServerContext(const SSLConfigParams *params);

// Release SSL_CTX and the associated data. This works for both
// client and server contexts and gracefully accepts nullptr.
// Used by TS API (TSSslContextDestroy)
void SSLReleaseContext(SSL_CTX *ctx);

// Initialize the SSL library.
void SSLInitializeLibrary();

// Initialize SSL library based on configuration settings
void SSLPostConfigInitialize();

// Wrapper functions to SSL I/O routines
ssl_error_t SSLWriteBuffer(SSL *ssl, const void *buf, int64_t nbytes, int64_t &nwritten);
ssl_error_t SSLReadBuffer(SSL *ssl, void *buf, int64_t nbytes, int64_t &nread);
ssl_error_t SSLAccept(SSL *ssl);
ssl_error_t SSLConnect(SSL *ssl);

// Attach a SSL NetVC back pointer to a SSL session.
void SSLNetVCAttach(SSL *ssl, SSLNetVConnection *vc);

// Detach from SSL NetVC back pointer to a SSL session.
void SSLNetVCDetach(SSL *ssl);

// Return the SSLNetVConnection (if any) attached to this SSL session.
SSLNetVConnection *SSLNetVCAccess(const SSL *ssl);

void setClientCertLevel(SSL *ssl, uint8_t certLevel);
void setTLSValidProtocols(SSL *ssl, unsigned long proto_mask, unsigned long max_mask);

namespace ssl
{
namespace detail
{
  struct SCOPED_X509_TRAITS {
    typedef X509 *value_type;
    static value_type
    initValue()
    {
      return nullptr;
    }
    static bool
    isValid(value_type x)
    {
      return x != nullptr;
    }
    static void
    destroy(value_type x)
    {
      X509_free(x);
    }
  };

  struct SCOPED_BIO_TRAITS {
    typedef BIO *value_type;
    static value_type
    initValue()
    {
      return nullptr;
    }
    static bool
    isValid(value_type x)
    {
      return x != nullptr;
    }
    static void
    destroy(value_type x)
    {
      BIO_free(x);
    }
  };
  /* namespace ssl */ // namespace detail
} /* namespace detail */
} // namespace ssl

struct ats_wildcard_matcher {
  ats_wildcard_matcher()
  {
    if (!regex.compile("^\\*\\.[^\\*.]+")) {
      Fatal("failed to compile TLS wildcard matching regex");
    }
  }

  ~ats_wildcard_matcher() {}
  bool
  match(const char *hostname) const
  {
    return regex.match(hostname) != -1;
  }

private:
  DFA regex;
};

typedef ats_scoped_resource<ssl::detail::SCOPED_X509_TRAITS> scoped_X509;
typedef ats_scoped_resource<ssl::detail::SCOPED_BIO_TRAITS> scoped_BIO;
