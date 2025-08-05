/** @file
 *
 * XDebug plugin headers functionality declarations.
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

#include <sstream>
#include "ts/ts.h"

namespace xdebug
{

/**
 * Whether to print the headers for the "probe-full-json" format.
 */
static constexpr bool FULL_JSON = true;

/**
 * Print headers to a stringstream with JSON-like formatting.
 * @param bufp The TSMBuffer containing the headers.
 * @param hdr_loc The TSMLoc for the headers.
 * @param ss The stringstream to write to.
 * @param full_json Whether to print the headers in a compliant JSON
 * format. The legacy "probe" format is not JSON-compliant. The new
 * "probe-full-json" format is JSON-compliant.
 */
void print_headers(TSMBuffer bufp, TSMLoc hdr_loc, std::stringstream &ss, bool full_json);

/**
 * Log headers to debug for debugging purposes.
 * @param txn The transaction.
 * @param bufp The TSMBuffer containing the headers.
 * @param hdr_loc The TSMLoc for the headers.
 * @param type_msg Description of the header type.
 */
void log_headers(TSHttpTxn txn, TSMBuffer bufp, TSMLoc hdr_loc, char const *type_msg);

/**
 * Print request headers in the "probe" format.
 * @param txn The transaction.
 * @param output The stringstream to write to.
 */
void print_request_headers(TSHttpTxn txn, std::stringstream &output);

/**
 * Print response headers in the "probe" format.
 * @param txn The transaction.
 * @param output The stringstream to write to.
 */
void print_response_headers(TSHttpTxn txn, std::stringstream &output);

/**
 * Print request headers in JSON format for probe-full-json.
 * @param txn The transaction.
 * @param output The stringstream to write to.
 */
void print_request_headers_full_json(TSHttpTxn txn, std::stringstream &output);

/**
 * Print response headers in JSON format for probe-full-json.
 * @param txn The transaction.
 * @param output The stringstream to write to.
 */
void print_response_headers_full_json(TSHttpTxn txn, std::stringstream &output);

} // namespace xdebug
