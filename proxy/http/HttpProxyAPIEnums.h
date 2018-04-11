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

#ifndef _HTTP_PROXY_API_ENUMS_H_
#define _HTTP_PROXY_API_ENUMS_H_

/// Server session sharing values - match
typedef enum {
  TS_SERVER_SESSION_SHARING_MATCH_NONE,
  TS_SERVER_SESSION_SHARING_MATCH_BOTH,
  TS_SERVER_SESSION_SHARING_MATCH_IP,
  TS_SERVER_SESSION_SHARING_MATCH_HOST
} TSServerSessionSharingMatchType;

/// Server session sharing values - pool
typedef enum {
  TS_SERVER_SESSION_SHARING_POOL_GLOBAL,
  TS_SERVER_SESSION_SHARING_POOL_THREAD,
} TSServerSessionSharingPoolType;
#endif
