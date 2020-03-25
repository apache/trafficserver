/** @file

  Traffic Server SDK API - HTTP related enumerations

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

  @section developers Developers

  Developers, when adding a new element to an enum, append it. DO NOT
  insert it.  Otherwise, binary compatibility of plugins will be broken!

 */

#pragma once

/// Server session sharing values - match
typedef enum {
  TS_SERVER_SESSION_SHARING_MATCH_IP,
  TS_SERVER_SESSION_SHARING_MATCH_HOSTONLY,
  TS_SERVER_SESSION_SHARING_MATCH_HOSTSNISYNC,
  TS_SERVER_SESSION_SHARING_MATCH_SNI,
  TS_SERVER_SESSION_SHARING_MATCH_CERT,
  TS_SERVER_SESSION_SHARING_MATCH_NONE,
  TS_SERVER_SESSION_SHARING_MATCH_BOTH,
  TS_SERVER_SESSION_SHARING_MATCH_HOST,
} TSServerSessionSharingMatchType;

typedef enum {
  TS_SERVER_SESSION_SHARING_MATCH_MASK_NONE        = 0,
  TS_SERVER_SESSION_SHARING_MATCH_MASK_IP          = 0x1,
  TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY    = 0x2,
  TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTSNISYNC = 0x4,
  TS_SERVER_SESSION_SHARING_MATCH_MASK_SNI         = 0x8,
  TS_SERVER_SESSION_SHARING_MATCH_MASK_CERT        = 0x10
} TSServerSessionSharingMatchMask;

/// Server session sharing values - pool
typedef enum {
  TS_SERVER_SESSION_SHARING_POOL_GLOBAL,
  TS_SERVER_SESSION_SHARING_POOL_THREAD,
} TSServerSessionSharingPoolType;

// This is use to signal apidefs.h to not define these again.
#ifndef _HTTP_PROXY_API_ENUMS_H_
#define _HTTP_PROXY_API_ENUMS_H_

/// Values for per server outbound connection tracking group definition.
/// See proxy.config.http.per_server.match
typedef enum {
  TS_SERVER_OUTBOUND_MATCH_IP,
  TS_SERVER_OUTBOUND_MATCH_PORT,
  TS_SERVER_OUTBOUND_MATCH_HOST,
  TS_SERVER_OUTBOUND_MATCH_BOTH
} TSOutboundConnectionMatchType;

#endif
