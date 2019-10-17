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
 * @file config.h
 * @brief Access Control Plug-in Configuration (Headers).
 * @see config.h
 */

#pragma once

#include "common.h"
#include "access_control.h"
#include "pattern.h"

/**
 * Access control plugin configuration.
 */
class AccessControlConfig
{
public:
  AccessControlConfig() {}
  virtual ~AccessControlConfig() { delete _tokenFactory; }
  bool init(int argc, char *argv[]);

  bool loadMultiPatternsFromFile(const String &filename, bool blacklist = true);

  StringMap _symmetricKeysMap; /** @brief a map secrets accessible by key string (KID) */

  /* Predefined and plugin parameter configurable HTTP responses. */
  TSHttpStatus _invalidSignature      = TS_HTTP_STATUS_UNAUTHORIZED;
  TSHttpStatus _invalidTiming         = TS_HTTP_STATUS_FORBIDDEN;
  TSHttpStatus _invalidScope          = TS_HTTP_STATUS_FORBIDDEN;
  TSHttpStatus _invalidSyntax         = TS_HTTP_STATUS_BAD_REQUEST;
  TSHttpStatus _invalidRequest        = TS_HTTP_STATUS_BAD_REQUEST;
  TSHttpStatus _invalidOriginResponse = static_cast<TSHttpStatus>(520); /* catch all response for unexpected origin responses,
                                                                           although TS_HTTP_STATUS_BAD_GATEWAY seems more
                                                                           appropriate it is too widely used */
  TSHttpStatus _internalError = TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;

  KvpAccessTokenConfig _kvpAccessTokenConfig;
  bool _debugLevel = false;

  String _cookieName = "cdn_auth"; /** @brief name of the cookie containing the token to be verified */

  AccessTokenFactory *_tokenFactory = nullptr;

  bool _rejectRequestsWithInvalidTokens = false; /** reject versa forward to the origin if access token is invalid */
  String _respTokenHeaderName;   /** @brief name of header used by origin to provide the access token in its response */
  String _extrSubHdrName;        /** @brief header name to extract the token subject content, if empty => no extraction */
  String _extrTokenIdHdrName;    /** @brief header name to extract the token id, if empty => no extraction */
  String _extrValidationHdrName; /** @brief header name to extract the token validation status, if empty => no extraction */
  bool _useRedirects = false;    /** @brief true - use redirect to set the access token cookie, @todo not used yet */
  Classifier _uriPathScope; /**< @brief blacklist (exclude) and white-list (include) which path should have the access control */
};
