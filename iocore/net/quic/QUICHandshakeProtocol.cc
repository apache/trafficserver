/** @file
 *
 *  QUIC Handshake Protocol (TLS to Secure QUIC)
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
#include "QUICGlobals.h"
#include "QUICHandshakeProtocol.h"

#include "ts/Diags.h"
#include "ts/string_view.h"
#include "QUICTypes.h"
#include "QUICHKDF.h"

//
// QUICPacketProtection
//

QUICPacketProtection::~QUICPacketProtection()
{
}

void
QUICPacketProtection::set_key(std::unique_ptr<KeyMaterial> km, QUICKeyPhase phase)
{
  this->_key_phase = phase;
  switch (phase) {
  case QUICKeyPhase::PHASE_0:
    this->_phase_0_key = std::move(km);
    break;
  case QUICKeyPhase::PHASE_1:
    this->_phase_1_key = std::move(km);
    break;
  case QUICKeyPhase::CLEARTEXT:
    this->_cleartext_key = std::move(km);
    break;
  case QUICKeyPhase::ZERORTT:
    this->_zerortt_key = std::move(km);
    break;
  }
}

const KeyMaterial *
QUICPacketProtection::get_key(QUICKeyPhase phase) const
{
  switch (phase) {
  case QUICKeyPhase::PHASE_0:
    return this->_phase_0_key.get();
  case QUICKeyPhase::PHASE_1:
    return this->_phase_1_key.get();
  case QUICKeyPhase::CLEARTEXT:
    return this->_cleartext_key.get();
  case QUICKeyPhase::ZERORTT:
    return this->_zerortt_key.get();
  }

  ink_release_assert(!"Bad phase");
  return nullptr;
}

QUICKeyPhase
QUICPacketProtection::key_phase() const
{
  return this->_key_phase;
}
