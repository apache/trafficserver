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

#include "tscore/ink_platform.h"
#include "iocore/net/Net.h"

#include <string_view>

#include "proxy/http/HttpSessionAccept.h"

// HTTP/QUIC Session Accept.
//
// HTTP/QUIC needs to be explicitly enabled on a server port. The syntax is different for SSL and raw
// ports. The example below configures QUIC on port 443 (with TLS).
//
// CONFIG proxy.config.http.server_ports STRING 443:quic

class Http3SessionAccept : public HttpSessionAcceptBase
{
public:
  /// The HTTP application selected from a negotiated ALPN tag.
  enum class AppType {
    HTTP_09, ///< HTTP/0.9 over QUIC (interop only).
    HTTP_3,  ///< HTTP/3.
    UNKNOWN, ///< Missing or unrecognized ALPN; the connection must be rejected.
  };

  explicit Http3SessionAccept(OptionsHandle options, HttpProxyPort *proxy_port = nullptr);
  ~Http3SessionAccept();

  bool accept(NetVConnection *, MIOBuffer *, IOBufferReader *) override;
  int  mainEvent(int event, void *netvc) override;

  /// Map a negotiated ALPN tag to the HTTP application to run.
  /// @return @c AppType::UNKNOWN for an empty or unrecognized @a alpn.
  static AppType select_app_type(std::string_view alpn);

private:
  Http3SessionAccept(const Http3SessionAccept &);
  Http3SessionAccept &operator=(const Http3SessionAccept &);
};
