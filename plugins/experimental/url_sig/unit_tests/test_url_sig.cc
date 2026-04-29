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

#include <ctime>
#include <sstream>
#include <string>

#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <catch2/catch_test_macros.hpp>

// Helper to generate HMAC-SHA1 hex signature for test URLs.
static std::string
hmac_sha1_hex(std::string const &key, std::string const &data)
{
  unsigned char sig[20];
  unsigned int  sig_len = 0;

  HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()), reinterpret_cast<unsigned char const *>(data.data()), data.size(), sig,
       &sig_len);

  std::string hex;
  for (unsigned int i = 0; i < sig_len; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", sig[i]);
    hex.append(buf, 2);
  }
  return hex;
}

static std::string
hmac_md5_hex(std::string const &key, std::string const &data)
{
  unsigned char sig[16];
  unsigned int  sig_len = 0;

  HMAC(EVP_md5(), key.data(), static_cast<int>(key.size()), reinterpret_cast<unsigned char const *>(data.data()), data.size(), sig,
       &sig_len);

  std::string hex;
  for (unsigned int i = 0; i < sig_len; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", sig[i]);
    hex.append(buf, 2);
  }
  return hex;
}

// ============================================================
// Config Parsing Tests
// ============================================================

TEST_CASE("Config parsing - valid config", "[config]")
{
  std::istringstream input("key0 = secretkey0\n"
                           "key3 = secretkey3\n"
                           "error_url = 403\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg != nullptr);
  CHECK(error.empty());
  CHECK(cfg->keys[0] == "secretkey0");
  CHECK(cfg->keys[3] == "secretkey3");
  CHECK(cfg->keys[1].empty());
  CHECK(cfg->err_status == UrlSigErrStatus::FORBIDDEN);
  CHECK(cfg->err_url.empty());
}

TEST_CASE("Config parsing - 302 redirect", "[config]")
{
  std::istringstream input("key0 = mykey\n"
                           "error_url = 302 http://example.com/error\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg != nullptr);
  CHECK(cfg->err_status == UrlSigErrStatus::MOVED_TEMPORARILY);
  CHECK(cfg->err_url == "http://example.com/error");
}

TEST_CASE("Config parsing - 302 without URL fails", "[config]")
{
  std::istringstream input("key0 = mykey\n"
                           "error_url = 302\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg == nullptr);
  CHECK(error.find("302") != std::string::npos);
}

TEST_CASE("Config parsing - key too long", "[config]")
{
  std::string const  long_key(MAX_KEY_LEN + 10, 'x');
  std::istringstream input("key0 = " + long_key + "\nerror_url = 403\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg == nullptr);
  CHECK(error.find("Maximum key length") != std::string::npos);
}

TEST_CASE("Config parsing - large key index accepted", "[config]")
{
  std::istringstream input("key99 = somekey\n"
                           "error_url = 403\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->keys.size() > 99);
  CHECK(cfg->keys[99] == "somekey");
}

TEST_CASE("Config parsing - ignore_expiry", "[config]")
{
  std::istringstream input("key0 = mykey\n"
                           "ignore_expiry = true\n"
                           "error_url = 403\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg != nullptr);
  CHECK(cfg->ignore_expiry == true);
}

TEST_CASE("Config parsing - sig_anchor", "[config]")
{
  std::istringstream input("key0 = mykey\n"
                           "sig_anchor = urlsig\n"
                           "error_url = 403\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg != nullptr);
  CHECK(cfg->sig_anchor == "urlsig");
}

TEST_CASE("Config parsing - url_type pristine", "[config]")
{
  std::istringstream input("key0 = mykey\n"
                           "url_type = pristine\n"
                           "error_url = 403\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg != nullptr);
  CHECK(cfg->pristine_url_flag == true);
}

TEST_CASE("Config parsing - comments and blank lines", "[config]")
{
  std::istringstream input("# This is a comment\n"
                           "\n"
                           "key0 = mykey\n"
                           "# another comment\n"
                           "error_url = 403\n");

  std::string error;
  auto const  cfg = load_config(input, error);

  REQUIRE(cfg != nullptr);
  CHECK(cfg->keys[0] == "mykey");
}

// ============================================================
// getAppQueryString Tests
// ============================================================

TEST_CASE("get_app_query_string - strips signing params", "[query]")
{
  CHECK(get_app_query_string("foo=bar&baz=1&E=123&A=1&K=0&P=1&S=abc") == "foo=bar&baz=1");
}

TEST_CASE("get_app_query_string - no app params", "[query]")
{
  CHECK(get_app_query_string("E=123&A=1&K=0&P=1&S=abc") == "");
}

TEST_CASE("get_app_query_string - only app params (no signing)", "[query]")
{
  CHECK(get_app_query_string("foo=bar&baz=1") == "foo=bar&baz=1");
}

TEST_CASE("get_app_query_string - empty", "[query]")
{
  CHECK(get_app_query_string("") == "");
}

// ============================================================
// URL Signature Validation Tests
// ============================================================

TEST_CASE("validate_url - valid HMAC-SHA1 signature", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.resize(4);
  cfg.keys[3] = "DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7";

  // Build signed URL: parts=1 means use fqdn + all path parts.
  // URL: http://test-remap.domain.com/
  // Path parts after split: ["test-remap.domain.com"]
  // Signed string: "test-remap.domain.com?E=...&A=1&K=3&P=1&S="
  // (trailing / from part replaced with ?)
  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  std::string const query_no_sig = "E=" + exp_str + "&A=1&K=3&P=1&S=";
  std::string const signed_part  = "test-remap.domain.com?" + query_no_sig;
  std::string const sig          = hmac_sha1_hex("DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7", signed_part);

  std::string const url = "http://test-remap.domain.com/?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::ALLOW);
  CHECK(result.reason.empty());
}

TEST_CASE("validate_url - valid HMAC-MD5 signature", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");

  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  std::string const query_no_sig = "E=" + exp_str + "&A=2&K=0&P=1&S=";
  std::string const signed_part  = "example.com/path/file.ts?" + query_no_sig;
  std::string const sig          = hmac_md5_hex("testkey", signed_part);

  std::string const url = "http://example.com/path/file.ts?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::ALLOW);
}

TEST_CASE("validate_url - expired signature", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");

  std::string const query_no_sig = "E=1000000000&A=1&K=0&P=1&S=";
  std::string const signed_part  = "example.com?" + query_no_sig;
  std::string const sig          = hmac_sha1_hex("testkey", signed_part);

  std::string const url = "http://example.com/?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::DENY);
  CHECK(result.reason.find("expir") != std::string::npos);
}

TEST_CASE("validate_url - ignore_expiry bypasses expiration", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");
  cfg.ignore_expiry = true;

  std::string const query_no_sig = "E=1000000000&A=1&K=0&P=1&S=";
  std::string const signed_part  = "example.com?" + query_no_sig;
  std::string const sig          = hmac_sha1_hex("testkey", signed_part);

  std::string const url = "http://example.com/?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::ALLOW);
}

TEST_CASE("validate_url - wrong key", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("correctkey");

  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  std::string const query_no_sig = "E=" + exp_str + "&A=1&K=0&P=1&S=";
  std::string const signed_part  = "example.com?" + query_no_sig;
  std::string const sig          = hmac_sha1_hex("wrongkey", signed_part);

  std::string const url = "http://example.com/?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::DENY);
  CHECK(result.reason.find("Signature check failed") != std::string::npos);
}

TEST_CASE("validate_url - invalid key index", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");

  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  std::string const url = "http://example.com/?E=" + exp_str + "&A=1&K=5&P=1&S=abcdef0123456789abcdef0123456789abcdef01";

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::DENY);
  CHECK(result.reason.find("key index") != std::string::npos);
}

TEST_CASE("validate_url - missing algorithm", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");

  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  std::string const url = "http://example.com/?E=" + exp_str + "&K=0&P=1&S=abc";

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::DENY);
  CHECK(result.reason.find("Algorithm") != std::string::npos);
}

TEST_CASE("validate_url - client IP match", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");

  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  std::string const query_no_sig = "C=10.0.0.1&E=" + exp_str + "&A=1&K=0&P=1&S=";
  std::string const signed_part  = "example.com?" + query_no_sig;
  std::string const sig          = hmac_sha1_hex("testkey", signed_part);

  std::string const url = "http://example.com/?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "10.0.0.1");

  CHECK(result.status == UrlSigStatus::ALLOW);
}

TEST_CASE("validate_url - client IP mismatch", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");

  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  std::string const query_no_sig = "C=10.0.0.1&E=" + exp_str + "&A=1&K=0&P=1&S=";
  std::string const signed_part  = "example.com?" + query_no_sig;
  std::string const sig          = hmac_sha1_hex("testkey", signed_part);

  std::string const url = "http://example.com/?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "192.168.1.1");

  CHECK(result.status == UrlSigStatus::DENY);
  CHECK(result.reason.find("Client IP") != std::string::npos);
}

TEST_CASE("validate_url - IPv6 client IP match", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");

  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  std::string const query_no_sig = "C=::1&E=" + exp_str + "&A=1&K=0&P=1&S=";
  std::string const signed_part  = "example.com?" + query_no_sig;
  std::string const sig          = hmac_sha1_hex("testkey", signed_part);

  std::string const url = "http://example.com/?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "::1");

  CHECK(result.status == UrlSigStatus::ALLOW);
}

TEST_CASE("validate_url - excl_regex allows without signature", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status       = UrlSigErrStatus::FORBIDDEN;
  cfg.excl_regex_match = [](std::string_view url) -> bool { return url.find(".m3u8") != std::string_view::npos; };

  std::string const url = "http://example.com/path/manifest.m3u8";

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::ALLOW);
}

TEST_CASE("validate_url - excl_regex does not match", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status       = UrlSigErrStatus::FORBIDDEN;
  cfg.excl_regex_match = [](std::string_view url) -> bool { return url.find(".m3u8") != std::string_view::npos; };

  std::string const url = "http://example.com/path/video.ts?E=123&A=1&K=0&P=1&S=abc";

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::DENY);
}

TEST_CASE("validate_url - parts selection (partial)", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;
  cfg.keys.push_back("testkey");

  time_t const      future  = time(nullptr) + 3600;
  std::string const exp_str = std::to_string(future);

  // Parts=0110 means skip fqdn (part0), use part1 and part2, skip rest.
  // URL: http://cdn.example.com/content/video/file.ts
  // Parts: cdn.example.com=0, content=1, video=1, file.ts=0(last char repeated)
  // Signed: content/video/?E=...
  std::string const query_no_sig = "E=" + exp_str + "&A=1&K=0&P=0110&S=";
  std::string const signed_part  = "content/video?" + query_no_sig;
  std::string const sig          = hmac_sha1_hex("testkey", signed_part);

  std::string const url = "http://cdn.example.com/content/video/file.ts?" + query_no_sig + sig;

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::ALLOW);
}

TEST_CASE("validate_url - URL too long", "[verify]")
{
  UrlSigConfig cfg;
  cfg.err_status = UrlSigErrStatus::FORBIDDEN;

  std::string const url = "http://example.com/" + std::string(MAX_REQ_LEN, 'x');

  auto const result = validate_url(cfg, url, "");

  CHECK(result.status == UrlSigStatus::DENY);
  CHECK(result.reason.find("too long") != std::string::npos);
}

// ============================================================
// urlParse (path params) Tests
// ============================================================

TEST_CASE("url_parse_path_params - basic without anchor", "[parse]")
{
  // Simulate a URL with base64-encoded signing params in second-to-last segment.
  // For simplicity, use a simple base64 string.
  // "E=9999999999;A=1;K=0;P=1;S=abc" base64 = "RT05OTk5OTk5OTk5O0E9MTtLPTA7UD0xO1M9YWJj"
  std::string const url = "http://example.com/path/RT05OTk5OTk5OTk5O0E9MTtLPTA7UD0xO1M9YWJj/file.ts";

  std::string new_path;
  std::string signed_seg;
  auto const  result = url_parse_path_params(url, "", new_path, signed_seg);

  CHECK(!result.empty());
  CHECK(!new_path.empty());
  CHECK(!signed_seg.empty());
}

TEST_CASE("url_parse_path_params - too few segments", "[parse]")
{
  std::string const url = "http://example.com/file.ts";

  std::string new_path;
  std::string signed_seg;
  auto const  result = url_parse_path_params(url, "", new_path, signed_seg);

  CHECK(result.empty());
}

TEST_CASE("url_parse_path_params - invalid scheme", "[parse]")
{
  std::string const url = "notaurl";

  std::string new_path;
  std::string signed_seg;
  auto const  result = url_parse_path_params(url, "", new_path, signed_seg);

  CHECK(result.empty());
}
