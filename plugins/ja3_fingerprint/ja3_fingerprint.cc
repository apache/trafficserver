/** @file ja3_fingerprint.cc
 *
  Plugin JA3 Fingerprint calculates JA3 signatures for incoming SSL traffic.

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

#include "ja3_utils.h"

#include <getopt.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string_view>

#include "ts/apidefs.h"
#include "ts/ts.h"
#include "ts/remap.h"

#ifdef OPENSSL_NO_SSL_INTERN
#undef OPENSSL_NO_SSL_INTERN
#endif

#include <openssl/ssl.h>
#include <openssl/md5.h>
#include <openssl/opensslv.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace
{
constexpr std::string_view JA3_VIA_HEADER{"x-ja3-via"};
constexpr int              ja3_hash_included_byte_count{16};
static_assert(ja3_hash_included_byte_count <= MD5_DIGEST_LENGTH);

constexpr int ja3_hash_hex_string_with_null_terminator_length{2 * ja3_hash_included_byte_count + 1};

} // end anonymous namespace

const char            *PLUGIN_NAME = "ja3_fingerprint";
static DbgCtl          dbg_ctl{PLUGIN_NAME};
static TSTextLogObject pluginlog                      = nullptr;
static int             ja3_idx                        = -1;
static int             global_raw_enabled             = 0;
static int             global_log_enabled             = 0;
static int             global_modify_incoming_enabled = 0;
static int             global_preserve_enabled        = 0;

struct ja3_data {
  std::string ja3_string;
  char        md5_string[ja3_hash_hex_string_with_null_terminator_length];
  char        ip_addr[INET6_ADDRSTRLEN];

  char const *
  update_fingerprint()
  {
    // Validate that the buffer is the same size as we will be writing into.
    static_assert(ja3_hash_hex_string_with_null_terminator_length == sizeof(this->md5_string));

    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<unsigned char const *>(this->ja3_string.c_str()), this->ja3_string.length(), digest);
    for (int i{0}; i < ja3_hash_included_byte_count; ++i) {
      std::snprintf(&(this->md5_string[i * 2]), sizeof(this->md5_string) - (i * 2), "%02x", static_cast<unsigned int>(digest[i]));
    }
    return this->md5_string;
  }
};

struct ja3_remap_info {
  int    raw_enabled      = false;
  int    log_enabled      = false;
  int    preserve_enabled = false;
  TSCont handler          = nullptr;

  ~ja3_remap_info()
  {
    if (handler) {
      TSContDestroy(handler);
      handler = nullptr;
    }
  }
};

char *
getIP(sockaddr const *s_sockaddr, char res[INET6_ADDRSTRLEN])
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

static std::string
custom_get_ja3(SSL *ssl)
{
  std::string          result;
  std::size_t          len{};
  const unsigned char *buf{};

  // Get version
  unsigned int version = SSL_client_hello_get0_legacy_version(ssl);
  result.append(std::to_string(version));
  result.push_back(',');

  // Get cipher suites
  len = SSL_client_hello_get0_ciphers(ssl, &buf);
  result.append(ja3::encode_word_buffer(buf, len));
  result.push_back(',');

  // Get extensions
  int *extension_ids{};
  if (SSL_client_hello_get1_extensions_present(ssl, &extension_ids, &len) == 1) {
    result.append(ja3::encode_integer_buffer(extension_ids, len));
    OPENSSL_free(extension_ids);
  }
  result.push_back(',');

  // Get elliptic curves
  if (SSL_client_hello_get0_ext(ssl, 0x0a, &buf, &len) == 1) {
    // Skip first 2 bytes since we already have length
    result.append(ja3::encode_word_buffer(buf + 2, len - 2));
  }
  result.push_back(',');

  // Get elliptic curve point formats
  if (SSL_client_hello_get0_ext(ssl, 0x0b, &buf, &len) == 1) {
    // Skip first byte since we already have length
    result.append(ja3::encode_byte_buffer(buf + 1, len - 1));
  }
  return result;
}

// This function will append value to the last occurrence of field. If none exists, it will
// create a field and append to the headers
static void
append_to_field(TSMBuffer bufp, TSMLoc hdr_loc, const char *field, int field_len, const char *value, int value_len, bool preserve)
{
  if (!bufp || !hdr_loc || !field || field_len <= 0) {
    return;
  }

  TSMLoc target = TSMimeHdrFieldFind(bufp, hdr_loc, field, field_len);
  if (target == TS_NULL_MLOC) {
    TSMimeHdrFieldCreateNamed(bufp, hdr_loc, field, field_len, &target);
    TSMimeHdrFieldAppend(bufp, hdr_loc, target);
    TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, target, -1, value, value_len);
  } else if (!preserve) {
    TSMLoc next = target;
    while (next) {
      target = next;
      next   = TSMimeHdrFieldNextDup(bufp, hdr_loc, target);
    }
    TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, target, -1, value, value_len);
  }
  TSHandleMLocRelease(bufp, hdr_loc, target);
}

static ja3_data *
create_ja3_data(TSVConn const ssl_vc)
{
  ja3_data         *result = new ja3_data;
  std::string const raw_ja3_string{custom_get_ja3(reinterpret_cast<SSL *>(TSVConnSslConnectionGet(ssl_vc)))};
  result->ja3_string = std::move(raw_ja3_string);
  getIP(TSNetVConnRemoteAddrGet(ssl_vc), result->ip_addr);
  return result;
}

static int
tls_client_hello_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  if (TS_EVENT_SSL_CLIENT_HELLO != event) {
    Dbg(dbg_ctl, "Unexpected event %d.", event);
    // We ignore the event, but we don't want to reject the connection.
    return TS_SUCCESS;
  }

  TSVConn const ssl_vc{static_cast<TSVConn>(edata)};
  ja3_data     *ja3_vconn_data{create_ja3_data(ssl_vc)};
  TSUserArgSet(ssl_vc, ja3_idx, static_cast<void *>(ja3_vconn_data));
  Dbg(dbg_ctl, "JA3 raw: %s", ja3_vconn_data->ja3_string.c_str());
  char const *fingerprint{ja3_vconn_data->update_fingerprint()};
  Dbg(dbg_ctl, "JA3 fingerprint: %s", fingerprint);
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

static int
vconn_close_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  if (TS_EVENT_VCONN_CLOSE != event) {
    Dbg(dbg_ctl, "Unexpected event %d.", event);
    // We ignore the event, but we don't want to reject the connection.
    return TS_SUCCESS;
  }

  TSVConn const ssl_vc{static_cast<TSVConn>(edata)};
  delete static_cast<ja3_data *>(TSUserArgGet(ssl_vc, ja3_idx));
  TSUserArgSet(ssl_vc, ja3_idx, nullptr);
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

static void
modify_ja3_headers(TSCont contp, TSHttpTxn txnp, ja3_data const *ja3_vconn_data)
{
  // Decide global or remap
  ja3_remap_info *remap_info    = static_cast<ja3_remap_info *>(TSContDataGet(contp));
  bool            raw_flag      = remap_info ? remap_info->raw_enabled : global_raw_enabled;
  bool            log_flag      = remap_info ? remap_info->log_enabled : global_log_enabled;
  bool            preserve_flag = remap_info ? remap_info->preserve_enabled : global_preserve_enabled;
  Dbg(dbg_ctl, "Found ja3 string.");

  // Get handle to headers
  TSMBuffer bufp;
  TSMLoc    hdr_loc;
  if (global_modify_incoming_enabled) {
    TSAssert(TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc));
  } else {
    TSAssert(TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &bufp, &hdr_loc));
  }

  TSMgmtString proxy_name = nullptr;
  if (TS_SUCCESS != TSMgmtStringGet("proxy.config.proxy_name", &proxy_name)) {
    TSError("[%s] Failed to get proxy name for %s, set 'proxy.config.proxy_name' in records.config", PLUGIN_NAME,
            JA3_VIA_HEADER.data());
    proxy_name = TSstrdup("unknown");
  }
  append_to_field(bufp, hdr_loc, JA3_VIA_HEADER.data(), static_cast<int>(JA3_VIA_HEADER.length()), proxy_name,
                  static_cast<int>(std::strlen(proxy_name)), preserve_flag);
  TSfree(proxy_name);

  // Add JA3 md5 fingerprints
  append_to_field(bufp, hdr_loc, "x-ja3-sig", 9, ja3_vconn_data->md5_string, 32, preserve_flag);

  // If raw string is configured, added JA3 raw string to header as well
  if (raw_flag) {
    append_to_field(bufp, hdr_loc, "x-ja3-raw", 9, ja3_vconn_data->ja3_string.data(), ja3_vconn_data->ja3_string.size(),
                    preserve_flag);
  }
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  // Write to logfile
  if (log_flag) {
    TSTextLogObjectWrite(pluginlog, "Client IP: %s\tJA3: %.*s\tMD5: %.*s", ja3_vconn_data->ip_addr,
                         static_cast<int>(ja3_vconn_data->ja3_string.size()), ja3_vconn_data->ja3_string.data(), 32,
                         ja3_vconn_data->md5_string);
  }
}

static int
req_hdr_ja3_handler(TSCont contp, TSEvent event, void *edata)
{
  TSEvent expected_event = global_modify_incoming_enabled ? TS_EVENT_HTTP_READ_REQUEST_HDR : TS_EVENT_HTTP_SEND_REQUEST_HDR;
  if (event != expected_event) {
    TSError("[%s] Unexpected event, got %d, expected %d", PLUGIN_NAME, event, expected_event);
    TSAssert(event == expected_event);
  }

  TSHttpTxn txnp{};
  TSHttpSsn ssnp{};
  TSVConn   vconn{};
  if ((txnp = static_cast<TSHttpTxn>(edata)) == nullptr || (ssnp = TSHttpTxnSsnGet(txnp)) == nullptr ||
      (vconn = TSHttpSsnClientVConnGet(ssnp)) == nullptr) {
    Dbg(dbg_ctl, "Failure to retrieve txn/ssn/vconn object.");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  // Retrieve ja3_data from vconn args
  ja3_data const *ja3_vconn_data = static_cast<ja3_data *>(TSUserArgGet(vconn, ja3_idx));
  if (ja3_vconn_data) {
    modify_ja3_headers(contp, txnp, ja3_vconn_data);
  } else {
    Dbg(dbg_ctl, "ja3 data not set. Not SSL vconn. Abort.");
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static bool
read_config_option(int argc, const char *argv[], int &raw, int &log, int &modify_incoming, int &preserve)
{
  const struct option longopts[] = {
    {"ja3raw",          no_argument, &raw,             1},
    {"ja3log",          no_argument, &log,             1},
    {"modify-incoming", no_argument, &modify_incoming, 1},
    {"preserve",        no_argument, &preserve,        1},
    {nullptr,           0,           nullptr,          0}
  };

  int opt = 0;
  while ((opt = getopt_long(argc, const_cast<char *const *>(argv), "", longopts, nullptr)) >= 0) {
    switch (opt) {
    case '?':
      Dbg(dbg_ctl, "Unrecognized command arguments.");
    case 0:
    case -1:
      break;
    default:
      Dbg(dbg_ctl, "Unexpected options error.");
      return false;
    }
  }

  Dbg(dbg_ctl, "ja3 raw is %s", (raw == 1) ? "enabled" : "disabled");
  Dbg(dbg_ctl, "ja3 logging is %s", (log == 1) ? "enabled" : "disabled");
  Dbg(dbg_ctl, "ja3 modify-incoming is %s", (modify_incoming == 1) ? "enabled" : "disabled");
  Dbg(dbg_ctl, "ja3 preserve is %s", (preserve == 1) ? "enabled" : "disabled");
  return true;
}

void
TSPluginInit(int argc, const char *argv[])
{
  Dbg(dbg_ctl, "Initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  // Options
  if (!read_config_option(argc, argv, global_raw_enabled, global_log_enabled, global_modify_incoming_enabled,
                          global_preserve_enabled)) {
    return;
  }

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Unable to initialize plugin. Failed to register.", PLUGIN_NAME);
  } else {
    if (global_log_enabled && !pluginlog) {
      TSAssert(TS_SUCCESS == TSTextLogObjectCreate(PLUGIN_NAME, TS_LOG_MODE_ADD_TIMESTAMP, &pluginlog));
      Dbg(dbg_ctl, "log object created successfully");
    }
    // SNI handler
    TSUserArgIndexReserve(TS_USER_ARGS_VCONN, PLUGIN_NAME, "used to pass ja3", &ja3_idx);
    TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, TSContCreate(tls_client_hello_handler, nullptr));
    TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, TSContCreate(vconn_close_handler, nullptr));

    TSHttpHookID const hook = global_modify_incoming_enabled ? TS_HTTP_READ_REQUEST_HDR_HOOK : TS_HTTP_SEND_REQUEST_HDR_HOOK;
    TSHttpHookAdd(hook, TSContCreate(req_hdr_ja3_handler, nullptr));
  }

  return;
}

// Remap Part
TSReturnCode
TSRemapInit(TSRemapInterface * /* api_info ATS_UNUSED */, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  Dbg(dbg_ctl, "JA3 Remap Plugin initializing..");

  // Check if there is config conflict as both global and remap plugin
  if (ja3_idx >= 0) {
    TSError("[%s] JA3 configured as both global and remap. Check plugin.config.", PLUGIN_NAME);
    return TS_ERROR;
  }

  // Set up SNI handler for all TLS connections
  TSUserArgIndexReserve(TS_USER_ARGS_VCONN, PLUGIN_NAME, "Used to pass ja3", &ja3_idx);
  TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, TSContCreate(tls_client_hello_handler, nullptr));
  TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, TSContCreate(vconn_close_handler, nullptr));

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  Dbg(dbg_ctl, "New instance for client matching %s to %s", argv[0], argv[1]);
  std::unique_ptr<ja3_remap_info> remap_info{new ja3_remap_info};

  // Parse parameters
  int discard_modify_incoming = -1; // Not used for remap.
  if (!read_config_option(argc - 1, const_cast<const char **>(argv + 1), remap_info->raw_enabled, remap_info->log_enabled,
                          discard_modify_incoming, remap_info->preserve_enabled)) {
    Dbg(dbg_ctl, "Bad arguments");
    return TS_ERROR;
  }

  if (remap_info->log_enabled && !pluginlog) {
    TSAssert(TS_SUCCESS == TSTextLogObjectCreate(PLUGIN_NAME, TS_LOG_MODE_ADD_TIMESTAMP, &pluginlog));
    Dbg(dbg_ctl, "log object created successfully");
  }

  // Create continuation
  remap_info->handler = TSContCreate(req_hdr_ja3_handler, nullptr);
  TSContDataSet(remap_info->handler, remap_info.get());

  // Pass to other remap plugin functions
  *ih = static_cast<void *>(remap_info.release());
  return TS_SUCCESS;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  auto remap_info = static_cast<ja3_remap_info *>(ih);

  // On remap, set up handler at send req hook to send JA3 data as header
  if (!remap_info || !rri || !(remap_info->handler)) {
    TSError("[%s] Invalid private data or RRI or handler.", PLUGIN_NAME);
  } else {
    TSHttpTxnHookAdd(rh, TS_HTTP_SEND_REQUEST_HDR_HOOK, remap_info->handler);
  }

  return TSREMAP_NO_REMAP;
}

void
TSRemapDeleteInstance(void *ih)
{
  auto remap_info = static_cast<ja3_remap_info *>(ih);
  delete remap_info;
  ih = nullptr;
}
