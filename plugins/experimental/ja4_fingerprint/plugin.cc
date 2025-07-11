/** @file ja3_fingerprint.cc
 *
  Plugin JA4 Fingerprint calculates JA4 signatures for incoming SSL traffic.

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

#include "ja4.h"

#include <ts/apidefs.h>
#include <ts/ts.h>

#include <openssl/sha.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

struct JA4_data {
  std::string fingerprint;
  char        IP_addr[INET6_ADDRSTRLEN];
};

// Returns true on success, false otherwise; must succeed before registering
// hooks.
[[nodiscard]] static bool register_plugin();
static void               reserve_user_arg();
static bool               create_log_file();
static void               register_hooks();
static int                handle_client_hello(TSCont cont, TSEvent event, void *edata);
static std::string        get_fingerprint(SSL *ssl);
char                     *get_IP(sockaddr const *s_sockaddr, char res[INET6_ADDRSTRLEN]);
static void               log_fingerprint(JA4_data const *data);
static std::uint16_t      get_version(SSL *ssl);
static std::string        get_first_ALPN(SSL *ssl);
static void               add_ciphers(JA4::TLSClientHelloSummary &summary, SSL *ssl);
static void               add_extensions(JA4::TLSClientHelloSummary &summary, SSL *ssl);
static std::string        hash_with_SHA256(std::string_view sv);
static int                handle_read_request_hdr(TSCont cont, TSEvent event, void *edata);
static void               append_JA4_headers(TSCont cont, TSHttpTxn txnp, std::string const *fingerprint);
static void append_to_field(TSMBuffer bufp, TSMLoc hdr_loc, char const *field, int field_len, char const *value, int value_len);
static int  handle_vconn_close(TSCont cont, TSEvent event, void *edata);

namespace
{
constexpr char const *PLUGIN_NAME{"ja4_fingerprint"};
constexpr char const *PLUGIN_VENDOR{"Apache Software Foundation"};
constexpr char const *PLUGIN_SUPPORT_EMAIL{"dev@trafficserver.apache.org"};

constexpr std::string_view JA4_VIA_HEADER{"x-ja4-via"};

constexpr unsigned int EXT_ALPN{0x10};
constexpr unsigned int EXT_SUPPORTED_VERSIONS{0x2b};
constexpr int          SSL_SUCCESS{1};

DbgCtl dbg_ctl{PLUGIN_NAME};

} // end anonymous namespace

static int *
get_user_arg_index()
{
  static int *arg{new int{}};
  return arg;
}

static TSTextLogObject *
get_log_handle()
{
  static TSTextLogObject *log_handle{new TSTextLogObject{nullptr}};
  return log_handle;
}

static constexpr TSPluginRegistrationInfo
get_registration_info()
{
  TSPluginRegistrationInfo info{};
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = PLUGIN_VENDOR;
  info.support_email = PLUGIN_SUPPORT_EMAIL;
  return info;
}

static constexpr std::uint16_t
make_word(unsigned char lowbyte, unsigned char highbyte)
{
  return (static_cast<std::uint16_t>(highbyte) << 8) | lowbyte;
}

void
TSPluginInit(int /* argc ATS_UNUSED */, char const ** /* argv ATS_UNUSED */)
{
  if (!register_plugin()) {
    TSError("[%s] Failed to register.", PLUGIN_NAME);
    return;
  }
  reserve_user_arg();
  if (!create_log_file()) {
    TSError("[%s] Failed to create log.", PLUGIN_NAME);
    return;
  } else {
    Dbg(dbg_ctl, "Created log file.");
  }
  register_hooks();
}

bool
register_plugin()
{
  constexpr auto info{get_registration_info()};
  return (TS_SUCCESS == TSPluginRegister(&info));
}

bool
create_log_file()
{
  return (TS_SUCCESS == TSTextLogObjectCreate(PLUGIN_NAME, TS_LOG_MODE_ADD_TIMESTAMP, get_log_handle()));
}

void
reserve_user_arg()
{
  TSUserArgIndexReserve(TS_USER_ARGS_VCONN, PLUGIN_NAME, "used to pass JA4 between hooks", get_user_arg_index());
}

void
register_hooks()
{
  TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, TSContCreate(handle_client_hello, nullptr));
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(handle_read_request_hdr, nullptr));
  TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, TSContCreate(handle_vconn_close, nullptr));
}

int
handle_client_hello(TSCont /* cont ATS_UNUSED */, TSEvent event, void *edata)
{
  if (TS_EVENT_SSL_CLIENT_HELLO != event) {
    Dbg(dbg_ctl, "Unexpected event %d.", event);
    // We ignore the event, but we don't want to reject the connection.
    return TS_SUCCESS;
  }
  TSVConn const         ssl_vc{static_cast<TSVConn>(edata)};
  TSSslConnection const ssl{TSVConnSslConnectionGet(ssl_vc)};
  if (nullptr == ssl) {
    Dbg(dbg_ctl, "Could not get SSL object.");
  } else {
    auto data{std::make_unique<JA4_data>()};
    data->fingerprint = get_fingerprint(reinterpret_cast<SSL *>(ssl));
    get_IP(TSNetVConnRemoteAddrGet(ssl_vc), data->IP_addr);
    log_fingerprint(data.get());
    // The VCONN_CLOSE handler is now responsible for freeing the resource.
    TSUserArgSet(ssl_vc, *get_user_arg_index(), static_cast<void *>(data.release()));
  }
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

std::string
get_fingerprint(SSL *ssl)
{
  JA4::TLSClientHelloSummary summary{};
  summary.protocol    = JA4::Protocol::TLS;
  summary.TLS_version = get_version(ssl);
  summary.ALPN        = get_first_ALPN(ssl);
  add_ciphers(summary, ssl);
  add_extensions(summary, ssl);
  std::string result{JA4::make_JA4_fingerprint(summary, hash_with_SHA256)};
  return result;
}

// This implementation is copied verbatim from JA3 fingerprint to make the
// potential for deduplication as obvious as possible.
char *
get_IP(sockaddr const *s_sockaddr, char res[INET6_ADDRSTRLEN])
{
  res[0] = '\0';

  if (s_sockaddr == nullptr) {
    return nullptr;
  }

  switch (s_sockaddr->sa_family) {
  case AF_INET: {
    const struct sockaddr_in *s_sockaddr_in = reinterpret_cast<const struct sockaddr_in *>(s_sockaddr);
    inet_ntop(AF_INET, &s_sockaddr_in->sin_addr, res, INET_ADDRSTRLEN);
  } break;
  case AF_INET6: {
    const struct sockaddr_in6 *s_sockaddr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(s_sockaddr);
    inet_ntop(AF_INET6, &s_sockaddr_in6->sin6_addr, res, INET6_ADDRSTRLEN);
  } break;
  default:
    return nullptr;
  }

  return res[0] ? res : nullptr;
}

void
log_fingerprint(JA4_data const *data)
{
  Dbg(dbg_ctl, "JA4 fingerprint: %s", data->fingerprint.c_str());
  if (TS_ERROR == TSTextLogObjectWrite(*get_log_handle(), "Client IP: %s\tJA4: %s", data->IP_addr, data->fingerprint.c_str())) {
    Dbg(dbg_ctl, "Failed to write to log!");
  }
}

std::uint16_t
get_version(SSL *ssl)
{
  unsigned char const *buf{};
  std::size_t          buflen{};
  if (SSL_SUCCESS == SSL_client_hello_get0_ext(ssl, EXT_SUPPORTED_VERSIONS, &buf, &buflen)) {
    std::uint16_t max_version{0};
    for (std::size_t i{1}; i < buflen; i += 2) {
      std::uint16_t version{make_word(buf[i - 1], buf[i])};
      if ((!JA4::is_GREASE(version)) && version > max_version) {
        max_version = version;
      }
    }
    return max_version;
  } else {
    Dbg(dbg_ctl, "No supported_versions extension... using legacy version.");
    return SSL_client_hello_get0_legacy_version(ssl);
  }
}

std::string
get_first_ALPN(SSL *ssl)
{
  unsigned char const *buf{};
  std::size_t          buflen{};
  std::string          result{""};
  if (SSL_SUCCESS == SSL_client_hello_get0_ext(ssl, EXT_ALPN, &buf, &buflen)) {
    // The first two bytes are a 16bit encoding of the total length.
    unsigned char first_ALPN_length{buf[2]};
    TSAssert(buflen > 4);
    TSAssert(0 != first_ALPN_length);
    result.assign(&buf[3], (&buf[3]) + first_ALPN_length);
  }
  return result;
}

void
add_ciphers(JA4::TLSClientHelloSummary &summary, SSL *ssl)
{
  unsigned char const *buf{};
  std::size_t          buflen{SSL_client_hello_get0_ciphers(ssl, &buf)};
  if (buflen > 0) {
    for (std::size_t i{1}; i < buflen; i += 2) {
      summary.add_cipher(make_word(buf[i], buf[i - 1]));
    }
  } else {
    Dbg(dbg_ctl, "Failed to get ciphers.");
  }
}

void
add_extensions(JA4::TLSClientHelloSummary &summary, SSL *ssl)
{
  int        *buf{};
  std::size_t buflen{};
  if (SSL_SUCCESS == SSL_client_hello_get1_extensions_present(ssl, &buf, &buflen)) {
    for (std::size_t i{1}; i < buflen; i += 2) {
      summary.add_extension(make_word(buf[i], buf[i - 1]));
    }
  }
  OPENSSL_free(buf);
}

std::string
hash_with_SHA256(std::string_view sv)
{
  Dbg(dbg_ctl, "Hashing %s", std::string{sv}.c_str());
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<unsigned char const *>(sv.data()), sv.size(), hash);
  std::string result;
  result.resize(SHA256_DIGEST_LENGTH * 2 + 1);
  for (int i{0}; i < SHA256_DIGEST_LENGTH; ++i) {
    std::snprintf(result.data() + (i * 2), result.size() - (i * 2), "%02x", hash[i]);
  }
  return result;
}

int
handle_read_request_hdr(TSCont cont, TSEvent event, void *edata)
{
  if (TS_EVENT_HTTP_READ_REQUEST_HDR != event) {
    TSError("[%s] Unexpected event, got %d, expected %d", PLUGIN_NAME, event, TS_EVENT_HTTP_READ_REQUEST_HDR);
    return TS_SUCCESS;
  }

  TSHttpTxn txnp{};
  TSHttpSsn ssnp{};
  TSVConn   vconn{};
  if ((txnp = static_cast<TSHttpTxn>(edata)) == nullptr || (ssnp = TSHttpTxnSsnGet(txnp)) == nullptr ||
      (vconn = TSHttpSsnClientVConnGet(ssnp)) == nullptr) {
    Dbg(dbg_ctl, "Failed to get txn/ssn/vconn object.");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  std::string *fingerprint{static_cast<std::string *>(TSUserArgGet(vconn, *get_user_arg_index()))};
  if (fingerprint) {
    append_JA4_headers(cont, txnp, fingerprint);
  } else {
    Dbg(dbg_ctl, "No JA4 fingerprint attached to vconn!");
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

void
append_JA4_headers(TSCont /* cont ATS_UNUSED */, TSHttpTxn txnp, std::string const *fingerprint)
{
  TSMBuffer bufp;
  TSMLoc    hdr_loc;
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    append_to_field(bufp, hdr_loc, "ja4", 3, fingerprint->data(), fingerprint->size());

    TSMgmtString proxy_name = nullptr;
    if (TS_SUCCESS != TSMgmtStringGet("proxy.config.proxy_name", &proxy_name)) {
      TSError("[%s] Failed to get proxy name for %s, set 'proxy.config.proxy_name' in records.config", PLUGIN_NAME,
              JA4_VIA_HEADER.data());
      proxy_name = TSstrdup("unknown");
    }
    append_to_field(bufp, hdr_loc, JA4_VIA_HEADER.data(), static_cast<int>(JA4_VIA_HEADER.length()), proxy_name,
                    static_cast<int>(std::strlen(proxy_name)));
    TSfree(proxy_name);

  } else {
    Dbg(dbg_ctl, "Failed to get headers.");
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

// This function will append value to the last occurrence of field. If none exists, it will
// create a field and append to the headers
void
append_to_field(TSMBuffer bufp, TSMLoc hdr_loc, const char *field, int field_len, const char *value, int value_len)
{
  TSMLoc target = TSMimeHdrFieldFind(bufp, hdr_loc, field, field_len);
  if (target == TS_NULL_MLOC) {
    TSMimeHdrFieldCreateNamed(bufp, hdr_loc, field, field_len, &target);
    TSMimeHdrFieldAppend(bufp, hdr_loc, target);
  } else {
    TSMLoc next = target;
    while (next) {
      target = next;
      next   = TSMimeHdrFieldNextDup(bufp, hdr_loc, target);
    }
  }
  TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, target, -1, value, value_len);
  TSHandleMLocRelease(bufp, hdr_loc, target);
}

int
handle_vconn_close(TSCont /* cont ATS_UNUSED */, TSEvent event, void *edata)
{
  if (TS_EVENT_VCONN_CLOSE != event) {
    Dbg(dbg_ctl, "Unexpected event %d.", event);
    // We ignore the event, but we don't want to reject the connection.
    return TS_SUCCESS;
  }

  TSVConn const ssl_vc{static_cast<TSVConn>(edata)};
  delete static_cast<std::string *>(TSUserArgGet(ssl_vc, *get_user_arg_index()));
  TSUserArgSet(ssl_vc, *get_user_arg_index(), nullptr);
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}
