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
#include <cstddef>
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

std::vector<std::uint16_t> const &
JA4::TLSClientHelloSummary::get_signature_algorithms() const
{
  return this->_signature_algorithms;
}

void
JA4::TLSClientHelloSummary::set_signature_algorithms(unsigned char const *body, std::size_t body_len)
{
  this->_signature_algorithms.clear();
  if (body_len < 2) {
    return;
  }

  std::size_t end{std::min(body_len, 2 + ((static_cast<std::size_t>(body[0]) << 8) | body[1]))};
  for (std::size_t i{2}; i + 1 < end; i += 2) {
    std::uint16_t alg{static_cast<std::uint16_t>((body[i] << 8) | body[i + 1])};
    if (!is_GREASE(alg)) {
      this->_signature_algorithms.push_back(alg);
    }
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
