/** @file

  Header file for SSLProxySession class.

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

#include <memory>
#include <string_view>

class SSLNetVConnection;

class SSLProxySession
{
public:
  // Returns null pointer if no SNI server name, otherwise pointer to null-terminated string.
  //
  char const *
  client_sni_server_name() const
  {
    return _client_sni_server_name.get();
  }

  bool
  client_provided_certificate() const
  {
    return _client_provided_cert;
  }

  void init(SSLNetVConnection const &new_vc);

private:
  std::unique_ptr<char[]> _client_sni_server_name;
  bool _client_provided_cert = false;
};
