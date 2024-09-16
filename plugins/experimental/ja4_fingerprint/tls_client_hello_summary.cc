/** @file ja3_fingerprint.cc
 *
  TLSClientHelloSummary data structure for JA4 fingerprint calculation.

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

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace
{

constexpr std::array<std::uint16_t, 16> GREASE_values{0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a,
                                                      0x8a8a, 0x9a9a, 0xaaaa, 0xbaba, 0xcaca, 0xdada, 0xeaea, 0xfafa};
constexpr std::uint16_t                 extension_SNI{0x0};
constexpr std::uint16_t                 extension_ALPN{0x10};

} // end anonymous namespace

static bool is_ignored_non_GREASE_extension(std::uint16_t extension);

std::vector<std::uint16_t> const &
JA4::TLSClientHelloSummary::get_ciphers() const
{
  return this->_ciphers;
}

void
JA4::TLSClientHelloSummary::add_cipher(std::uint16_t cipher)
{
  if (is_GREASE(cipher)) {
    return;
  }

  this->_ciphers.push_back(cipher);
}

std::vector<std::uint16_t> const &
JA4::TLSClientHelloSummary::get_extensions() const
{
  return this->_extensions;
}

void
JA4::TLSClientHelloSummary::add_extension(std::uint16_t extension)
{
  if (is_GREASE(extension)) {
    return;
  }

  if (extension_SNI == extension) {
    this->_SNI_type = SNI::to_domain;
  }

  ++this->_extension_count_including_sni_and_alpn;
  if (!is_ignored_non_GREASE_extension(extension)) {
    this->_extensions.push_back(extension);
  }
}

JA4::TLSClientHelloSummary::difference_type
JA4::TLSClientHelloSummary::get_cipher_count() const
{
  return this->_ciphers.size();
}

JA4::TLSClientHelloSummary::difference_type
JA4::TLSClientHelloSummary::get_extension_count() const
{
  return this->_extension_count_including_sni_and_alpn;
}

bool
is_ignored_non_GREASE_extension(std::uint16_t extension)
{
  return (extension_SNI == extension) || (extension_ALPN == extension);
}

JA4::SNI
JA4::TLSClientHelloSummary::get_SNI_type() const
{
  return this->_SNI_type;
}

bool
JA4::is_GREASE(std::uint16_t value)
{
  return std::binary_search(GREASE_values.begin(), GREASE_values.end(), value);
}
