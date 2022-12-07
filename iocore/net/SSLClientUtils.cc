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
#include "tscore/Filenames.h"
#include "tscore/X509HostnameValidator.h"

#include "P_Net.h"
#include "P_SSLClientUtils.h"
#include "P_SSLConfig.h"
#include "P_SSLNetVConnection.h"
#include "P_TLSKeyLogger.h"
#include "YamlSNIConfig.h"
#include "SSLDiags.h"
#include "SSLSessionCache.h"

#include <openssl/err.h>
#include <openssl/pem.h>

SSLOriginSessionCache *origin_sess_cache;

int
verify_callback(int signature_ok, X509_STORE_CTX *ctx)
{
  X509 *cert;
  int depth;
  int err;
  SSL *ssl;

  Debug("ssl_verify", "Entered cert verify callback");

  /*
   * Retrieve the pointer to the SSL of the connection currently treated
   * and the application specific data stored into the SSL object.
   */
  ssl                      = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);

  // No enforcing, go away
  if (netvc == nullptr) {
    // No netvc, very bad.  Go away.  Things are not good.
    Debug("ssl_verify", "WARNING, NetVC is NULL in cert verify callback");
    return false;
  } else if (netvc->options.verifyServerPolicy == YamlSNIConfig::Policy::DISABLED) {
    return true; // Tell them that all is well
  }

  depth = X509_STORE_CTX_get_error_depth(ctx);
  cert  = X509_STORE_CTX_get_current_cert(ctx);
  err   = X509_STORE_CTX_get_error(ctx);

  bool enforce_mode = (netvc->options.verifyServerPolicy == YamlSNIConfig::Policy::ENFORCED);
  bool check_sig =
    static_cast<uint8_t>(netvc->options.verifyServerProperties) & static_cast<uint8_t>(YamlSNIConfig::Property::SIGNATURE_MASK);

  if (check_sig) {
    if (!signature_ok) {
      Debug("ssl_verify", "verification error:num=%d:%s:depth=%d", err, X509_verify_cert_error_string(err), depth);
      const char *sni_name;
      char buff[INET6_ADDRSTRLEN];
      ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
      if (netvc->options.sni_servername) {
        sni_name = netvc->options.sni_servername.get();
      } else {
        sni_name = buff;
      }
      Warning("Core server certificate verification failed for (%s). Action=%s Error=%s server=%s(%s) depth=%d", sni_name,
              enforce_mode ? "Terminate" : "Continue", X509_verify_cert_error_string(err), netvc->options.ssl_servername.get(),
              buff, depth);
      // If not enforcing ignore the error, just log warning
      return enforce_mode ? signature_ok : 1;
    }
  }
  // Don't check names and other things unless this is the terminal cert
  if (depth != 0) {
    // Not server cert....
    return signature_ok;
  }

  bool check_name =
    static_cast<uint8_t>(netvc->options.verifyServerProperties) & static_cast<uint8_t>(YamlSNIConfig::Property::NAME_MASK);
  if (check_name) {
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
      Debug("ssl_verify", "Hostname %s verified OK, matched %s", sni_name, matched_name);
      ats_free(matched_name);
    } else { // Name validation failed
      // Get the server address if we did't already compute it
      if (netvc->options.sni_servername) {
        ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
      }
      // If we got here the verification failed
      Warning("SNI (%s) not in certificate. Action=%s server=%s(%s)", sni_name, enforce_mode ? "Terminate" : "Continue",
              netvc->options.ssl_servername.get(), buff);
      return !enforce_mode;
    }
  }
  // If the previous configured checks passed, give the hook a try
  netvc->set_verify_cert(ctx);
  netvc->callHooks(TS_EVENT_SSL_VERIFY_SERVER);
  netvc->set_verify_cert(nullptr);
  if (netvc->getSSLHandShakeComplete()) { // hook moved the handshake state to terminal
    unsigned char *sni_name;
    char buff[INET6_ADDRSTRLEN];
    if (netvc->options.sni_servername) {
      sni_name = reinterpret_cast<unsigned char *>(netvc->options.sni_servername.get());
    } else {
      sni_name = reinterpret_cast<unsigned char *>(buff);
      ats_ip_ntop(netvc->get_remote_addr(), buff, INET6_ADDRSTRLEN);
    }
    Warning("TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin certificate check for %s.  Action=%s SNI=%s",
            netvc->options.ssl_servername.get(), enforce_mode ? "Terminate" : "Continue", sni_name);
    return !enforce_mode;
  }
  // Made it this far.  All is good
  return true;
}

static int
ssl_client_cert_callback(SSL *ssl, void * /*arg*/)
{
  SSLNetVConnection *netvc = SSLNetVCAccess(ssl);
  SSL_CTX *ctx             = SSL_get_SSL_CTX(ssl);
  if (ctx) {
    // Do not need to free either the cert or the ssl_ctx
    // both are internal pointers
    X509 *cert = SSL_CTX_get0_certificate(ctx);
    netvc->set_sent_cert(cert != nullptr ? 2 : 1);
    Debug("ssl_verify", "sent cert: %d", cert != nullptr ? 2 : 1);
  }
  return 1;
}

static int
ssl_new_session_callback(SSL *ssl, SSL_SESSION *sess)
{
  std::string sni_addr = get_sni_addr(ssl);
  if (!sni_addr.empty()) {
    std::string lookup_key;
    ts::bwprint(lookup_key, "{}:{}:{}", sni_addr.c_str(), SSL_get_SSL_CTX(ssl), get_verify_str(ssl));
    origin_sess_cache->insert_session(lookup_key, sess, ssl);
  } else {
    if (is_debug_tag_set("ssl.origin_session_cache")) {
      Debug("ssl.origin_session_cache", "Failed to fetch SNI/IP.");
    }
  }

  // return 0 here since we're converting the sessions using i2d_SSL_SESSION,
  // meaning if we return 1, openssl will keep an extra refcount on the session.
  return 0;
}

SSL_CTX *
SSLInitClientContext(const SSLConfigParams *params)
{
  const SSL_METHOD *meth = nullptr;
  SSL_CTX *client_ctx    = nullptr;

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
      SSLError("invalid client cipher suite in %s", ts::filename::RECORDS);
      goto fail;
    }
  }

#if TS_USE_TLS_SET_CIPHERSUITES
  if (params->client_tls13_cipher_suites != nullptr) {
    if (!SSL_CTX_set_ciphersuites(client_ctx, params->client_tls13_cipher_suites)) {
      SSLError("invalid tls client cipher suites in %s", ts::filename::RECORDS);
      goto fail;
    }
  }
#endif

#if defined(SSL_CTX_set1_groups_list) || defined(SSL_CTX_set1_curves_list)
  if (params->client_groups_list != nullptr) {
#ifdef SSL_CTX_set1_groups_list
    if (!SSL_CTX_set1_groups_list(client_ctx, params->client_groups_list)) {
#else
    if (!SSL_CTX_set1_curves_list(client_ctx, params->client_groups_list)) {
#endif
      SSLError("invalid groups list for client in %s", ts::filename::RECORDS);
      goto fail;
    }
  }
#endif

  SSL_CTX_set_verify_depth(client_ctx, params->client_verify_depth);
  if (SSLConfigParams::init_ssl_ctx_cb) {
    SSLConfigParams::init_ssl_ctx_cb(client_ctx, false);
  }

  SSL_CTX_set_cert_cb(client_ctx, ssl_client_cert_callback, nullptr);

  if (params->ssl_origin_session_cache == 1) {
    SSL_CTX_set_session_cache_mode(client_ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_AUTO_CLEAR | SSL_SESS_CACHE_NO_INTERNAL);
    SSL_CTX_sess_set_new_cb(client_ctx, ssl_new_session_callback);
  }

#if TS_HAS_TLS_KEYLOGGING
  if (unlikely(TLSKeyLogger::is_enabled())) {
    SSL_CTX_set_keylog_callback(client_ctx, TLSKeyLogger::ssl_keylog_cb);
  }
#endif

  return client_ctx;

fail:
  SSLReleaseContext(client_ctx);
  ::exit(1);
}

SSL_CTX *
SSLCreateClientContext(const struct SSLConfigParams *params, const char *ca_bundle_path, const char *ca_bundle_file,
                       const char *cert_path, const char *key_path)
{
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(nullptr, &SSL_CTX_free);

  if (nullptr == params || nullptr == cert_path) {
    return nullptr;
  }

  ctx.reset(SSLInitClientContext(params));

  if (!ctx) {
    return nullptr;
  }

  if (!SSL_CTX_use_certificate_chain_file(ctx.get(), cert_path)) {
    SSLError("SSLCreateClientContext(): failed to load client certificate: %s",
             (!cert_path || cert_path[0] == '\0') ? "[empty file name]" : cert_path);
    return nullptr;
  }

  if (!key_path || key_path[0] == '\0') {
    key_path = cert_path;
  }

  if (!SSL_CTX_use_PrivateKey_file(ctx.get(), key_path, SSL_FILETYPE_PEM)) {
    SSLError("SSLCreateClientContext(): failed to load client private key: %s",
             (!key_path || key_path[0] == '\0') ? "[empty file]" : key_path);
    return nullptr;
  }

  if (!SSL_CTX_check_private_key(ctx.get())) {
    SSLError("SSLCreateClientContext(): client private key: %s does not match client certificate: %s",
             (!key_path || key_path[0] == '\0') ? "[empty file]" : key_path,
             (!cert_path || cert_path[0] == '\0') ? "[empty file]" : cert_path);
    return nullptr;
  }

  if (ca_bundle_file || ca_bundle_path) {
    if (!SSL_CTX_load_verify_locations(ctx.get(), ca_bundle_file, ca_bundle_path)) {
      SSLError("SSLCreateClientContext(): Invalid CA Certificate file: %s or CA Certificate path: %s",
               (!ca_bundle_file || ca_bundle_file[0] == '\0') ? "[empty file name]" : ca_bundle_file,
               (!ca_bundle_path || ca_bundle_path[0] == '\0') ? "[empty path]" : ca_bundle_path);
      SSLError("SSLCreateClientContext(): Invalid client CA cert file/CA path.");
      return nullptr;
    }
  } else if (!SSL_CTX_set_default_verify_paths(ctx.get())) {
    SSLError("SSLCreateClientContext(): failed to set the default verify paths.");
    return nullptr;
  }
  return ctx.release();
}
