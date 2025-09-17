/** @file

  HTTP utilities for ESI plugins.

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
#include <ts/ts.h>

/// The string to use when the URL cannot be retrieved.
inline constexpr const char *UNKNOWN_URL_STRING = "(unknown)";

/** Returns the pristine request URL for a transaction.
 *
 * @param[in] txnp The transaction to get the URL for.
 *
 * @return The URL. Returns UNKNOWN_URL_STRING if the URL cannot be retrieved
 *   due to API failures or invalid parameters.
 */
std::string getRequestUrlString(TSHttpTxn txnp);

/** Returns a URL string from a buffer and URL loc.
 *
 * @param[in] bufp The buffer to get the URL for.
 * @param[in] url_loc The location of the URL within the buffer.
 *
 * @return The URL. Returns UNKNOWN_URL_STRING if the URL cannot be retrieved
 *   due to API failures or invalid parameters.
 */
std::string getUrlString(TSMBuffer bufp, TSMLoc url_loc);
