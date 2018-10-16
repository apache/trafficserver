/** @file

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

#include "tscore/ink_config.h"
#include "records/I_RecHttp.h"
#include "tscore/ink_platform.h"
#include "tscore/X509HostnameValidator.h"
#include "P_Net.h"
#include "P_SSLClientUtils.h"
#include "YamlSNIConfig.h"

#include <openssl/err.h>
#include <openssl/pem.h>

#if (OPENSSL_VERSION_NUMBER >= 0x10000000L) // openssl returns a const SSL_METHOD
using ink_ssl_method_t = const SSL_METHOD *;
#else
typedef SSL_METHOD *ink_ssl_method_t;
#endif

int
verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
  X509 *cert;
  int depth;
  int err;
  SSL *ssl;

  SSLDebug("Entered verify cb");
  depth = X509_STORE_CTX_get_error_depth(ctx);
  cert  = X509_STORE_CTX_get_current_cert(ctx);
  err   = X509_STORE_CTX_get_error(ctx);

  /*
   * Retrieve the pointer to the SSL of the connection currently treated
   * and the application specific data stored into the SSL object.
   */
  ssl                      = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  bool enforce_mode        = (netvc && netvc->options.clientVerificationFlag == static_cast<uint8_t>(YamlSNIConfig::Level::STRICT));
  if (!preverify_ok && netvc != nullptr) {
    // Don't bother to check the hostname if we failed openssl's verification
    SSLDebug("verify error:num=%d:%s:depth=%d", err, X509_verify_cert_error_string(err), depth);
    const char *sni_name;
    char buff[INET6_ADDRSTRLEN];
    ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
    if (netvc->options.sni_servername) {
      sni_name = netvc->options.sni_servername.get();
    } else {
      sni_name = buff;
    }
    Warning("Core server certificate verification failed for (%s). Action=%s Error=%s server=%s(%s) depth=%d", sni_name,
            enforce_mode ? "Terminate" : "Continue", X509_verify_cert_error_string(err), netvc->options.ssl_servername.get(), buff,
            depth);
    // If not enforcing ignore the error, just log warning
    return enforce_mode ? preverify_ok : 1;
  }
  if (depth != 0) {
    // Not server cert....
    return preverify_ok;
  }

  if (netvc != nullptr) {
    netvc->callHooks(TS_EVENT_SSL_SERVER_VERIFY_HOOK);
    char *matched_name = nullptr;
    unsigned char *sni_name;
    char buff[INET6_ADDRSTRLEN];
    if (netvc->options.sni_servername) {
      sni_name = reinterpret_cast<unsigned char *>(netvc->options.sni_servername.get());
    } else {
      sni_name = reinterpret_cast<unsigned char *>(buff);
      ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
    }
    if (validate_hostname(cert, sni_name, false, &matched_name)) {
      SSLDebug("Hostname %s verified OK, matched %s", netvc->options.sni_servername.get(), matched_name);
      ats_free(matched_name);
      return preverify_ok;
    }
    // Get the server address if we did't already compute it
    if (netvc->options.sni_servername) {
      ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
    }
    // If we got here the verification failed
    Warning("SNI (%s) not in certificate. Action=%s server=%s(%s)", sni_name, enforce_mode ? "Terminate" : "Continue",
            netvc->options.ssl_servername.get(), buff);
    return enforce_mode ? 0 : preverify_ok;
  }
  return preverify_ok;
}

SSL_CTX *
SSLInitClientContext(const SSLConfigParams *params)
{
  ink_ssl_method_t meth = nullptr;
  SSL_CTX *client_ctx   = nullptr;
  char *clientKeyPtr    = nullptr;

  // Note that we do not call RAND_seed() explicitly here, we depend on OpenSSL
  // to do the seeding of the PRNG for us. This is the case for all platforms that
  // has /dev/urandom for example.

  meth       = SSLv23_client_method();
  client_ctx = SSL_CTX_new(meth);

  if (!client_ctx) {
    SSLError("cannot create new client context");
    ::exit(1);
  }

  SSL_CTX_set_options(client_ctx, params->ssl_client_ctx_options);
  if (params->client_cipherSuite != nullptr) {
    if (!SSL_CTX_set_cipher_list(client_ctx, params->client_cipherSuite)) {
      SSLError("invalid client cipher suite in records.config");
      goto fail;
    }
  }

#if TS_USE_TLS_SET_CIPHERSUITES
  if (params->client_tls13_cipher_suites != nullptr) {
    if (!SSL_CTX_set_ciphersuites(client_ctx, params->client_tls13_cipher_suites)) {
      SSLError("invalid tls client cipher suites in records.config");
      goto fail;
    }
  }
#endif

#ifdef SSL_CTX_set1_groups_list
  if (params->client_groups_list != nullptr) {
    if (!SSL_CTX_set1_groups_list(client_ctx, params->client_groups_list)) {
      SSLError("invalid groups list for client in records.config");
      goto fail;
    }
  }
#endif

  // if no path is given for the client private key,
  // assume it is contained in the client certificate file.
  clientKeyPtr = params->clientKeyPath;
  if (clientKeyPtr == nullptr) {
    clientKeyPtr = params->clientCertPath;
  }

  if (params->clientCertPath != nullptr && params->clientCertPath[0] != '\0') {
    if (!SSL_CTX_use_certificate_chain_file(client_ctx, params->clientCertPath)) {
      SSLError("failed to load client certificate from %s", params->clientCertPath);
      goto fail;
    }

    if (!SSL_CTX_use_PrivateKey_file(client_ctx, clientKeyPtr, SSL_FILETYPE_PEM)) {
      SSLError("failed to load client private key file from %s", clientKeyPtr);
      goto fail;
    }

    if (!SSL_CTX_check_private_key(client_ctx)) {
      SSLError("client private key (%s) does not match the certificate public key (%s)", clientKeyPtr, params->clientCertPath);
      goto fail;
    }
  }

  if (params->clientVerify) {
    SSL_CTX_set_verify(client_ctx, SSL_VERIFY_PEER, verify_callback);
    SSL_CTX_set_verify_depth(client_ctx, params->client_verify_depth);
  }

  if (params->clientCACertFilename != nullptr || params->clientCACertPath != nullptr) {
    if (!SSL_CTX_load_verify_locations(client_ctx, params->clientCACertFilename, params->clientCACertPath)) {
      SSLError("invalid client CA Certificate file (%s) or CA Certificate path (%s)", params->clientCACertFilename,
               params->clientCACertPath);
      goto fail;
    }
  } else if (!SSL_CTX_set_default_verify_paths(client_ctx)) {
    SSLError("failed to set the default verify paths");
    goto fail;
  }

  if (SSLConfigParams::init_ssl_ctx_cb) {
    SSLConfigParams::init_ssl_ctx_cb(client_ctx, false);
  }

  return client_ctx;

fail:
  SSLReleaseContext(client_ctx);
  ::exit(1);
}
