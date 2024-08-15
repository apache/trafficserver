/** @file ja3_fingerprint.cc
 *
  JA4 fingerprint calculation.

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

#pragma once

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace JA4
{

constexpr char PORTION_DELIMITER{'_'};

enum class Protocol {
  DTLS = 'd',
  QUIC = 'q',
  TLS  = 't',
};

enum class SNI {
  to_domain = 'd',
  to_IP     = 'i',
};

/**
 * Represents the data sent in a TLS Client Hello needed for JA4 fingerprints.
 */
class TLSClientHelloSummary
{
public:
  using difference_type = std::iterator_traits<std::vector<std::uint16_t>::iterator>::difference_type;

  Protocol      protocol;
  SNI           SNI_type;
  std::uint16_t TLS_version;
  std::string   ALPN;

  std::vector<std::uint16_t> const &get_ciphers() const;
  void                              add_cipher(std::uint16_t cipher);

  std::vector<std::uint16_t> const &get_extensions() const;
  void                              add_extension(std::uint16_t extension);

  /**
   * Get the number of ciphers excluding GREASE values.
   *
   * @return Returns the count of non-GREASE ciphers.
   */
  difference_type get_cipher_count() const;

  /**
   * Get the number of extensions excluding GREASE values.
   *
   * @return Returns the count of non-GREASE extensions.
   */
  difference_type get_extension_count() const;

private:
  std::vector<std::uint16_t> _ciphers;
  std::vector<std::uint16_t> _extensions;
  int                        _extension_count_including_sni_and_alpn{0};
};

/**
 * Calculate the a portion of the JA4 fingerprint for the given client hello.
 *
 * The a portion of the fingerprint encodes the protocol, TLS version, SNI
 * type, number of cipher suites, number of extensions, and first ALPN value.
 *
 * For more information see:
 * https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md.
 *
 * @param TLS_summary The TLS client hello.
 * @return Returns a string containing the a portion of the JA4 fingerprint.
 */
std::string make_JA4_a_raw(TLSClientHelloSummary const &TLS_summary);

/**
 * Calculate the b portion of the JA4 fingerprint for the given client hello.
 *
 * The b portion of the fingerprint is a comma-delimited list of lowercase hex
 * numbers representing the cipher suites in sorted order. GREASE values are
 * ignored.
 *
 * For more information see:
 * https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md.
 *
 * @param TLS_summary The TLS client hello.
 * @return Returns a string containing the b portion of the JA4 fingerprint.
 */
std::string make_JA4_b_raw(TLSClientHelloSummary const &TLS_summary);

/**
 * Calculate the c portion of the JA4 fingerprint for the given client hello.
 *
 * The b portion of the fingerprint is a comma-delimited list of lowercase hex
 * numbers representing the extensions in sorted order. GREASE values and the
 * SNI and ALPN extensions are ignored.
 *
 * For more information see:
 * https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md.
 *
 * @param TLS_summary The TLS client hello.
 * @return Returns a string containing the c portion of the JA4 fingerprint.
 */
std::string make_JA4_c_raw(TLSClientHelloSummary const &TLS_summary);

/**
 * Calculate the JA4 fingerprint for the given TLS client hello.
 *
 * @param TLS_summary The TLS client hello. If there was no ALPN in the
 * Client Hello, TLS_summary.ALPN should either be empty or set to "00".
 * Behavior when the number of digits in TLS_summary.TLS_version is greater
 * than 2, the number of digits in TLS_summary.ALPN is greater than 2
 * (except when TLS_summary.ALPN is empty) is unspecified.
 * @param UnaryOp hasher A hash function. For a specification-compliant
 * JA4 fingerprint, this should be a sha256 hash.
 * @return Returns a string containing the JA4 fingerprint.
 */
template <typename UnaryOp>
std::string
make_JA4_fingerprint(TLSClientHelloSummary const &TLS_summary, UnaryOp hasher)
{
  std::string result;
  result.append(make_JA4_a_raw(TLS_summary));
  result.push_back(JA4::PORTION_DELIMITER);
  result.append(hasher(make_JA4_b_raw(TLS_summary)).substr(0, 12));
  result.push_back(JA4::PORTION_DELIMITER);
  result.append(hasher(make_JA4_c_raw(TLS_summary)).substr(0, 12));
  return result;
}

} // end namespace JA4
