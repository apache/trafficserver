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

#include "datasource.h"

#include <array>
#include <algorithm>

constexpr std::array<std::uint16_t, 16> GREASE_values{0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a,
                                                      0x8a8a, 0x9a9a, 0xaaaa, 0xbaba, 0xcaca, 0xdada, 0xeaea, 0xfafa};

JA4::Datasource::Protocol
JA4::Datasource::get_protocol()
{
  return this->_protocol;
}

int
JA4::Datasource::get_version()
{
  return this->_version;
}

JA4::Datasource::SNI
JA4::Datasource::get_sni_type()
{
  return this->_has_SNI ? JA4::Datasource::SNI::to_domain : JA4::Datasource::SNI::to_IP;
}

int
JA4::Datasource::get_cipher_count()
{
  return this->_n_ciphers;
}

int
JA4::Datasource::get_extension_count()
{
  return this->_n_extensions + (this->_has_ALPN ? 1 : 0) + (this->_has_SNI ? 1 : 0);
}

/**
 * Check whether @a value is a GREASE value.
 *
 * These are reserved extensions randomly advertised to keep implementations
 * well lubricated. They are ignored in all parts of JA4 because of their
 * random nature.
 *
 * @return Returns true if the value is a GREASE value, false otherwise.
 */
bool
JA4::Datasource::_is_GREASE(uint16_t value)
{
  return std::binary_search(GREASE_values.begin(), GREASE_values.end(), value);
}
