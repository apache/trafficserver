/** @file

  Shared JA3-family data model that is independent of ATS APIs.

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
#include <vector>

namespace ja3
{

/**
 * Parsed TLS ClientHello data shared across JA3-derived encoders.
 *
 * The summary keeps the raw ordered value lists needed by JA3, JAWS v1, and
 * JAWS v2 so each encoder can stay pure and independent of ATS APIs.
 */
struct ClientHelloSummary {
  /** Legacy ClientHello version used by historical JA3-compatible encoders. */
  std::uint16_t legacy_version{0};
  /** Effective TLS version, preferring supported_versions over legacy_version. */
  std::uint16_t effective_tls_version{0};
  /** Raw cipher suite list in ClientHello order, including duplicates. */
  std::vector<std::uint16_t> ciphers;
  /** Raw extension type list in ClientHello order, including duplicates. */
  std::vector<std::uint16_t> extensions;
  /** Raw supported_groups list in ClientHello order, including duplicates. */
  std::vector<std::uint16_t> curves;
  /** Raw ec_point_formats values in ClientHello order. */
  std::vector<std::uint8_t> point_formats;
  /** Whether the cipher list contained at least one GREASE value. */
  bool ciphers_have_grease{false};
  /** Whether the extension list contained at least one GREASE value. */
  bool extensions_have_grease{false};
  /** Whether the supported_groups list contained at least one GREASE value. */
  bool curves_have_grease{false};
};

/**
 * Check whether a 16-bit TLS value matches the GREASE pattern.
 *
 * JA3-family encoders use this helper to keep GREASE from polluting anchored
 * bitfields while still tracking its presence when needed.
 *
 * @param[in] value TLS value to classify.
 * @return @c true when the value matches the RFC 8701 GREASE pattern,
 * otherwise @c false.
 */
inline bool
is_GREASE(std::uint16_t value)
{
  return (value & 0x0f0f) == 0x0a0a;
}

} // namespace ja3
