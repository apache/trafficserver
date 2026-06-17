/*
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

/**
 * @file path.h
 * @brief Prefetch path construction helpers.
 */

#pragma once

#include "common.h"

/** A normalized path and optional query for a derived prefetch request. */
struct SafeRelativeFetchPath {
  String path;          ///< Normalized path component without a query or fragment.
  String query;         ///< Query string without the leading @c ?, meaningful when @c hasQuery is @c true.
  bool hasQuery{false}; ///< Whether @c query should replace the cloned request query.
};

/** Build a normalized prefetch target from a relative path.
 *
 * The prefetch plugin accepts query metadata as an instruction to fetch a
 * sibling object relative to the triggering object. Without this boundary
 * check, an attacker can turn a request for an allowed object into an implicit
 * background fetch for a different origin resource by using traversal segments
 * such as @c ../ in that metadata. This is not a general rejection of HTTP URL
 * paths containing dot segments; it enforces the plugin's relative prefetch
 * contract before scheduling and caching a derived request.
 *
 * The path component of @a relativePath is resolved against the directory that
 * contains @a currentPath. Query suffixes are returned separately so callers
 * can set the URL query component instead of embedding @c ? in the path.
 * Fragment suffixes are rejected because they are not sent in HTTP requests.
 * The resulting path must remain under that original directory after
 * dot-segment cleanup and after decoding encoded dot or separator octets used
 * for validation.
 *
 * @param[in] currentPath The path of the request that is triggering the
 * prefetch.
 * @param[in] relativePath The query-supplied path to fetch relative to @a
 * currentPath.
 * @param[out] fetchPath The normalized fetch path and optional query when
 * construction succeeds.
 * @return @c true if @a fetchPath was populated with a safe target, @c false
 * if @a relativePath is empty, absolute, contains a fragment, or would escape
 * the current directory.
 */
bool makeSafeRelativeFetchPath(const String &currentPath, const String &relativePath, SafeRelativeFetchPath &fetchPath);
