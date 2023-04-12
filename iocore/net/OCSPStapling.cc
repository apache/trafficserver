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

#include "P_OCSPStapling.h"

#if TS_USE_TLS_OCSP

#include <openssl/bio.h>
#include <openssl/ssl.h>

#if TS_HAS_BORINGOCSP
#include <boringocsp/ocsp.h>
#else
#include <openssl/ocsp.h>
#endif

#include "tscore/ink_memory.h"
#include "P_Net.h"
#include "P_SSLConfig.h"
#include "P_SSLUtils.h"
#include "SSLStats.h"
#include "FetchSM.h"

// Maximum OCSP stapling response size.
// This should be the response for a single certificate and will typically include the responder certificate chain,
// so 10K should be more than enough.
#define MAX_STAPLING_DER 10240

// Cached info stored in SSL_CTX ex_info
struct certinfo {
  unsigned char idx[20]; // Index in session cache SHA1 hash of certificate
  OCSP_CERTID *cid;      // Certificate ID for OCSP requests or nullptr if ID cannot be determined
  char *uri;             // Responder details
  char *certname;
  char *user_agent;
  ink_mutex stapling_mutex;
  unsigned char resp_der[MAX_STAPLING_DER];
  unsigned int resp_derlen;
  bool is_prefetched;
  bool is_expire;
  time_t expire_time;
};

extern ClassAllocator<FetchSM> FetchSMAllocator;

class HTTPRequest : public Continuation
{
public:
  static constexpr int MAX_RESP_LEN = 100 * 1024;

  HTTPRequest()
  {
    mutex = new_ProxyMutex();
    SET_HANDLER(&HTTPRequest::event_handler);
  }

  ~HTTPRequest()
  {
    this->_fsm->ext_destroy();
    OPENSSL_free(this->_req_body);
  }

  int
  event_handler(int event, Event *e)
  {
    if (event == TS_EVENT_IMMEDIATE) {
      this->fetch();
    } else {
      auto fsm = reinterpret_cast<FetchSM *>(e);
      auto ctx = reinterpret_cast<HTTPRequest *>(fsm->ext_get_user_data());
      if (event == TS_FETCH_EVENT_EXT_BODY_DONE) {
        ctx->set_done();
      } else if (event == TS_EVENT_ERROR) {
        ctx->set_error();
      }
    }

    return 0;
  }

  void
  set_request_line(bool use_post, const char *uri)
  {
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port        = 65535;

    this->_fsm = FetchSMAllocator.alloc();
    this->_fsm->ext_set_user_data(this);
    if (use_post) {
      this->_fsm->ext_init(this, "POST", uri, "HTTP/1.1", reinterpret_cast<sockaddr *>(&sin), 0);
    } else {
      this->_fsm->ext_init(this, "GET", uri, "HTTP/1.1", reinterpret_cast<sockaddr *>(&sin), 0);
    }
  }

  int
  set_body(const char *content_type, const ASN1_ITEM *it, const ASN1_VALUE *req)
  {
    this->_req_body = nullptr;

    if (req != nullptr) {
      // const_cast is needed for OpenSSL 1.1.1
      this->_req_body_len = ASN1_item_i2d(const_cast<ASN1_VALUE *>(req), &this->_req_body, it);
      if (this->_req_body_len == -1) {
        return 0;
      }
    }
    this->add_header("Content-Type", content_type);
    char req_body_len_str[10];
    int req_body_len_str_len;
    req_body_len_str_len = ink_fast_itoa(this->_req_body_len, req_body_len_str, sizeof(req_body_len_str));
    this->add_header("Content-Length", 14, req_body_len_str, req_body_len_str_len);

    return 1;
  }

  void
  add_header(const char *name, int name_len, const char *value, int value_len)
  {
    this->_fsm->ext_add_header(name, name_len, value, value_len);
  }

  void
  add_header(const char *name, const char *value)
  {
    this->add_header(name, strlen(name), value, strlen(value));
  }

  void
  fetch()
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    this->_fsm->ext_launch();
    this->_fsm->ext_write_data(this->_req_body, this->_req_body_len);
  }

  void
  set_done()
  {
    this->_result = 1;
  }

  void
  set_error()
  {
    this->_result = -1;
  }

  bool
  is_done()
  {
    return this->_result != 0;
  }

  bool
  is_success()
  {
    return this->_result == 1;
  }

  unsigned char *
  get_response_body(int *len)
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    char *buf = static_cast<char *>(malloc(MAX_RESP_LEN));
    *len      = this->_fsm->ext_read_data(buf, MAX_RESP_LEN);
    return reinterpret_cast<unsigned char *>(buf);
  }

private:
  FetchSM *_fsm            = nullptr;
  unsigned char *_req_body = nullptr;
  int _req_body_len        = 0;
  int _result              = 0;
};

/*
 * In the case of multiple certificates associated with a SSL_CTX, we must store a map
 * of cached responses
 */
using certinfo_map = std::map<X509 *, certinfo *>;

void
certinfo_map_free(void * /*parent*/, void *ptr, CRYPTO_EX_DATA * /*ad*/, int /*idx*/, long /*argl*/, void * /*argp*/)
{
  certinfo_map *map = static_cast<certinfo_map *>(ptr);

  if (!map) {
    return;
  }

  for (certinfo_map::iterator iter = map->begin(); iter != map->end(); ++iter) {
    certinfo *cinf = iter->second;
    if (cinf->uri) {
      OPENSSL_free(cinf->uri);
    }

    ats_free(cinf->certname);
    ats_free(cinf->user_agent);

    ink_mutex_destroy(&cinf->stapling_mutex);
    OPENSSL_free(cinf);
  }
  delete map;
}

static int ssl_stapling_index = -1;

void
ssl_stapling_ex_init()
{
  if (ssl_stapling_index != -1) {
    return;
  }
  ssl_stapling_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, certinfo_map_free);
}

static X509 *
stapling_get_issuer(SSL_CTX *ssl_ctx, X509 *x)
{
  X509 *issuer                = nullptr;
  X509_STORE *st              = SSL_CTX_get_cert_store(ssl_ctx);
  STACK_OF(X509) *extra_certs = nullptr;
  X509_STORE_CTX *inctx       = X509_STORE_CTX_new();

  if (inctx == nullptr) {
    return nullptr;
  }

#ifdef SSL_CTX_select_current_cert
  if (!SSL_CTX_select_current_cert(ssl_ctx, x)) {
    Warning("OCSP: could not select current certificate chain %p", x);
  }
#endif

  if (X509_STORE_CTX_init(inctx, st, nullptr, nullptr) == 0) {
    goto end;
  }

#ifdef SSL_CTX_get_extra_chain_certs
  SSL_CTX_get_extra_chain_certs(ssl_ctx, &extra_certs);
#else
  extra_certs = ssl_ctx->extra_certs;
#endif

  if (sk_X509_num(extra_certs) == 0) {
    goto end;
  }

  for (int i = 0; i < static_cast<int>(sk_X509_num(extra_certs)); i++) {
    issuer = sk_X509_value(extra_certs, i);
    if (X509_check_issued(issuer, x) == X509_V_OK) {
#if OPENSSL_VERSION_NUMBER < 0x10100000
      CRYPTO_add(&issuer->references, 1, CRYPTO_LOCK_X509);
      return issuer;
#else
      X509_up_ref(issuer);
      goto end;
#endif
    }
  }

  if (X509_STORE_CTX_get1_issuer(&issuer, inctx, x) <= 0) {
    issuer = nullptr;
  }

end:
  X509_STORE_CTX_free(inctx);

  return issuer;
}

static bool
stapling_cache_response(OCSP_RESPONSE *rsp, certinfo *cinf)
{
  unsigned char resp_der[MAX_STAPLING_DER];
  unsigned char *p;
  unsigned int resp_derlen;

  p           = resp_der;
  resp_derlen = i2d_OCSP_RESPONSE(rsp, &p);

  if (resp_derlen == 0) {
    Error("stapling_cache_response: cannot decode OCSP response for %s", cinf->certname);
    return false;
  }

  if (resp_derlen > MAX_STAPLING_DER) {
    Error("stapling_cache_response: OCSP response too big (%u bytes) for %s", resp_derlen, cinf->certname);
    return false;
  }

  ink_mutex_acquire(&cinf->stapling_mutex);
  memcpy(cinf->resp_der, resp_der, resp_derlen);
  cinf->resp_derlen = resp_derlen;
  cinf->is_expire   = false;
  cinf->expire_time = time(nullptr) + SSLConfigParams::ssl_ocsp_cache_timeout;
  ink_mutex_release(&cinf->stapling_mutex);

  Debug("ssl_ocsp", "stapling_cache_response: success to cache response");
  return true;
}

bool
ssl_stapling_init_cert(SSL_CTX *ctx, X509 *cert, const char *certname, const char *rsp_file)
{
  scoped_X509 issuer;
  STACK_OF(OPENSSL_STRING) *aia = nullptr;
  BIO *rsp_bio                  = nullptr;
  OCSP_RESPONSE *rsp            = nullptr;

  if (!cert) {
    Error("null cert passed in for %s", certname);
    return false;
  }

  certinfo_map *map = static_cast<certinfo_map *>(SSL_CTX_get_ex_data(ctx, ssl_stapling_index));
  if (map && map->find(cert) != map->end()) {
    Note("certificate already initialized for %s", certname);
    return false;
  }

  if (!map) {
    map = new certinfo_map;
  }
  certinfo *cinf = static_cast<certinfo *>(OPENSSL_malloc(sizeof(certinfo)));
  if (!cinf) {
    Error("error allocating memory for %s", certname);
    delete map;
    return false;
  }

  // Initialize certinfo
  cinf->cid      = nullptr;
  cinf->uri      = nullptr;
  cinf->certname = ats_strdup(certname);
  if (SSLConfigParams::ssl_ocsp_user_agent != nullptr) {
    cinf->user_agent = ats_strdup(SSLConfigParams::ssl_ocsp_user_agent);
  }
  cinf->resp_derlen = 0;
  ink_mutex_init(&cinf->stapling_mutex);
  cinf->is_prefetched = rsp_file ? true : false;
  cinf->is_expire     = true;
  cinf->expire_time   = 0;

  if (cinf->is_prefetched) {
#ifndef OPENSSL_IS_BORINGSSL
    Debug("ssl_ocsp", "using OCSP prefetched response file %s", rsp_file);
    rsp_bio = BIO_new_file(rsp_file, "r");
    if (rsp_bio) {
      rsp = d2i_OCSP_RESPONSE_bio(rsp_bio, nullptr);
    }

    if (!rsp_bio || !rsp) {
      Note("cannot get prefetched response for %s from %s", certname, rsp_file);
      goto err;
    }

    if (!stapling_cache_response(rsp, cinf)) {
      Error("stapling_refresh_response: can not cache response");
      goto err;
    } else {
      Debug("ssl_ocsp", "stapling_refresh_response: successful refresh OCSP response");
      OCSP_RESPONSE_free(rsp);
      rsp = nullptr;
      BIO_free(rsp_bio);
      rsp_bio = nullptr;
    }
#else
    Warning("failed to set prefetched OCSP response; this functionality not supported by BoringSSL");
#endif
  }

  issuer.reset(stapling_get_issuer(ctx, cert));
  if (issuer.get() == nullptr) {
    Note("cannot get issuer certificate from %s", certname);
    goto err;
  }

  cinf->cid = OCSP_cert_to_id(nullptr, cert, issuer.get());
  if (!cinf->cid) {
    goto err;
  }

  X509_digest(cert, EVP_sha1(), cinf->idx, nullptr);

  aia = X509_get1_ocsp(cert);
  if (aia) {
    cinf->uri = sk_OPENSSL_STRING_pop(aia);
    X509_email_free(aia);
  }

  if (!cinf->uri) {
    Note("no OCSP responder URI for %s", certname);
    goto err;
  }

#ifdef OPENSSL_IS_BORINGSSL
  X509_up_ref(cert);
#endif

  map->insert(std::make_pair(cert, cinf));
  SSL_CTX_set_ex_data(ctx, ssl_stapling_index, map);

  Note("successfully initialized stapling for %s into SSL_CTX: %p uri=%s", certname, ctx, cinf->uri);
  return true;

err:
  if (cinf->cid) {
    OCSP_CERTID_free(cinf->cid);
  }

  ats_free(cinf->certname);
  ats_free(cinf->user_agent);

  if (cinf) {
    OPENSSL_free(cinf);
  }
  if (map) {
    delete map;
  }

  if (rsp) {
    OCSP_RESPONSE_free(rsp);
  }
  if (rsp_bio) {
    BIO_free(rsp_bio);
  }

  return false;
}

static certinfo_map *
stapling_get_cert_info(SSL_CTX *ctx)
{
  certinfo_map *map;

  // Only return the map if it contains at least one element with a valid entry
  map = static_cast<certinfo_map *>(SSL_CTX_get_ex_data(ctx, ssl_stapling_index));
  if (map && !map->empty() && map->begin()->second && map->begin()->second->cid) {
    return map;
  }

  return nullptr;
}

static int
stapling_check_response(certinfo *cinf, OCSP_RESPONSE *rsp)
{
  int status, reason;
  OCSP_BASICRESP *bs = nullptr;
  ASN1_GENERALIZEDTIME *rev, *thisupd, *nextupd;
  int response_status = OCSP_response_status(rsp);

  // Check to see if response is an error.
  // If so we automatically accept it because it would have expired from the cache if it was time to retry.
  if (response_status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
    return SSL_TLSEXT_ERR_NOACK;
  }

  bs = OCSP_response_get1_basic(rsp);
  if (bs == nullptr) {
    // If we can't parse response just pass it back to client
    Error("stapling_check_response: cannot parse response for %s", cinf->certname);
    return SSL_TLSEXT_ERR_OK;
  }
  if (!OCSP_resp_find_status(bs, cinf->cid, &status, &reason, &rev, &thisupd, &nextupd)) {
    // If ID not present just pass it back to client
    Error("stapling_check_response: certificate ID not present in response for %s", cinf->certname);
  } else {
    OCSP_check_validity(thisupd, nextupd, 300, -1);
  }

  switch (status) {
  case V_OCSP_CERTSTATUS_GOOD:
    break;
  case V_OCSP_CERTSTATUS_REVOKED:
    SSL_INCREMENT_DYN_STAT(ssl_ocsp_revoked_cert_stat);
    break;
  case V_OCSP_CERTSTATUS_UNKNOWN:
    SSL_INCREMENT_DYN_STAT(ssl_ocsp_unknown_cert_stat);
    break;
  default:
    break;
  }

  OCSP_BASICRESP_free(bs);

  return SSL_TLSEXT_ERR_OK;
}

static OCSP_RESPONSE *
query_responder(const char *uri, const char *user_agent, OCSP_REQUEST *req, int req_timeout)
{
  ink_hrtime start, end;
  OCSP_RESPONSE *resp = nullptr;

  start = Thread::get_hrtime();
  end   = ink_hrtime_add(start, ink_hrtime_from_sec(req_timeout));

  HTTPRequest httpreq;
  bool use_post = true;

  httpreq.set_request_line(use_post, uri);

  // Host header
  const char *host  = strstr(uri, "://") + 3;
  const char *slash = strchr(host, '/');
  if (slash == nullptr) {
    slash = host + strlen(host);
  }
  int host_len = slash - host;
  httpreq.add_header("Host", 4, host, host_len);

  // User-Agent header
  if (user_agent != nullptr) {
    httpreq.add_header("User-Agent", user_agent);
  }

  // Content-Type, Content-Length, Request Body
  if (use_post) {
    if (httpreq.set_body("application/ocsp-request", ASN1_ITEM_rptr(OCSP_REQUEST), (const ASN1_VALUE *)req) != 1) {
      Error("failed to make a request for OCSP server; uri=%s", uri);
      return nullptr;
    }
  }

  // Send request
  eventProcessor.schedule_imm(&httpreq, ET_NET);

  // Wait until the request completes
  do {
    ink_hrtime_sleep(HRTIME_MSECONDS(1));
  } while (!httpreq.is_done() && (Thread::get_hrtime() < end));

  if (httpreq.is_success()) {
    // Parse the response
    int len;
    const unsigned char *p = httpreq.get_response_body(&len);
    resp                   = reinterpret_cast<OCSP_RESPONSE *>(ASN1_item_d2i(nullptr, &p, len, ASN1_ITEM_rptr(OCSP_RESPONSE)));

    if (resp) {
      return resp;
    }
  }

  Error("failed to get a response from OCSP server; uri=%s", uri);
  return nullptr;
}

static bool
stapling_refresh_response(certinfo *cinf, OCSP_RESPONSE **prsp)
{
  bool rv             = true;
  OCSP_REQUEST *req   = nullptr;
  OCSP_CERTID *id     = nullptr;
  int response_status = 0;

  *prsp = nullptr;

  Debug("ssl_ocsp", "stapling_refresh_response: querying responder; uri=%s", cinf->uri);

  req = OCSP_REQUEST_new();
  if (!req) {
    goto err;
  }
  id = OCSP_CERTID_dup(cinf->cid);
  if (!id) {
    goto err;
  }
  if (!OCSP_request_add0_id(req, id)) {
    goto err;
  }

  *prsp = query_responder(cinf->uri, cinf->user_agent, req, SSLConfigParams::ssl_ocsp_request_timeout);
  if (*prsp == nullptr) {
    goto done;
  }

  response_status = OCSP_response_status(*prsp);
  if (response_status == OCSP_RESPONSE_STATUS_SUCCESSFUL) {
    Debug("ssl_ocsp", "stapling_refresh_response: query response received");
    stapling_check_response(cinf, *prsp);
  } else {
    Error("stapling_refresh_response: responder response error; uri=%s response_status=%d", cinf->uri, response_status);
  }

  if (!stapling_cache_response(*prsp, cinf)) {
    Error("stapling_refresh_response: can not cache response");
  } else {
    Debug("ssl_ocsp", "stapling_refresh_response: successfully refreshed OCSP response");
  }
  goto done;

err:
  rv = false;
  Error("stapling_refresh_response: failed to refresh OCSP response");

done:
  if (req) {
    OCSP_REQUEST_free(req);
  }
  if (*prsp) {
    OCSP_RESPONSE_free(*prsp);
  }
  return rv;
}

void
ocsp_update()
{
  shared_SSL_CTX ctx;
  OCSP_RESPONSE *resp = nullptr;
  time_t current_time;

  SSLCertificateConfig::scoped_config certLookup;
  const unsigned ctxCount = certLookup ? certLookup->count() : 0;

  Debug("ssl_ocsp", "updating OCSP data");
  for (unsigned i = 0; i < ctxCount; i++) {
    SSLCertContext *cc = certLookup->get(i);
    if (cc) {
      ctx = cc->getCtx();
      if (ctx) {
        certinfo *cinf    = nullptr;
        certinfo_map *map = stapling_get_cert_info(ctx.get());
        if (map) {
          // Walk over all certs associated with this CTX
          for (auto &iter : *map) {
            cinf = iter.second;
            ink_mutex_acquire(&cinf->stapling_mutex);
            current_time = time(nullptr);
            if (cinf->resp_derlen == 0 || cinf->is_expire || cinf->expire_time < current_time) {
              ink_mutex_release(&cinf->stapling_mutex);
              if (stapling_refresh_response(cinf, &resp)) {
                Debug("ssl_ocsp", "Successfully refreshed OCSP for %s certificate. url=%s", cinf->certname, cinf->uri);
                SSL_INCREMENT_DYN_STAT(ssl_ocsp_refreshed_cert_stat);
              } else {
                Error("Failed to refresh OCSP for %s certificate. url=%s", cinf->certname, cinf->uri);
                SSL_INCREMENT_DYN_STAT(ssl_ocsp_refresh_cert_failure_stat);
              }
            } else {
              ink_mutex_release(&cinf->stapling_mutex);
            }
          }
        }
      }
    }
  }
}

// RFC 6066 Section-8: Certificate Status Request
int
#ifndef OPENSSL_IS_BORINGSSL
ssl_callback_ocsp_stapling(SSL *ssl)
#else
ssl_callback_ocsp_stapling(SSL *ssl, void *)
#endif
{
  // Assume SSL_get_SSL_CTX() is the same as reaching into the ssl structure
  // Using the official call, to avoid leaking internal openssl knowledge
  // originally was, cinf = stapling_get_cert_info(ssl->ctx);
  certinfo_map *map = stapling_get_cert_info(SSL_get_SSL_CTX(ssl));
  if (map == nullptr) {
    Debug("ssl_ocsp", "ssl_callback_ocsp_stapling: failed to get certificate map");
    return SSL_TLSEXT_ERR_NOACK;
  }

  if (map->empty()) {
    Debug("ssl_ocsp", "ssl_callback_ocsp_stapling: certificate map empty");
    return SSL_TLSEXT_ERR_NOACK;
  }

  // Fetch the specific certificate used in this negotiation
  X509 *cert = SSL_get_certificate(ssl);
  if (!cert) {
    Error("ssl_callback_ocsp_stapling: failed to get certificate");
    return SSL_TLSEXT_ERR_NOACK;
  }

  certinfo *cinf = nullptr;
#ifndef OPENSSL_IS_BORINGSSL
  certinfo_map::iterator iter = map->find(cert);
  if (iter != map->end()) {
    cinf = iter->second;
  }
#else
  for (certinfo_map::iterator iter = map->begin(); iter != map->end(); ++iter) {
    X509 *key = iter->first;
    if (key == nullptr) {
      continue;
    }

    if (X509_cmp(key, cert) == 0) {
      cinf = iter->second;
      break;
    }
  }
#endif

  if (cinf == nullptr) {
    Error("ssl_callback_ocsp_stapling: failed to get certificate information for ssl=%p", ssl);
    return SSL_TLSEXT_ERR_NOACK;
  }

  ink_mutex_acquire(&cinf->stapling_mutex);
  time_t current_time = time(nullptr);
  if ((cinf->resp_derlen == 0 || cinf->is_expire) || (cinf->expire_time < current_time && !cinf->is_prefetched)) {
    ink_mutex_release(&cinf->stapling_mutex);
    Debug("ssl_ocsp", "ssl_callback_ocsp_stapling: failed to get certificate status for %s", cinf->certname);
    return SSL_TLSEXT_ERR_NOACK;
  } else {
    unsigned char *p = static_cast<unsigned char *>(OPENSSL_malloc(cinf->resp_derlen));
    memcpy(p, cinf->resp_der, cinf->resp_derlen);
    ink_mutex_release(&cinf->stapling_mutex);
    SSL_set_tlsext_status_ocsp_resp(ssl, p, cinf->resp_derlen);
    Debug("ssl_ocsp", "ssl_callback_ocsp_stapling: successfully got certificate status for %s", cinf->certname);
    Debug("ssl_ocsp", "is_prefetched:%d uri:%s", cinf->is_prefetched, cinf->uri);
    return SSL_TLSEXT_ERR_OK;
  }
}

#endif /* TS_USE_TLS_OCSP */
