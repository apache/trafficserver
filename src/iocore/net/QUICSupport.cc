/** @file

  TLSSBasicSupport.cc provides implementations for
  QUICSupport methods

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

#include "iocore/net/QUICSupport.h"

int QUICSupport::_ex_data_index = -1;

void
QUICSupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"QUICSupport index", nullptr, nullptr, nullptr);
  }
}

QUICSupport *
QUICSupport::getInstance(SSL *ssl)
{
  return static_cast<QUICSupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
QUICSupport::bind(SSL *ssl, QUICSupport *srs)
{
  SSL_set_ex_data(ssl, _ex_data_index, srs);
}

void
QUICSupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

void
QUICSupport::clear()
{
}
