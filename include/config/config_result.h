/** @file

  Generic configuration parsing result type.

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

#include "swoc/Errata.h"

namespace config
{

/**
 * Result of a configuration parse operation.
 *
 * This template is the standard return type for all configuration parsers in
 * the config library. It bundles a parsed configuration value with an errata
 * that may contain warnings or errors encountered during parsing.
 *
 * Design rationale:
 * - Parsers can return partial results (value populated) even when warnings
 *   are present, allowing callers to decide how to handle degraded configs.
 * - The ok() method checks whether parsing succeeded without errors, but
 *   callers should also inspect the errata for warnings.
 * - This type is reused across all configuration file types (ssl_multicert,
 *   etc.) to provide a consistent API.
 *
 * Example usage:
 * @code
 *   config::SSLMultiCertParser parser;
 *   auto result = parser.parse("/path/to/ssl_multicert.yaml");
 *   if (!result.ok()) {
 *       // Handle error
 *       return;
 *   }
 *   for (const auto& entry : result.value) {
 *       // Use parsed entries
 *   }
 * @endcode
 *
 * @tparam T The configuration type (e.g., SSLMultiCertConfig).
 */
template <typename T> struct ConfigResult {
  T            value;  ///< The parsed configuration value.
  swoc::Errata errata; ///< Errors or warnings from parsing.

  /**
   * Check if parsing succeeded without errors.
   *
   * @return true if no errors occurred, false otherwise.
   */
  bool
  ok() const
  {
    return errata.is_ok();
  }
};

} // namespace config
