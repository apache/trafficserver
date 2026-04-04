/** @file

  JAWS v2 encoder for parsed TLS ClientHello summaries.

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

namespace ja3::jaws_v2
{
/**
 * Encode a parsed TLS ClientHello summary as a JAWS v2 fingerprint.
 *
 * This encoder uses the effective TLS version, the v2 anchor arrays, the EC
 * point format section, and optional per-section GREASE tagging.
 *
 * @param[in] summary Parsed TLS ClientHello summary to encode.
 * @param[in] track_grease When @c true, append the ``g`` suffix for sections
 * that observed GREASE values.
 * @return JAWS v2 fingerprint text with the ``j2:`` prefix.
 */
std::string fingerprint(ClientHelloSummary const &summary, bool track_grease = true);
} // namespace ja3::jaws_v2
