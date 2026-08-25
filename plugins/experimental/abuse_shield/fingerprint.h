/** @file

  Extensible TLS ClientHello fingerprint provider interface.

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

#include "swoc/MemSpan.h"
#include "ts/ts.h"

namespace abuse_shield
{

using FingerprintResults = std::unordered_map<std::string, std::string>;

/** A ClientHello fingerprint implementation compiled into Abuse Shield.
 *
 * Providers live in @c fingerprints/<name>/ and export a @c method instance.
 * Add the directory name to @c ABUSE_SHIELD_FINGERPRINT_METHODS to compile a
 * provider into the plugin.
 */
struct FingerprintMethod {
  using Compute      = std::string (*)(TSClientHello);
  using Canonicalize = std::optional<std::string> (*)(std::string_view);

  std::string_view name;         ///< Stable method name used as the YAML key.
  Compute          compute;      ///< Compute the fingerprint from a ClientHello.
  Canonicalize     canonicalize; ///< Validate and canonicalize a configured fingerprint.
};

/** Return all ClientHello fingerprint providers compiled into Abuse Shield. */
swoc::MemSpan<const FingerprintMethod *const> fingerprint_methods();

/** Find a compiled fingerprint provider by case-insensitive name. */
const FingerprintMethod *find_fingerprint_method(std::string_view name);

} // namespace abuse_shield
