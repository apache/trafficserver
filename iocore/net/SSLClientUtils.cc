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

#include "ts/ink_config.h"
#include "records/I_RecHttp.h"
#include "ts/ink_platform.h"
#include "ts/X509HostnameValidator.h"
#include "P_Net.h"
#include "P_SSLClientUtils.h"

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
  if (!preverify_ok) {
    // Don't bother to check the hostname if we failed openssl's verification
    SSLDebug("verify error:num=%d:%s:depth=%d", err, X509_verify_cert_error_string(err), depth);
    if (netvc && netvc->options.clientVerificationFlag == 2) {
      if (netvc->options.sni_servername)
        Warning("Hostname verification failed for (%s) but still continuing with the connection establishment",
                netvc->options.sni_servername.get());
      else {
        char buff[INET6_ADDRSTRLEN];
        ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
        Warning("Server certificate verification failed for %s but still continuing with the connection establishment", buff);
      }
      return 1;
    }
    return preverify_ok;
  }
  if (depth != 0) {
    // Not server cert....
    return preverify_ok;
  }

  if (netvc != nullptr) {
    netvc->callHooks(TS_EVENT_SSL_SERVER_VERIFY_HOOK);
    // Match SNI if present
    if (netvc->options.sni_servername) {
      char *matched_name = nullptr;
      if (validate_hostname(cert, reinterpret_cast<unsigned char *>(netvc->options.sni_servername.get()), false, &matched_name)) {
        SSLDebug("Hostname %s verified OK, matched %s", netvc->options.sni_servername.get(), matched_name);
        ats_free(matched_name);
        return preverify_ok;
      }
      Warning("Hostname verification failed for (%s)", netvc->options.sni_servername.get());
    }
    // Otherwise match by IP
    else {
      char buff[INET6_ADDRSTRLEN];
      ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
      if (validate_hostname(cert, reinterpret_cast<unsigned char *>(buff), true, nullptr)) {
        SSLDebug("IP %s verified OK", buff);
        return preverify_ok;
      }
      Warning("IP verification failed for (%s)", buff);
    }

    if (netvc->options.clientVerificationFlag == 2) {
      char buff[INET6_ADDRSTRLEN];
      ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
      Warning("Server certificate verification failed but continuing with the connection establishment:%s:%s",
              netvc->options.sni_servername.get(), buff);
      return preverify_ok;
    }
    return 0;
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

    if (params->clientCACertFilename != nullptr || params->clientCACertPath != nullptr) {
      if (!SSL_CTX_load_verify_locations(client_ctx, params->clientCACertFilename, params->clientCACertPath)) {
        SSLError("invalid client CA Certificate file (%s) or CA Certificate path (%s)", params->clientCACertFilename,
                 params->clientCACertPath);
        goto fail;
      }
    }

    if (!SSL_CTX_set_default_verify_paths(client_ctx)) {
      SSLError("failed to set the default verify paths");
      goto fail;
    }
  }

  if (SSLConfigParams::init_ssl_ctx_cb) {
    SSLConfigParams::init_ssl_ctx_cb(client_ctx, false);
  }

  return client_ctx;

fail:
  SSLReleaseContext(client_ctx);
  ::exit(1);
}
