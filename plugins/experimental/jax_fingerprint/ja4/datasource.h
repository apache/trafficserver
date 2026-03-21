/** @file

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

#include <string_view>

constexpr uint16_t EXT_SNI{0x0};
constexpr uint16_t EXT_ALPN{0x10};
constexpr uint16_t EXT_SUPPORTED_VERSIONS{0x2b};

namespace JA4
{

class Datasource
{
public:
  Datasource()          = default;
  virtual ~Datasource() = default;

  enum class Protocol {
    DTLS = 'd',
    QUIC = 'q',
    TLS  = 't',
  };

  enum class SNI {
    to_domain = 'd',
    to_IP     = 'i',
  };

  Protocol                 get_protocol();
  int                      get_version();
  SNI                      get_sni_type();
  int                      get_cipher_count();
  int                      get_extension_count();
  virtual std::string_view get_first_alpn()                              = 0;
  virtual void             get_cipher_suites_hash(unsigned char out[32]) = 0;
  virtual void             get_extension_hash(unsigned char out[32])     = 0;

protected:
  bool _is_GREASE(uint16_t value);

  Protocol _protocol;
  int      _version      = 0;
  bool     _has_ALPN     = false;
  bool     _has_SNI      = false;
  int      _n_ciphers    = 0;
  int      _n_extensions = 0;
};

} // namespace JA4
