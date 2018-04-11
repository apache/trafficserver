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
 * @file cachekey.h
 * @brief Cache key manipulation (header file).
 */

#pragma once

#include "common.h"
#include "configs.h"

/**
 * @brief Cache key manipulation class.
 *
 * Initialize the cache key from the request URI.
 *
 * The cache key is to be a valid URI. Key structure documented in doc/cachekey.en.rst#cache-key-structure
 * @note scheme, #fragment, user:password@ from URI authority component are currently ignored.
 * The query parameters, headers and cookies are handled similarly in general,
 * but there are some differences in the handling of the query and the rest of the elements:
 * - headers and cookies are never included in the cache key by default, query is.
 * - query manipulation is different (stripping off, sorting, exclusion of query parameters, etc).
 * That is why seemed like a good idea to add headers, cookies, UA-captures, UA-classes
 * to the "hier-part" and keep only the query parameters in the "query part" (RFC 3986).
 *
 * @todo Consider avoiding the ATS API multiple-lookups while handling headers and cookies.
 * Currently ts/ts.h states that iterating through the headers one by one is not efficient
 * but being able to iterate through all the headers once and figure out what to append to
 * the cache key seems be more time efficient.
 */
class CacheKey
{
public:
  CacheKey(TSHttpTxn txn, TSMBuffer buf, TSMLoc url, TSMLoc hdrs, String separator);

  void append(unsigned number);
  void append(const String &);
  void append(const char *s);
  void append(const char *n, unsigned s);
  void appendPrefix(const String &prefix, Pattern &prefixCapture, Pattern &prefixCaptureUri);
  void appendPath(Pattern &pathCapture, Pattern &pathCaptureUri);
  void appendHeaders(const ConfigHeaders &config);
  void appendQuery(const ConfigQuery &config);
  void appendCookies(const ConfigCookies &config);
  void appendUaCaptures(Pattern &config);
  bool appendUaClass(Classifier &classifier);
  bool finalize() const;

  // noncopyable
  CacheKey(const CacheKey &) = delete;            // disallow
  CacheKey &operator=(const CacheKey &) = delete; // disallow

private:
  CacheKey(); // disallow

  /* Information from the request */
  TSHttpTxn _txn; /**< @brief transaction handle */
  TSMBuffer _buf; /**< @brief marshal buffer */
  TSMLoc _url;    /**< @brief URI handle */
  TSMLoc _hdrs;   /**< @brief headers handle */

  String _key;       /**< @brief cache key */
  String _separator; /**< @brief a separator used to separate the cache key elements extracted from the URI */
};
