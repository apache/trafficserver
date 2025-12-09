/** @file
 *
 * XDebug plugin JSON escaping functionality.
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <string_view>

namespace xdebug
{

/**
 * Whether to print the headers for the "probe-full-json" format.
 */
static constexpr bool FULL_JSON = true;

/** Functor to escape characters for JSON or legacy probe output.
 *
 * This class is used to process HTTP header content character by character,
 * handling the state transitions between header name and value, and escaping
 * characters appropriately for the output format.
 *
 */
class EscapeCharForJson
{
public:
  /** Construct an EscapeCharForJson functor.
   *
   * @param full_json If true, produce valid JSON output. If false, produce
   * the legacy probe format which uses single-quoted strings.
   */
  EscapeCharForJson(bool full_json) : _full_json(full_json) {}

  /** Process a single character and return the escaped output.
   *
   * @param c The character to process.
   * @return The escaped string view for this character.
   */
  std::string_view operator()(char const &c);

  /** Get the number of characters to back up after processing all headers.
   *
   * After the last header line, the output will have a trailing separator
   * that needs to be removed. This returns how many characters to back up.
   *
   * @param full_json Whether full JSON format is being used.
   * @return The number of characters to back up.
   */
  static std::size_t backup(bool full_json);

private:
  static std::string_view _after_value(bool full_json);
  static std::string_view _handle_empty_value(bool full_json);

  enum _State { BEFORE_NAME, IN_NAME, BEFORE_VALUE, IN_VALUE };

  _State _state{BEFORE_VALUE};
  bool   _full_json = false;
};

} // namespace xdebug
