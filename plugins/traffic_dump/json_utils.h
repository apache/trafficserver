/** @file

  JSON formatting functions.

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

#include <string>
#include <string_view>

namespace traffic_dump
{
/** Create the name and value as an escaped JSON map entry.
 *
 * @param[in] name The key name for the map entry.
 *
 * @param[in] value The value to write.
 *
 * @return The JSON map string.
 */
std::string json_entry(std::string_view name, std::string_view value);

/** Create the name and value as an escaped JSON map entry.
 *
 * @param[in] name The key name for the map entry.
 *
 * @param[in] value The buffer for the value to write.
 *
 * @param[in] size The size of the value buffer.
 *
 * @return The JSON map string.
 */
std::string json_entry(std::string_view name, char const *value, int64_t size);

/** Create the name and value as an escaped JSON array entry.
 *
 * @param[in] name The key name for the map entry.
 *
 * @param[in] value The value to write for the JSON map entry.
 *
 * @return The JSON array string.
 */
std::string json_entry_array(std::string_view name, std::string_view value);

/** Escape characters in a string as needed and return the resultant escaped string.
 *
 * @param[in] s The characters that need to be escaped.
 */
std::string escape_json(std::string_view s);

/** An escape_json overload for a char buffer.
 *
 * @param[in] buf The char buffer pointer with characters that need to be escaped.
 *
 * @param[in] size The size of the buf char array.
 */
std::string escape_json(char const *buf, int64_t size);

} // namespace traffic_dump
