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

#pragma once

#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "tsutil/Regex.h"

// Constants
inline constexpr int MAX_KEY_LEN   = 256;
inline constexpr int MAX_SIG_SIZE  = 20;
inline constexpr int SHA1_SIG_SIZE = 20;
inline constexpr int MD5_SIG_SIZE  = 16;
inline constexpr int MAX_REQ_LEN   = 8192;
inline constexpr int MAX_QUERY_LEN = 4096;
inline constexpr int MAX_SEGMENTS  = 64;
inline constexpr int MAX_PARTS     = 32;

// Query/path parameter identifiers
inline constexpr std::string_view CIP_QSTRING = "C"; ///< Client IP address
inline constexpr std::string_view EXP_QSTRING = "E"; ///< Expiration (seconds since epoch)
inline constexpr std::string_view ALG_QSTRING = "A"; ///< Algorithm number
inline constexpr std::string_view KIN_QSTRING = "K"; ///< Key index
inline constexpr std::string_view PAR_QSTRING = "P"; ///< Parts to sign
inline constexpr std::string_view SIG_QSTRING = "S"; ///< Signature (must be last)

// Signing algorithms
inline constexpr int USIG_HMAC_SHA1 = 1;
inline constexpr int USIG_HMAC_MD5  = 2;

/// Error status codes for denied requests.
enum class UrlSigErrStatus {
  FORBIDDEN,         ///< Return 403
  MOVED_TEMPORARILY, ///< Return 302 redirect
};

/// Result of URL signature validation.
enum class UrlSigStatus {
  ALLOW,
  DENY,
};

/// Detailed result from signature validation.
struct UrlSigResult {
  UrlSigStatus status = UrlSigStatus::DENY;
  std::string  reason;                  ///< Human-readable denial reason (empty on allow)
  std::string  new_path;                ///< Rewritten path (for path-param mode), empty if unchanged
  std::string  app_query;               ///< Application query string to preserve (empty if none)
  bool         has_path_params = false; ///< Whether signing used path params mode
};

/// Extracted signing parameters from a URL query or path-param string.
///
/// All fields are views into the original parameter string; they are empty if
/// the corresponding key was not present.  Call @c parse() to populate them in
/// a single linear scan.
struct SigningParams {
  std::string_view client_ip;  ///< C= client IP
  std::string_view expiration; ///< E= expiry (seconds since epoch, unparsed)
  std::string_view algorithm;  ///< A= algorithm number, unparsed
  std::string_view key_index;  ///< K= key index, unparsed
  std::string_view parts;      ///< P= parts bitmask string
  std::string_view signature;  ///< S= HMAC hex signature

  /** Populate fields by a single linear scan of @a params.
   *
   * @param params Delimiter-separated key=value pairs (no leading delimiter).
   * @param delim  Token separator: '&' for query strings, ';' for path params.
   *
   * Each signing key is a single ASCII letter (A C E K P S) followed immediately
   * by '='.  Unknown tokens are skipped.  Only the first occurrence of each key
   * is recorded.
   */
  void parse(std::string_view params, char delim);
};

/// Plugin configuration loaded from config file.
struct UrlSigConfig {
  UrlSigErrStatus          err_status = UrlSigErrStatus::FORBIDDEN;
  std::string              err_url;
  std::vector<std::string> keys;
  bool                     pristine_url_flag = false;
  std::string              sig_anchor;
  bool                     ignore_expiry = false;

  Regex excl_regex; ///< Optional compiled exclusion pattern. Non-empty means check before signing.
};

/// Load configuration from an input stream.
/// @param input stream to read config from.
/// @param[out] error populated with error message on failure.
/// @return populated config on success, nullptr on failure.
std::unique_ptr<UrlSigConfig> load_config(std::istream &input, std::string &error);

/// Validate a URL's signature.
/// @param cfg plugin configuration.
/// @param url full URL to validate.
/// @param client_ip client IP address string (from cache layer).
/// @param now current time in seconds since epoch (0 = use system time).
/// @return validation result with status, reason, and rewrite info.
UrlSigResult validate_url(UrlSigConfig const &cfg, std::string_view url, std::string_view client_ip, time_t now = 0);

/// Extract application query parameters (non-signing params) from query string.
/// @param query query string (without leading '?').
/// @return application query string, empty if none.
std::string get_app_query_string(std::string_view query);

/// Parse URL with path-embedded signing parameters.
/// @param url full URL.
/// @param anchor sig_anchor string (empty if none).
/// @param[out] new_path rewritten path without signing segment.
/// @param[out] signed_seg raw encoded signing parameter segment.
/// @return reconstructed URL with decoded params, empty on failure.
std::string url_parse_path_params(std::string_view url, std::string_view anchor, std::string &new_path, std::string &signed_seg);
