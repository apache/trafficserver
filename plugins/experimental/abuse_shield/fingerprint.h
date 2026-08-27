/** @file

  TLS ClientHello fingerprint configuration helpers.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements. See the NOTICE file distributed with this work for additional information regarding
  copyright ownership. Licensed under the Apache License, Version 2.0 (the "License"); you may not
  use this file except in compliance with the License. You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace abuse_shield
{

using FingerprintResults = std::unordered_map<std::string, std::string>;

/** A validated fingerprint method/value pair from configuration. */
struct ConfiguredFingerprint {
  std::string method;
  std::string value;
};

/** Validate and canonicalize a configured fingerprint.
 *
 * Public JAx methods receive their standard spelling and value validation.
 * Other method names are retained exactly so private JAx builds can export
 * implementation-specific fingerprints without changes to Abuse Shield.
 */
std::optional<ConfiguredFingerprint> canonicalize_fingerprint(std::string_view method, std::string_view value);

} // namespace abuse_shield
