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
#include <memory>
#include <sstream>

std::unique_ptr<UrlSigConfig>
load_config(std::istream &input, std::string &error)
{
  auto cfg     = std::make_unique<UrlSigConfig>();
  int  line_no = 0; // incremented per line

  std::string line;
  while (std::getline(input, line)) {
    line_no++;

    // Skip empty lines and comments.
    if (line.empty() || line[0] == '#') {
      continue;
    }

    auto const eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      // Not a fatal error, just skip like original.
      continue;
    }

    std::string_view key_part(line.data(), eq_pos);
    std::string_view value_part(line.data() + eq_pos + 1, line.size() - eq_pos - 1);

    // Trim trailing whitespace from key.
    while (!key_part.empty() && (key_part.back() == ' ' || key_part.back() == '\t')) {
      key_part.remove_suffix(1);
    }
    // Trim leading whitespace from value.
    while (!value_part.empty() && (value_part.front() == ' ' || value_part.front() == '\t')) {
      value_part.remove_prefix(1);
    }
    // Trim trailing whitespace/newline from value.
    while (!value_part.empty() &&
           (value_part.back() == ' ' || value_part.back() == '\t' || value_part.back() == '\n' || value_part.back() == '\r')) {
      value_part.remove_suffix(1);
    }

    if (key_part.starts_with("key")) {
      std::string_view const index_str = key_part.substr(3);
      int                    keynum    = -1;

      if (index_str == "0") {
        keynum = 0;
      } else {
        auto [ptr, ec] = std::from_chars(index_str.data(), index_str.data() + index_str.size(), keynum);
        if (ec != std::errc{} || keynum == 0) {
          keynum = -1;
        }
      }

      if (keynum < 0) {
        error = "Key number is NaN at line " + std::to_string(line_no);
        return nullptr;
      }

      if (static_cast<int>(value_part.size()) >= MAX_KEY_LEN) {
        error = "Maximum key length (" + std::to_string(MAX_KEY_LEN - 1) + ") exceeded on line " + std::to_string(line_no);
        return nullptr;
      }

      if (keynum >= static_cast<int>(cfg->keys.size())) {
        cfg->keys.resize(keynum + 1);
      }
      cfg->keys[keynum] = std::string(value_part);

    } else if (key_part == "error_url") {
      // Format: error_url = <status_code> <url>
      // e.g. "error_url = 403" or "error_url = 302 http://example.com/error"
      int status_code      = 0;
      auto const [ptr, ec] = std::from_chars(value_part.data(), value_part.data() + value_part.size(), status_code);
      if (ec != std::errc{}) {
        continue;
      }

      if (status_code == 302) {
        cfg->err_status = UrlSigErrStatus::MOVED_TEMPORARILY;
        // Skip past status code and whitespace to get URL.
        std::string_view remainder(ptr, static_cast<size_t>(value_part.data() + value_part.size() - ptr));
        while (!remainder.empty() && (remainder.front() == ' ' || remainder.front() == '\t')) {
          remainder.remove_prefix(1);
        }
        cfg->err_url = std::string(remainder);
      } else {
        cfg->err_status = UrlSigErrStatus::FORBIDDEN;
        cfg->err_url.clear();
      }

    } else if (key_part == "sig_anchor") {
      cfg->sig_anchor = std::string(value_part);

    } else if (key_part == "excl_regex") {
      // excl_regex is handled by the caller setting excl_regex_match after load.
      // Store raw pattern in sig_anchor-like fashion? No — we just note it here.
      // The ATS adapter will compile the regex and set the callback.
      // Store the pattern string temporarily in a way the adapter can retrieve.
      // Actually, let's just skip it here — the adapter reads the file itself for regex.
      // But wait, we want core to be self-contained for config...
      // Compromise: we note the pattern but don't compile it (no regex dep in core).
      // The caller can re-read or we store it.
      // For now: store nothing, adapter handles regex separately by reading config.
      // This matches original behavior where excl_regex was compiled with ATS Regex.

    } else if (key_part == "ignore_expiry") {
      cfg->ignore_expiry = (value_part == "true");

    } else if (key_part == "url_type") {
      cfg->pristine_url_flag = (value_part == "pristine");
    }
    // Unknown keys silently ignored (matches original for forward compat).
  }

  // Validate config.
  switch (cfg->err_status) {
  case UrlSigErrStatus::MOVED_TEMPORARILY:
    if (cfg->err_url.empty()) {
      error = "Invalid config, err_status == 302, but err_url is empty";
      return nullptr;
    }
    break;
  case UrlSigErrStatus::FORBIDDEN:
    if (!cfg->err_url.empty()) {
      error = "Invalid config, err_status == 403, but err_url is not empty";
      return nullptr;
    }
    break;
  }

  return cfg;
}
