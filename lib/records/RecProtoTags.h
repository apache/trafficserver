/** @file

  Protocol Tags

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

#ifndef _REC_PROTO_TAGS_H_
#define _REC_PROTO_TAGS_H_

#include "ts/ink_inet.h"
#include "ts/Map.h"

/* Protocol session well-known protocol names.
   These are also used for NPN setup.
*/

const char *const TS_ALPN_PROTOCOL_HTTP_0_9 = IP_PROTO_TAG_HTTP_0_9.ptr();
const char *const TS_ALPN_PROTOCOL_HTTP_1_0 = IP_PROTO_TAG_HTTP_1_0.ptr();
const char *const TS_ALPN_PROTOCOL_HTTP_1_1 = IP_PROTO_TAG_HTTP_1_1.ptr();
const char *const TS_ALPN_PROTOCOL_HTTP_2_0 = IP_PROTO_TAG_HTTP_2_0.ptr();

const char *const TS_PROTO_TAG_HTTP_1_0 = TS_ALPN_PROTOCOL_HTTP_1_0;
const char *const TS_PROTO_TAG_HTTP_1_1 = TS_ALPN_PROTOCOL_HTTP_1_1;
const char *const TS_PROTO_TAG_HTTP_2_0 = TS_ALPN_PROTOCOL_HTTP_2_0;
const char *const TS_PROTO_TAG_TLS_1_3  = IP_PROTO_TAG_TLS_1_3.ptr();
const char *const TS_PROTO_TAG_TLS_1_2  = IP_PROTO_TAG_TLS_1_2.ptr();
const char *const TS_PROTO_TAG_TLS_1_1  = IP_PROTO_TAG_TLS_1_1.ptr();
const char *const TS_PROTO_TAG_TLS_1_0  = IP_PROTO_TAG_TLS_1_0.ptr();
const char *const TS_PROTO_TAG_TCP      = IP_PROTO_TAG_TCP.ptr();
const char *const TS_PROTO_TAG_UDP      = IP_PROTO_TAG_UDP.ptr();
const char *const TS_PROTO_TAG_IPV4     = IP_PROTO_TAG_IPV4.ptr();
const char *const TS_PROTO_TAG_IPV6     = IP_PROTO_TAG_IPV6.ptr();

typedef HashSet<const char *, StringHashFns, const char *> TSProtoTagsSet;
static TSProtoTagsSet TSProtoTags;

void ts_session_protocol_well_known_name_tags_init();

const char *RecNormalizeProtoTag(const char *tag);

#endif
