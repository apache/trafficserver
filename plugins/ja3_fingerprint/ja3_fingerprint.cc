/** @ja3_fingerprint.cc
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

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <getopt.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>
#include <unordered_set>
#include <memory>

#include "ts/apidefs.h"
#include "ts/ts.h"
#include "ts/remap.h"

#ifdef OPENSSL_NO_SSL_INTERN
#undef OPENSSL_NO_SSL_INTERN
#endif

#include <openssl/ssl.h>
#include <openssl/md5.h>
#include <openssl/opensslv.h>

// Get 16bit big endian order and update pointer
#define n2s(c, s) ((s = (((unsigned int)(c[0])) << 8) | (((unsigned int)(c[1])))), c += 2)

const char *PLUGIN_NAME = "ja3_fingerprint";
static DbgCtl dbg_ctl{PLUGIN_NAME};
static TSTextLogObject pluginlog          = nullptr;
static int ja3_idx                        = -1;
static int global_raw_enabled             = 0;
static int global_log_enabled             = 0;
static int global_modify_incoming_enabled = 0;

// GREASE table as in ja3
static const std::unordered_set<uint16_t> GREASE_table = {0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a,
                                                          0x8a8a, 0x9a9a, 0xaaaa, 0xbaba, 0xcaca, 0xdada, 0xeaea, 0xfafa};

struct ja3_data {
  std::string ja3_string;
  char md5_string[33];
  char ip_addr[INET6_ADDRSTRLEN];
};

struct ja3_remap_info {
  int raw_enabled = false;
  int log_enabled = false;
  TSCont handler  = nullptr;

  ~ja3_remap_info()
  {
    if (handler) {
      TSContDestroy(handler);
      handler = nullptr;
    }
  }
};

static int
custom_get_ja3_prefixed(int unit, const unsigned char *&data, int len, std::string &result)
{
  int cnt, tmp;
  bool first = true;
  // Extract each entry and append to result string
  for (cnt = 0; cnt < len; cnt += unit) {
    if (unit == 1) {
      tmp = *(data++);
    } else {
      n2s(data, tmp);
    }

    // Check for GREASE for 16-bit values, append only if non-GREASE
    if (unit != 2 || GREASE_table.find(tmp) == GREASE_table.end()) {
      if (!first) {
        result += '-';
      }
      first   = false;
      result += std::to_string(tmp);
    }
  }
  return 0;
}

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
custom_get_ja3(SSL *s)
{
  std::string ja3;
  size_t len;
  const unsigned char *p;

  // Get version
  unsigned int version  = SSL_client_hello_get0_legacy_version(s);
  ja3                  += std::to_string(version) + ',';

  // Get cipher suites
  len = SSL_client_hello_get0_ciphers(s, &p);
  custom_get_ja3_prefixed(2, p, len, ja3);
  ja3 += ',';

  // Get extensions
  int *o;
  std::string eclist, ecpflist;
  if (SSL_client_hello_get0_ext(s, 0x0a, &p, &len) == 1) {
    // Skip first 2 bytes since we already have length
    p   += 2;
    len -= 2;
    custom_get_ja3_prefixed(2, p, len, eclist);
  }
  if (SSL_client_hello_get0_ext(s, 0x0b, &p, &len) == 1) {
    // Skip first byte since we already have length
    ++p;
    --len;
    custom_get_ja3_prefixed(1, p, len, ecpflist);
  }
  if (SSL_client_hello_get1_extensions_present(s, &o, &len) == 1) {
    bool first = true;
    for (size_t i = 0; i < len; i++) {
      int type = o[i];
      if (GREASE_table.find(type) == GREASE_table.end()) {
        if (!first) {
          ja3 += '-';
        }
        first  = false;
        ja3   += std::to_string(type);
      }
    }
    OPENSSL_free(o);
  }
  ja3 += "," + eclist + "," + ecpflist;
  return ja3;
}

// This function will append value to the last occurrence of field. If none exists, it will
// create a field and append to the headers
static void
append_to_field(TSMBuffer bufp, TSMLoc hdr_loc, const char *field, int field_len, const char *value, int value_len)
{
  if (!bufp || !hdr_loc || !field || field_len <= 0) {
    return;
  }

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

static int
client_hello_ja3_handler(TSCont contp, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  switch (event) {
  case TS_EVENT_SSL_CLIENT_HELLO: {
    TSSslConnection sslobj = TSVConnSslConnectionGet(ssl_vc);

    // OpenSSL handle
    SSL *ssl = reinterpret_cast<SSL *>(sslobj);

    ja3_data *data = new ja3_data;
    data->ja3_string.append(custom_get_ja3(ssl));
    getIP(TSNetVConnRemoteAddrGet(ssl_vc), data->ip_addr);

    TSUserArgSet(ssl_vc, ja3_idx, static_cast<void *>(data));
    Dbg(dbg_ctl, "client_hello_ja3_handler(): JA3: %s", data->ja3_string.c_str());

    // MD5 hash
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char *)data->ja3_string.c_str(), data->ja3_string.length(), digest);

    // Validate that the buffer is the same size as we will be writing into (compile time)
    static_assert((16 * 2 + 1) == sizeof(data->md5_string));
    for (int i = 0; i < 16; i++) {
      snprintf(&(data->md5_string[i * 2]), sizeof(data->md5_string) - (i * 2), "%02x", static_cast<unsigned int>(digest[i]));
    }
    Dbg(dbg_ctl, "Fingerprint: %s", data->md5_string);
    break;
  }
  case TS_EVENT_VCONN_CLOSE: {
    // Clean up
    ja3_data *data = static_cast<ja3_data *>(TSUserArgGet(ssl_vc, ja3_idx));

    if (data == nullptr) {
      Dbg(dbg_ctl, "client_hello_ja3_handler(): Failed to retrieve ja3 data at VCONN_CLOSE.");
      return TS_ERROR;
    }

    TSUserArgSet(ssl_vc, ja3_idx, nullptr);

    delete data;
    break;
  }
  default: {
    Dbg(dbg_ctl, "client_hello_ja3_handler(): Unexpected event.");
    break;
  }
  }
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

static int
req_hdr_ja3_handler(TSCont contp, TSEvent event, void *edata)
{
  TSEvent expected_event = global_modify_incoming_enabled ? TS_EVENT_HTTP_READ_REQUEST_HDR : TS_EVENT_HTTP_SEND_REQUEST_HDR;
  if (event != expected_event) {
    TSError("[%s] req_hdr_ja3_handler(): Unexpected event, got %d, expected %d", PLUGIN_NAME, event, expected_event);
    TSAssert(event == expected_event);
  }

  TSHttpTxn txnp = nullptr;
  TSHttpSsn ssnp = nullptr;
  TSVConn vconn  = nullptr;
  if ((txnp = static_cast<TSHttpTxn>(edata)) == nullptr || (ssnp = TSHttpTxnSsnGet(txnp)) == nullptr ||
      (vconn = TSHttpSsnClientVConnGet(ssnp)) == nullptr) {
    Dbg(dbg_ctl, "req_hdr_ja3_handler(): Failure to retrieve txn/ssn/vconn object.");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  // Retrieve ja3_data from vconn args
  ja3_data *data = static_cast<ja3_data *>(TSUserArgGet(vconn, ja3_idx));
  if (data) {
    // Decide global or remap
    ja3_remap_info *remap_info = static_cast<ja3_remap_info *>(TSContDataGet(contp));
    bool raw_flag              = remap_info ? remap_info->raw_enabled : global_raw_enabled;
    bool log_flag              = remap_info ? remap_info->log_enabled : global_log_enabled;
    Dbg(dbg_ctl, "req_hdr_ja3_handler(): Found ja3 string.");

    // Get handle to headers
    TSMBuffer bufp;
    TSMLoc hdr_loc;
    if (global_modify_incoming_enabled) {
      TSAssert(TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc));
    } else {
      TSAssert(TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &bufp, &hdr_loc));
    }

    // Add JA3 md5 fingerprints
    append_to_field(bufp, hdr_loc, "X-JA3-Sig", 9, data->md5_string, 32);

    // If raw string is configured, added JA3 raw string to header as well
    if (raw_flag) {
      append_to_field(bufp, hdr_loc, "x-JA3-RAW", 9, data->ja3_string.data(), data->ja3_string.size());
    }
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    // Write to logfile
    if (log_flag) {
      TSTextLogObjectWrite(pluginlog, "Client IP: %s\tJA3: %.*s\tMD5: %.*s", data->ip_addr,
                           static_cast<int>(data->ja3_string.size()), data->ja3_string.data(), 32, data->md5_string);
    }
  } else {
    Dbg(dbg_ctl, "req_hdr_ja3_handler(): ja3 data not set. Not SSL vconn. Abort.");
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static bool
read_config_option(int argc, const char *argv[], int &raw, int &log, int &modify_incoming)
{
  const struct option longopts[] = {
    {"ja3raw",          no_argument, &raw,             1},
    {"ja3log",          no_argument, &log,             1},
    {"modify-incoming", no_argument, &modify_incoming, 1},
    {nullptr,           0,           nullptr,          0}
  };

  int opt = 0;
  while ((opt = getopt_long(argc, const_cast<char *const *>(argv), "", longopts, nullptr)) >= 0) {
    switch (opt) {
    case '?':
      Dbg(dbg_ctl, "read_config_option(): Unrecognized command arguments.");
    case 0:
    case -1:
      break;
    default:
      Dbg(dbg_ctl, "read_config_option(): Unexpected options error.");
      return false;
    }
  }

  Dbg(dbg_ctl, "read_config_option(): ja3 raw is %s", (raw == 1) ? "enabled" : "disabled");
  Dbg(dbg_ctl, "read_config_option(): ja3 logging is %s", (log == 1) ? "enabled" : "disabled");
  Dbg(dbg_ctl, "read_config_option(): ja3 modify-incoming is %s", (modify_incoming == 1) ? "enabled" : "disabled");
  return true;
}

void
TSPluginInit(int argc, const char *argv[])
{
  Dbg(dbg_ctl, "Initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Oath";
  info.support_email = "zeyuany@oath.com";

  // Options
  if (!read_config_option(argc, argv, global_raw_enabled, global_log_enabled, global_modify_incoming_enabled)) {
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
    TSCont ja3_cont = TSContCreate(client_hello_ja3_handler, nullptr);
    TSUserArgIndexReserve(TS_USER_ARGS_VCONN, PLUGIN_NAME, "used to pass ja3", &ja3_idx);
    TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, ja3_cont);
    TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, ja3_cont);

    TSHttpHookID const hook = global_modify_incoming_enabled ? TS_HTTP_READ_REQUEST_HDR_HOOK : TS_HTTP_SEND_REQUEST_HDR_HOOK;
    TSHttpHookAdd(hook, TSContCreate(req_hdr_ja3_handler, nullptr));
  }

  return;
}

// Remap Part
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  Dbg(dbg_ctl, "JA3 Remap Plugin initializing..");

  // Check if there is config conflict as both global and remap plugin
  if (ja3_idx >= 0) {
    TSError(PLUGIN_NAME, "TSRemapInit(): JA3 configured as both global and remap. Check plugin.config.");
    return TS_ERROR;
  }

  // Set up SNI handler for all TLS connections
  TSCont ja3_cont = TSContCreate(client_hello_ja3_handler, nullptr);
  TSUserArgIndexReserve(TS_USER_ARGS_VCONN, PLUGIN_NAME, "Used to pass ja3", &ja3_idx);
  TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, ja3_cont);
  TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, ja3_cont);

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
                          discard_modify_incoming)) {
    Dbg(dbg_ctl, "TSRemapNewInstance(): Bad arguments");
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
    TSError("[%s] TSRemapDoRemap(): Invalid private data or RRI or handler.", PLUGIN_NAME);
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
