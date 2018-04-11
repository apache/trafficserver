/** @file

  A brief file description

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

/*When this is being compiled with TS, we enable more features the use
  non modularized stuff. namely:
  ip_ranges and multiple socks server support.
*/
#define SOCKS_WITH_TS

#define SOCKS_DEFAULT_VERSION 0 // defined the configuration variable
#define SOCKS4_VERSION 4
#define SOCKS5_VERSION 5
#define SOCKS_CONNECT 1
#define SOCKS4_REQ_LEN 9
#define SOCKS4_REP_LEN 8
#define SOCKS5_REP_LEN 262 // maximum possible
#define SOCKS4_REQ_GRANTED 90
#define SOCKS4_CONN_FAILED 91
#define SOCKS5_REQ_GRANTED 0
#define SOCKS5_CONN_FAILED 1

enum {
  // For these two, we need to pick two values which are not used for any of the
  //"commands" (eg: CONNECT, BIND) in SOCKS protocols.
  NORMAL_SOCKS = 0,
  NO_SOCKS     = 48
};

enum {
  SOCKS_ATYPE_NONE = 0,
  SOCKS_ATYPE_IPV4 = 1,
  SOCKS_ATYPE_FQHN = 3,
  SOCKS_ATYPE_IPV6 = 4,
};

struct SocksAddrType {
  unsigned char type;
  union {
    // mostly it is ipv4. in other cases we will xalloc().
    unsigned char ipv4[4];
    unsigned char *buf;
  } addr;

  void reset();
  SocksAddrType() : type(SOCKS_ATYPE_NONE) { addr.buf = nullptr; }
  ~SocksAddrType() { reset(); }
};
