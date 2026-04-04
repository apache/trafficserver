/** @file

  Frozen JAWS v1 encoder for stripped JA3 raw strings.

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

#include "ja3/ja3_model.h"

#include <string>
#include <string_view>

namespace ja3::jaws_v1
{
/**
 * Encode a GREASE-stripped JA3 raw string as a frozen JAWS v1 score.
 *
 * This helper preserves the historical v1 anchor ordering and count semantics
 * so existing downstream consumers continue to see stable output.
 *
 * @param[in] ja3_string GREASE-stripped JA3 raw string to score.
 * @return JAWS v1 fingerprint text derived from the JA3 raw input.
 */
std::string score(std::string_view ja3_string);

/**
 * Encode a parsed TLS ClientHello summary as a frozen JAWS v1 fingerprint.
 *
 * This helper reuses the shared JA3 raw encoder so the summary-based path
 * remains byte-for-byte compatible with historical JAWS v1 output.
 *
 * @param[in] summary Parsed TLS ClientHello summary to encode.
 * @return JAWS v1 fingerprint text derived from the summary.
 */
std::string fingerprint(ClientHelloSummary const &summary);
} // namespace ja3::jaws_v1
