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

#include <charconv>
#include <cstring>
#include <ctime>
#include <vector>

#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace
{

/// Compute HMAC signature and return hex string.
std::string
compute_hmac(int const algorithm, std::string_view const key, std::string_view const data)
{
  EVP_MD const *md           = nullptr;
  unsigned int  expected_len = 0;

  switch (algorithm) {
  case USIG_HMAC_SHA1:
    md           = EVP_sha1();
    expected_len = SHA1_SIG_SIZE;
    break;
  case USIG_HMAC_MD5:
    md           = EVP_md5();
    expected_len = MD5_SIG_SIZE;
    break;
  default:
    return {};
  }

  unsigned char sig[MAX_SIG_SIZE + 1];
  unsigned int  sig_len = 0;

  HMAC(md, key.data(), static_cast<int>(key.size()), reinterpret_cast<unsigned char const *>(data.data()), data.size(), sig,
       &sig_len);

  if (sig_len != expected_len) {
    return {};
  }

  std::string hex;
  hex.reserve(sig_len * 2);
  for (unsigned int i = 0; i < sig_len; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", sig[i]);
    hex.append(buf, 2);
  }
  return hex;
}

/// Split a string_view by delimiter, returning vector of parts.
std::vector<std::string_view>
split(std::string_view const sv_in, char const delim)
{
  std::vector<std::string_view> result;
  std::string_view              sv = sv_in;
  while (!sv.empty()) {
    auto pos = sv.find(delim);
    if (pos == std::string_view::npos) {
      result.push_back(sv);
      break;
    }
    result.push_back(sv.substr(0, pos));
    sv.remove_prefix(pos + 1);
  }
  return result;
}

/// Base64 decode (minimal implementation for path params).
/// Uses OpenSSL EVP_DecodeBlock.
std::string
base64_decode(std::string_view const input)
{
  if (input.empty()) {
    return {};
  }

  // EVP_DecodeBlock needs null-terminated input; output can be up to 3/4 * input_len.
  std::string padded(input);
  // Pad to multiple of 4.
  while (padded.size() % 4 != 0) {
    padded.push_back('=');
  }

  std::vector<unsigned char> out(padded.size());
  int const                  decoded_len =
    EVP_DecodeBlock(out.data(), reinterpret_cast<unsigned char const *>(padded.data()), static_cast<int>(padded.size()));

  if (decoded_len < 0) {
    return {};
  }

  // Remove padding bytes from length.
  // Count '=' at end of padded input.
  int pad_count = 0;
  for (auto it = padded.rbegin(); it != padded.rend() && *it == '='; ++it) {
    pad_count++;
  }
  // Original input padding.
  int orig_pad = 0;
  for (auto it = input.rbegin(); it != input.rend() && *it == '='; ++it) {
    orig_pad++;
  }
  int const len = decoded_len - (pad_count - orig_pad);

  if (len < 0) {
    return {};
  }

  return std::string(reinterpret_cast<char *>(out.data()), len);
}

} // anonymous namespace

void
SigningParams::parse(std::string_view params, char const delim)
{
  while (!params.empty()) {
    // Consume one token up to the next delimiter.
    auto const       sep = params.find(delim);
    std::string_view token;
    if (sep == std::string_view::npos) {
      token  = params;
      params = {};
    } else {
      token  = params.substr(0, sep);
      params = params.substr(sep + 1);
    }

    // A signing key token is exactly: <single-letter>=<value>.
    if (token.size() < 2 || token[1] != '=') {
      continue;
    }

    std::string_view const value = token.substr(2);

    switch (token[0]) {
    case 'C':
      if (client_ip.empty()) {
        client_ip = value;
      }
      break;
    case 'E':
      if (expiration.empty()) {
        expiration = value;
      }
      break;
    case 'A':
      if (algorithm.empty()) {
        algorithm = value;
      }
      break;
    case 'K':
      if (key_index.empty()) {
        key_index = value;
      }
      break;
    case 'P':
      if (parts.empty()) {
        parts = value;
      }
      break;
    case 'S':
      if (signature.empty()) {
        signature = value;
      }
      break;
    default:
      break;
    }
  }
}

std::string
get_app_query_string(std::string_view const query)
{
  if (query.empty()) {
    return {};
  }

  if (static_cast<int>(query.size()) < MAX_QUERY_LEN) {
    // Find first signing parameter.
    std::string_view remaining = query;
    std::string      result;

    while (!remaining.empty()) {
      auto             amp = remaining.find('&');
      std::string_view param;
      if (amp == std::string_view::npos) {
        param     = remaining;
        remaining = {};
      } else {
        param = remaining.substr(0, amp);
        remaining.remove_prefix(amp + 1);
      }

      // Check if this is a signing parameter (starts with A, C, E, K, P, or S followed by =).
      if (!param.empty()) {
        char const first = param[0];
        if ((first == 'A' || first == 'C' || first == 'E' || first == 'K' || first == 'P' || first == 'S') && param.size() >= 2 &&
            param[1] == '=') {
          // This is a signing param — stop here, don't include it or anything after.
          break;
        }
        if (!result.empty()) {
          result.push_back('&');
        }
        result.append(param);
      }
    }
    return result;
  }
  return {};
}

std::string
url_parse_path_params(std::string_view const url, std::string_view const anchor, std::string &new_path, std::string &signed_seg)
{
  new_path.clear();
  signed_seg.clear();

  // Find scheme.
  auto const colon = url.find(':');
  if (colon == std::string_view::npos || url.size() < colon + 3 || url[colon + 1] != '/' || url[colon + 2] != '/') {
    return {};
  }

  std::string_view const scheme = url.substr(0, colon + 3);
  std::string_view const rest   = url.substr(colon + 3);

  // Split path into segments.
  auto segments = split(rest, '/'); // not const: anchor search may truncate a segment
  if (segments.size() < 3) {
    return {};
  }
  if (static_cast<int>(segments.size()) >= MAX_SEGMENTS) {
    return {};
  }

  int              sig_anchor_seg = -1;
  std::string_view sig_anchor_value;

  // Look for anchor in segments.
  if (!anchor.empty()) {
    for (size_t i = 0; i < segments.size(); i++) {
      auto const anchor_pos = segments[i].find(anchor);
      if (anchor_pos != std::string_view::npos) {
        // Find the '=' after anchor.
        auto const eq_pos = segments[i].find('=', anchor_pos);
        if (eq_pos != std::string_view::npos) {
          sig_anchor_value = segments[i].substr(eq_pos + 1);
          // Truncate segment to before the ';' preceding anchor.
          if (0 < anchor_pos && segments[i][anchor_pos - 1] == ';') {
            segments[i] = segments[i].substr(0, anchor_pos - 1);
          }
          sig_anchor_seg = static_cast<int>(i);
        }
        break;
      }
    }
  }

  // Build new_path (skip fqdn segment[0], skip signing segment if no anchor).
  for (size_t i = 1; i < segments.size(); i++) {
    if (sig_anchor_value.empty() && i == segments.size() - 2) {
      // No anchor: signing params in second-to-last segment, skip it.
      continue;
    }
    if (!new_path.empty()) {
      new_path.push_back('/');
    }
    new_path.append(segments[i]);
  }

  // Save signed segment.
  if (!sig_anchor_value.empty()) {
    signed_seg = std::string(sig_anchor_value);
  } else {
    signed_seg = std::string(segments[segments.size() - 2]);
  }

  // Decode the signed segment.
  std::string const decoded = base64_decode(signed_seg);

  // Build new URL with decoded params inserted.
  std::string new_url;
  new_url.append(scheme);

  for (size_t i = 0; i < segments.size(); i++) {
    if (static_cast<int>(i) == sig_anchor_seg && !sig_anchor_value.empty()) {
      new_url.append(segments[i]);
      new_url.append(decoded);
      new_url.push_back('/');
      continue;
    } else if (sig_anchor_value.empty() && i == segments.size() - 2) {
      new_url.append(decoded);
      new_url.push_back('/');
      continue;
    }

    new_url.append(segments[i]);
    if (i < segments.size() - 1) {
      new_url.push_back('/');
    }
  }

  return new_url;
}

UrlSigResult
validate_url(UrlSigConfig const &cfg, std::string_view const url, std::string_view const client_ip, time_t const now)
{
  UrlSigResult result;
  result.status = UrlSigStatus::DENY;

  if (static_cast<int>(url.size()) >= MAX_REQ_LEN - 1) {
    result.reason = "Request URL string too long";
    return result;
  }

  // Check exclusion regex.
  if (!cfg.excl_regex.empty()) {
    // Only check up to first '?' or '#'.
    auto const             end_pos  = url.find_first_of("?#");
    std::string_view const base_url = (end_pos != std::string_view::npos) ? url.substr(0, end_pos) : url;
    if (cfg.excl_regex.exec(base_url)) {
      result.status = UrlSigStatus::ALLOW;
      return result;
    }
  }

  // Determine if query string or path params mode.
  bool             has_path_params = false;
  std::string      parsed_url_storage;
  std::string_view working_url = url;
  std::string      new_path;
  std::string      signed_seg;

  auto const       qmark = url.find('?');
  std::string_view query;

  if (qmark == std::string_view::npos || url.find("E=", qmark) == std::string_view::npos) {
    // No query string with E= found — try path params.
    parsed_url_storage = url_parse_path_params(url, cfg.sig_anchor, new_path, signed_seg);
    if (parsed_url_storage.empty()) {
      result.reason = "Unable to parse/decode URL path parameters";
      return result;
    }

    has_path_params = true;
    working_url     = parsed_url_storage;

    // Find semicolon-delimited params.
    auto const semi = parsed_url_storage.find(';');
    if (semi == std::string_view::npos) {
      result.reason = "Has no signing query string or signing path parameters";
      return result;
    }
    // Include leading ';' so signed_part matches reference behavior.
    query = std::string_view(parsed_url_storage).substr(semi);
  } else {
    query = url.substr(qmark + 1);
  }

  char const delim = has_path_params ? ';' : '&';

  // For path params, skip the leading ';' when extracting parameter values.
  std::string_view const param_query = has_path_params ? query.substr(1) : query;

  // Extract parameters with a single linear scan.
  SigningParams params;
  params.parse(param_query, delim);

  // Client IP check (optional parameter).
  if (!params.client_ip.empty()) {
    if (client_ip != params.client_ip) {
      result.reason = "Client IP doesn't match signature";
      return result;
    }
  }

  // Expiration check.
  if (!cfg.ignore_expiry) {
    if (params.expiration.empty()) {
      result.reason = "Expiration query string not found";
      return result;
    }
    uint64_t expiration = 0;
    auto [ptr, ec] = std::from_chars(params.expiration.data(), params.expiration.data() + params.expiration.size(), expiration);
    if (ec != std::errc{}) {
      result.reason = "Invalid expiration";
      return result;
    }
    time_t const current_time = (now != 0) ? now : time(nullptr);
    if (static_cast<time_t>(expiration) < current_time) {
      result.reason = "Invalid expiration, or expired";
      return result;
    }
  }

  // Algorithm.
  if (params.algorithm.empty()) {
    result.reason = "Algorithm query string not found";
    return result;
  }
  int algorithm = 0;
  {
    auto [ptr, ec] = std::from_chars(params.algorithm.data(), params.algorithm.data() + params.algorithm.size(), algorithm);
    if (ec != std::errc{}) {
      result.reason = "Invalid algorithm";
      return result;
    }
  }

  // Key index.
  if (params.key_index.empty()) {
    result.reason = "KeyIndex query string not found";
    return result;
  }
  int keyindex = -1;
  {
    auto [ptr, ec] = std::from_chars(params.key_index.data(), params.key_index.data() + params.key_index.size(), keyindex);
    if (ec != std::errc{}) {
      result.reason = "Invalid key index";
      return result;
    }
  }
  if (keyindex < 0 || static_cast<size_t>(keyindex) >= cfg.keys.size() || cfg.keys[keyindex].empty()) {
    result.reason = "Invalid key index";
    return result;
  }

  // Parts.
  if (params.parts.empty()) {
    result.reason = "PartsSigned query string not found";
    return result;
  }

  // Signature.
  if (params.signature.empty()) {
    result.reason = "Signature query string not found";
    return result;
  }
  if ((algorithm == USIG_HMAC_SHA1 && params.signature.size() < SHA1_SIG_SIZE) ||
      (algorithm == USIG_HMAC_MD5 && params.signature.size() < MD5_SIG_SIZE)) {
    result.reason = "Signature query string too short";
    return result;
  }

  // Build the signed string from parts.
  // Skip scheme (find "://").
  auto const scheme_end = working_url.find("://");
  if (scheme_end == std::string_view::npos) {
    result.reason = "Invalid URL format";
    return result;
  }
  std::string_view const after_scheme = working_url.substr(scheme_end + 3);

  // Find where query/params start.
  std::string_view path_portion;
  if (has_path_params) {
    auto const semi_pos = after_scheme.find(';');
    path_portion        = (semi_pos != std::string_view::npos) ? after_scheme.substr(0, semi_pos) : after_scheme;
  } else {
    auto const q_pos = after_scheme.find('?');
    path_portion     = (q_pos != std::string_view::npos) ? after_scheme.substr(0, q_pos) : after_scheme;
  }

  // Split path into parts by '/', filtering empty segments (matches strtok_r behavior).
  auto const                    raw_parts = split(path_portion, '/');
  std::vector<std::string_view> url_parts;
  for (auto const &p : raw_parts) {
    if (!p.empty()) {
      url_parts.push_back(p);
    }
  }

  // Build signed_part using parts mask.
  std::string signed_part;
  size_t      j = 0;
  for (size_t i = 0; i < url_parts.size(); i++) {
    char const part_flag = (j < params.parts.size()) ? params.parts[j] : params.parts.back();
    if (part_flag == '1') {
      signed_part.append(url_parts[i]);
      signed_part.push_back('/');
    }
    if (j + 1 < params.parts.size()) {
      j++;
    }
  }

  // Replace trailing '/' with '?' or terminate for path params.
  if (!signed_part.empty() && signed_part.back() == '/') {
    if (has_path_params) {
      signed_part.pop_back();
    } else {
      signed_part.back() = '?';
    }
  }

  // Append query up to and including "S=".
  std::string const sig_search = std::string(SIG_QSTRING) + "=";
  auto const        sig_pos    = query.find(sig_search);
  if (sig_pos == std::string_view::npos) {
    result.reason = "Signature marker not found in query";
    return result;
  }
  signed_part.append(query.substr(0, sig_pos + sig_search.size()));

  // Compute expected signature.
  std::string const expected_sig = compute_hmac(algorithm, cfg.keys[keyindex], signed_part);
  if (expected_sig.empty()) {
    result.reason = "Algorithm not supported or signature computation failed";
    return result;
  }

  // Compare signatures.
  unsigned int const cmp_len = (algorithm == USIG_HMAC_SHA1) ? SHA1_SIG_SIZE * 2 : MD5_SIG_SIZE * 2;
  if (params.signature.size() < cmp_len || expected_sig.size() < cmp_len) {
    result.reason = "Signature check failed";
    return result;
  }

  if (params.signature.substr(0, cmp_len) != std::string_view(expected_sig).substr(0, cmp_len)) {
    result.reason = "Signature check failed";
    return result;
  }

  // Signature valid.
  result.status          = UrlSigStatus::ALLOW;
  result.has_path_params = has_path_params;
  if (has_path_params && !new_path.empty()) {
    result.new_path = std::move(new_path);
  }

  // Extract application query string from original URL.
  auto const orig_qmark = url.find('?');
  if (orig_qmark != std::string_view::npos) {
    result.app_query = get_app_query_string(url.substr(orig_qmark + 1));
  }

  return result;
}
