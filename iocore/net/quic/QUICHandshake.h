/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "QUICConnection.h"
#include "QUICApplication.h"

/**
 * @class QUICHandshake
 * @brief Do handshake as a QUIC application
 * @detail
 *
 * state_read_client_hello()
 *  |  | _process_client_hello()
 *  |  v
 *  | state_read_client_finished()
 *  |  |  | _process_client_finished()
 *  | /  | _process_handshake_complete()
 *  |     v
 *  |    state_complete()
 *  |
 *  v
 * state_closed()
 */
class QUICHandshake : public QUICApplication
{
public:
  QUICHandshake(QUICConnection *qc, QUICCrypto *c);

  int state_read_client_hello(int event, Event *data);
  int state_read_client_finished(int event, Event *data);
  int state_address_validation(int event, void *data);
  int state_complete(int event, void *data);
  int state_closed(int event, void *data);
  bool is_completed();
  void negotiated_application_name(const uint8_t **name, unsigned int *len);

private:
  QUICCrypto *_crypto = nullptr;
  QUICError _process_client_hello();
  QUICError _process_client_finished();
  QUICError _process_handshake_complete();
};
