/** @file

  Private helper declarations for parsing log field fallback expressions.

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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "proxy/logging/LogField.h"

namespace LogFieldFallback
{
/** Parsed representation of a log field fallback chain. */
struct ParseResult {
  /// Header candidates evaluated from left to right.
  std::vector<LogField::HeaderField> header_fields;
  /// Optional final plain field symbol used when no header candidate is present.
  std::optional<std::string> fallback_symbol;
  /// Optional quoted default literal used when no header candidate is present.
  std::optional<std::string> fallback_default;
};

/** Determine whether a symbol uses log field fallback syntax.
 *
 * @param[in] symbol Log field symbol text from a custom format.
 * @return @c true if @a symbol contains an unquoted fallback separator.
 */
bool has_fallback(std::string_view symbol);

/** Parse a log field fallback symbol into header candidates and an optional
 * final plain symbol or default literal.
 *
 * @param[in] symbol Log field symbol text from a custom format.
 * @param[out] error Receives a human-readable parse error on failure.
 * @return Parsed fallback state on success, @c std::nullopt on failure.
 */
std::optional<ParseResult> parse(std::string_view symbol, std::string &error);
} // namespace LogFieldFallback
