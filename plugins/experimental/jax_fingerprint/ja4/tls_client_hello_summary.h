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

#include "ts/ts.h"
#include "datasource.h"

/**
 * Represents the data sent in a TLS Client Hello needed for JA4 fingerprints.
 */
class TLSClientHelloSummary : public JA4::Datasource
{
public:
  TLSClientHelloSummary(JA4::Datasource::Protocol protocol, TSClientHello ch);

  std::string_view get_first_alpn() override;
  void             get_cipher_suites_hash(unsigned char out[32]) override;
  void             get_extension_hash(unsigned char out[32]) override;

private:
  static constexpr int MAX_CIPHERS_FOR_FAST_PATH    = 64;
  static constexpr int MAX_EXTENSIONS_FOR_FAST_PATH = 32;

  TSClientHello _ch;

  uint16_t                                       *_ciphers = nullptr;
  std::array<uint16_t, MAX_CIPHERS_FOR_FAST_PATH> _fast_cipher_storage;
  std::unique_ptr<uint16_t[]>                     _slow_cipher_storage;

  uint16_t                                          *_extensions = nullptr;
  std::array<uint16_t, MAX_EXTENSIONS_FOR_FAST_PATH> _fast_extension_storage;
  std::unique_ptr<uint16_t[]>                        _slow_extension_storage;
};
