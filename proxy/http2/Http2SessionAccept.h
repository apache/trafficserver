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

#ifndef __HTTP2_SESSION_ACCEPT_H__
#define __HTTP2_SESSION_ACCEPT_H__

#include "ts/ink_platform.h"
#include "I_Net.h"

// XXX HttpSessionAccept::Options needs to be refactored and separated from HttpSessionAccept so that
// it can generically apply to all protocol implementations.
#include "http/HttpSessionAccept.h"

// HTTP/2 Session Accept.
//
// HTTP/2 needs to be explicitly enabled on a server port. The syntax is different for SSL and raw
// ports. There's currently no support for the HTTP/1.1 upgrade path. The example below configures
// HTTP/2 on port 80 and port 443 (with TLS).
//
// CONFIG proxy.config.http.server_ports STRING 80:proto=http2 443:ssl:proto=h2-12

struct Http2SessionAccept : public SessionAccept {
  explicit Http2SessionAccept(const HttpSessionAccept::Options &);
  ~Http2SessionAccept();

  bool accept(NetVConnection *, MIOBuffer *, IOBufferReader *);
  int mainEvent(int event, void *netvc);

private:
  Http2SessionAccept(const Http2SessionAccept &);
  Http2SessionAccept &operator=(const Http2SessionAccept &);

  HttpSessionAccept::Options options;
};

#endif // __HTTP2_SESSION_ACCEPT_H__
