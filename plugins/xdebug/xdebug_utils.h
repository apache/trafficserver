/** @file
 *
 * XDebug plugin utility functions.
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

enum class BodyEncoding_t;

/** Parse the probe-full-json header field value.
 *
 * @param[in] value The header field value to parse.
 * @param[out] encoding The encoding to set.
 * @return True if the header field value was parsed successfully, false otherwise.
 *
 * Supports formats:
 * - "probe-full-json" -> encoding = AUTO, returns true
 * - "probe-full-json=b64" -> encoding = BASE64, returns true
 * - "probe-full-json=escape" -> encoding = ESCAPE, returns true
 * - "probe-full-json=nobody" -> encoding = OMIT_BODY, returns true
 * - Invalid formats return false
 */
bool parse_probe_full_json_field_value(std::string_view value, BodyEncoding_t &encoding);

/** Check if a content-type string represents textual content.
 *
 * @param ct The content-type string to check.
 * @return True if the content-type represents textual content.
 *
 * Considers the following as textual:
 * - Starts with "text/"
 * - Contains "json", "xml", "javascript", "csv", "html", or "plain"
 */
bool is_textual_content_type(std::string_view ct);

} // namespace xdebug
