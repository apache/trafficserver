/** @file
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

#include "url_sig.h"

#include <climits>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "tsutil/Regex.h"

#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/remap_version.h>

static char const PLUGIN_NAME[] = "url_sig";

static DbgCtl dbg_ctl{PLUGIN_NAME};

namespace
{

/// Get client IP as string from transaction.
std::string
get_client_ip_str(TSHttpTxn txnp)
{
  struct sockaddr const *const ip = TSHttpTxnClientAddrGet(txnp);
  if (ip == nullptr) {
    return {};
  }

  char ipstr[INET6_ADDRSTRLEN] = {'\0'};
  switch (ip->sa_family) {
  case AF_INET:
    inet_ntop(AF_INET, &(reinterpret_cast<struct sockaddr_in const *>(ip)->sin_addr), ipstr, sizeof(ipstr));
    break;
  case AF_INET6:
    inet_ntop(AF_INET6, &(reinterpret_cast<struct sockaddr_in6 const *>(ip)->sin6_addr), ipstr, sizeof(ipstr));
    break;
  default:
    return {};
  }
  return std::string(ipstr);
}

} // end namespace

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);
  Dbg(dbg_ctl, "plugin is successfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  if (argc < 3 || 4 < argc) {
    snprintf(errbuf, errbuf_size,
             "[TSRemapNewInstance] - Argument count wrong (%d)... config file path is required first pparam, \"pristineurl\" is "
             "optional second pparam.",
             argc);
    return TS_ERROR;
  }

  Dbg(dbg_ctl, "Initializing remap function of %s -> %s with config from %s", argv[0], argv[1], argv[2]);

  // Resolve config file path.
  char        config_filepath_buf[PATH_MAX];
  char const *config_file = argv[2];
  if (argv[2][0] != '/') {
    snprintf(config_filepath_buf, sizeof(config_filepath_buf), "%s/%s", TSConfigDirGet(), argv[2]);
    config_file = config_filepath_buf;
  }

  Dbg(dbg_ctl, "config file name: %s", config_file);

  std::ifstream file(config_file);
  if (!file.is_open()) {
    snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - Error opening file %s", config_file);
    return TS_ERROR;
  }

  std::string error;
  auto        cfg = load_config(file, error);
  if (!cfg) {
    snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - %s", error.c_str());
    return TS_ERROR;
  }

  // Handle excl_regex: re-read file to find the pattern.
  file.clear();
  file.seekg(0);
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string_view key(line.data(), eq);
    // Trim trailing whitespace from key.
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
      key.remove_suffix(1);
    }
    if (key == "excl_regex") {
      std::string_view value(line.data() + eq + 1, line.size() - eq - 1);
      // Trim whitespace.
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
      }
      while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\n')) {
        value.remove_suffix(1);
      }

      auto const  regex = std::make_shared<Regex>();
      std::string re_error;
      int         erroffset = 0;
      if (regex->compile(std::string(value).c_str(), re_error, erroffset, 0)) {
        cfg->excl_regex_match = [regex](std::string_view url) -> bool { return regex->exec(url); };
      } else {
        Dbg(dbg_ctl, "Regex compilation failed with error (%s) at character %d", re_error.c_str(), erroffset);
      }
      break; // Only first excl_regex used.
    }
  }
  file.close();

  // Handle pristineurl pparam override.
  if (4 <= argc) {
    if (strcasecmp(argv[3], "pristineurl") == 0) {
      cfg->pristine_url_flag = true;
      Dbg(dbg_ctl, "Pristine URLs (from args) will be used");
    } else {
      snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - second pparam (if present) must be pristineurl");
      return TS_ERROR;
    }
  }

  if (cfg->ignore_expiry) {
    TSError("[url_sig] Plugin IGNORES sig expiration");
  }

  *ih = static_cast<void *>(cfg.release());
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  delete static_cast<UrlSigConfig *>(ih);
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  auto const *const cfg = static_cast<UrlSigConfig *>(ih);

  // Get URL.
  int         url_len         = 0;
  char *const current_url_raw = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &url_len);
  std::string url_to_check(current_url_raw, url_len);
  TSfree(current_url_raw);

  if (cfg->pristine_url_flag) {
    TSMBuffer    mbuf;
    TSMLoc       ul;
    TSReturnCode rc = TSHttpTxnPristineUrlGet(txnp, &mbuf, &ul);
    if (rc != TS_SUCCESS) {
      Dbg(dbg_ctl, "[url_sig] Failed call to TSHttpTxnPristineUrlGet()");
      goto deny;
    }
    int         pristine_len = 0;
    char *const pristine_raw = TSUrlStringGet(mbuf, ul, &pristine_len);
    url_to_check             = std::string(pristine_raw, pristine_len);
    TSfree(pristine_raw);

    if (static_cast<int>(url_to_check.size()) >= MAX_REQ_LEN - 1) {
      Dbg(dbg_ctl, "[url_sig] Pristine URL string too long.");
      goto deny;
    }
  }

  Dbg(dbg_ctl, "%s", url_to_check.c_str());

  // Get client IP.
  {
    std::string const client_ip = get_client_ip_str(txnp);

    // Validate.
    UrlSigResult const result = validate_url(*cfg, url_to_check, client_ip);

    if (result.status == UrlSigStatus::ALLOW) {
      // Apply path rewrite if path params mode.
      if (result.has_path_params && !result.new_path.empty()) {
        TSUrlPathSet(rri->requestBufp, rri->requestUrl, result.new_path.c_str(), result.new_path.size());
      }

      // Set or clear query string.
      if (!result.app_query.empty()) {
        TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, result.app_query.c_str(), result.app_query.size());
      } else {
        TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, nullptr, 0);
      }

      return TSREMAP_NO_REMAP;
    }

    // Deny path — log reason.
    Dbg(dbg_ctl, "[URL=%s]: %s", url_to_check.c_str(), result.reason.c_str());
  }

deny:
  switch (cfg->err_status) {
  case UrlSigErrStatus::MOVED_TEMPORARILY: {
    Dbg(dbg_ctl, "Redirecting to %s", cfg->err_url.c_str());
    char const       *start = cfg->err_url.c_str();
    char const *const end   = start + cfg->err_url.size();
    if (TSUrlParse(rri->requestBufp, rri->requestUrl, &start, end) != TS_PARSE_DONE) {
      Dbg(dbg_ctl, "[url_sig] Error in TSUrlParse!");
    }
    rri->redirect = 1;
    break;
  }
  default:
    TSHttpTxnErrorBodySet(txnp, TSstrdup("Authorization Denied"), sizeof("Authorization Denied") - 1, TSstrdup("text/plain"));
    break;
  }

  auto const status =
    cfg->err_status == UrlSigErrStatus::MOVED_TEMPORARILY ? TS_HTTP_STATUS_MOVED_TEMPORARILY : TS_HTTP_STATUS_FORBIDDEN;
  TSHttpTxnStatusSet(txnp, status, PLUGIN_NAME);

  return TSREMAP_DID_REMAP;
}
