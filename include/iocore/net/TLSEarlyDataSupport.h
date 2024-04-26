/** @file

  TLSEarlyDataSupport implements common methods and members to
  support TLS Early Data

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

#include <openssl/ssl.h>

class TLSEarlyDataSupport
{
public:
  virtual ~TLSEarlyDataSupport() = default;

  static void                 initialize();
  static TLSEarlyDataSupport *getInstance(SSL *ssl);
  static void                 bind(SSL *ssl, TLSEarlyDataSupport *srs);
  static void                 unbind(SSL *ssl);

  size_t get_early_data_len() const;
  void   update_early_data_config(SSL *ssl, uint32_t max_early_data, uint32_t recv_max_early_data);

protected:
  void clear();

  void _increment_early_data_len(size_t amount);

private:
  static int _ex_data_index;

  size_t _early_data_len = 0;
};
