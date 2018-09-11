/** @file

  PROXY Protocol

  See:  https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt

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

#ifndef ProxyProtocol_H_
#define ProxyProtocol_H_

#include "tscore/ink_defs.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_resolver.h"
#include "tscore/ink_platform.h"
#include "I_VConnection.h"
#include "I_NetVConnection.h"
#include "I_IOBuffer.h"

// http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt

extern bool proxy_protov1_parse(NetVConnection *, ts::TextView hdr);
extern bool ssl_has_proxy_v1(NetVConnection *, char *, int64_t *);
extern bool http_has_proxy_v1(IOBufferReader *, NetVConnection *);

const char *const PROXY_V1_CONNECTION_PREFACE = "\x50\x52\x4F\x58\x59";
const char *const PROXY_V2_CONNECTION_PREFACE = "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A\x02";

const size_t PROXY_V1_CONNECTION_PREFACE_LEN = strlen(PROXY_V1_CONNECTION_PREFACE); // 5
const size_t PROXY_V2_CONNECTION_PREFACE_LEN = 13;

const size_t PROXY_V1_CONNECTION_HEADER_LEN_MIN = 15;
const size_t PROXY_V2_CONNECTION_HEADER_LEN_MIN = 16;

const size_t PROXY_V1_CONNECTION_HEADER_LEN_MAX = 108;
const size_t PROXY_V2_CONNECTION_HEADER_LEN_MAX = 16;

#endif /* ProxyProtocol_H_ */
