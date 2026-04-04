/** @file

  Shared TLS ClientHello summary for JA3-family fingerprints.

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

#include "plugin.h"
#include "ja3_summary.h"

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace
{
using ClientHelloSummaryCache = std::unordered_map<TSVConn, std::unique_ptr<ja3::ClientHelloSummary>>;

constexpr unsigned int  EXT_SUPPORTED_GROUPS{0x0a};
constexpr unsigned int  EXT_EC_POINT_FORMATS{0x0b};
constexpr unsigned int  EXT_SUPPORTED_VERSIONS{0x2b};
std::mutex              summary_cache_mutex;
ClientHelloSummaryCache summary_cache;

constexpr std::uint16_t
make_word(unsigned char highbyte, unsigned char lowbyte)
{
  return (static_cast<std::uint16_t>(highbyte) << 8) | lowbyte;
}

void
add_ciphers(ja3::ClientHelloSummary &summary, TSClientHello ch)
{
  auto const *buf    = ch.get_cipher_suites();
  auto const  buflen = ch.get_cipher_suites_len();

  for (std::size_t i{0}; i + 1 < buflen; i += 2) {
    std::uint16_t const cipher{make_word(buf[i], buf[i + 1])};
    summary.ciphers_have_grease |= ja3::is_GREASE(cipher);
    summary.ciphers.push_back(cipher);
  }
}

void
add_extensions(ja3::ClientHelloSummary &summary, TSClientHello ch)
{
  for (auto ext_type : ch.get_extension_types()) {
    summary.extensions_have_grease |= ja3::is_GREASE(ext_type);
    summary.extensions.push_back(ext_type);
  }
}

void
add_supported_groups(ja3::ClientHelloSummary &summary, TSClientHello ch)
{
  unsigned char const *buf{};
  std::size_t          buflen{};
  if (TS_SUCCESS != TSClientHelloExtensionGet(ch, EXT_SUPPORTED_GROUPS, &buf, &buflen) || buflen < 2) {
    return;
  }

  std::size_t groups_len = make_word(buf[0], buf[1]);
  if (buflen < groups_len + 2) {
    Dbg(dbg_ctl, "Malformed supported_groups extension (truncated vector).");
    groups_len = buflen - 2;
  }

  for (std::size_t i{2}; (i + 1) < buflen && (i + 1) < (groups_len + 2); i += 2) {
    std::uint16_t const group{make_word(buf[i], buf[i + 1])};
    summary.curves_have_grease |= ja3::is_GREASE(group);
    summary.curves.push_back(group);
  }
}

void
add_ec_point_formats(ja3::ClientHelloSummary &summary, TSClientHello ch)
{
  unsigned char const *buf{};
  std::size_t          buflen{};
  if (TS_SUCCESS != TSClientHelloExtensionGet(ch, EXT_EC_POINT_FORMATS, &buf, &buflen) || buflen < 1) {
    return;
  }

  std::size_t point_formats_len = buf[0];
  if (buflen < point_formats_len + 1) {
    Dbg(dbg_ctl, "Malformed ec_point_formats extension (truncated vector).");
    point_formats_len = buflen - 1;
  }

  for (std::size_t i{1}; i < buflen && i < (point_formats_len + 1); ++i) {
    summary.point_formats.push_back(buf[i]);
  }
}

std::uint16_t
get_effective_tls_version(TSClientHello ch)
{
  unsigned char const *buf{};
  std::size_t          buflen{};
  if (TS_SUCCESS != TSClientHelloExtensionGet(ch, EXT_SUPPORTED_VERSIONS, &buf, &buflen) || buflen < 1) {
    return ch.get_version();
  }

  std::size_t versions_len = buf[0];
  if (buflen < versions_len + 1) {
    Dbg(dbg_ctl, "Malformed supported_versions extension (truncated vector)... using legacy version.");
    return ch.get_version();
  }

  std::uint16_t max_version{0};
  for (std::size_t i{1}; (i + 1) < buflen && (i + 1) < (versions_len + 1); i += 2) {
    std::uint16_t const version{make_word(buf[i], buf[i + 1])};
    if (!ja3::is_GREASE(version) && version > max_version) {
      max_version = version;
    }
  }

  return max_version == 0 ? ch.get_version() : max_version;
}

ja3::ClientHelloSummary
build_client_hello_summary(TSClientHello ch)
{
  ja3::ClientHelloSummary summary{};
  summary.legacy_version        = ch.get_version();
  summary.effective_tls_version = get_effective_tls_version(ch);
  add_ciphers(summary, ch);
  add_extensions(summary, ch);
  add_supported_groups(summary, ch);
  add_ec_point_formats(summary, ch);
  return summary;
}

} // end anonymous namespace

ja3::ClientHelloSummary const *
ja3::get_or_create_client_hello_summary(TSVConn vconn)
{
  {
    std::lock_guard<std::mutex> lock(summary_cache_mutex);
    if (auto const it = summary_cache.find(vconn); it != summary_cache.end()) {
      return it->second.get();
    }
  }

  TSClientHello ch = TSVConnClientHelloGet(vconn);
  if (!ch) {
    Dbg(dbg_ctl, "Could not get TSClientHello object.");
    return nullptr;
  }

  auto  summary = std::make_unique<ClientHelloSummary>(build_client_hello_summary(ch));
  auto *result  = summary.get();

  std::lock_guard<std::mutex> lock(summary_cache_mutex);
  if (auto const [it, inserted] = summary_cache.emplace(vconn, std::move(summary)); inserted) {
    return result;
  } else {
    return it->second.get();
  }
}

void
ja3::clear_cached_client_hello_summary(TSVConn vconn)
{
  std::lock_guard<std::mutex> lock(summary_cache_mutex);
  summary_cache.erase(vconn);
}
