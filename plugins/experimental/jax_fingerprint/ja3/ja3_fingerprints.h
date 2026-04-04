/** @file

  Pure encoders for JA3 raw and JA3 hash fingerprints.

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

#include "ja3_model.h"

#include <string>

namespace ja3
{

/**
 * Build the JA3 raw string from a parsed TLS ClientHello summary.
 *
 * This encoder preserves the historical JA3 field ordering and can optionally
 * keep GREASE values in the output for debugging and staged rollout use cases.
 *
 * @param[in] summary Parsed TLS ClientHello summary to encode.
 * @param[in] preserve_grease When @c true, include GREASE values in the
 * serialized cipher, extension, and curve lists.
 * @return JA3 raw text in the conventional
 * ``version,ciphers,extensions,curves,point_formats`` layout.
 */
std::string make_ja3_raw(ClientHelloSummary const &summary, bool preserve_grease);

/**
 * Build the historical JA3 MD5 hash from a parsed TLS ClientHello summary.
 *
 * This helper first generates the GREASE-stripped JA3 raw string so the hash
 * remains compatible with existing JA3 deployments.
 *
 * @param[in] summary Parsed TLS ClientHello summary to encode.
 * @return Lowercase hexadecimal MD5 digest of the stripped JA3 raw string.
 */
std::string make_ja3_hash(ClientHelloSummary const &summary);

} // namespace ja3
