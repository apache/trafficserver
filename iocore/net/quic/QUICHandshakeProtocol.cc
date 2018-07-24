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
#include "QUICTypes.h"
#include "QUICHKDF.h"

//
// QUICPacketProtection
//

QUICPacketProtection::~QUICPacketProtection() {}

void
QUICPacketProtection::set_key(std::unique_ptr<KeyMaterial> km, QUICKeyPhase phase)
{
  this->_key_chain[static_cast<int>(phase)] = std::move(km);
}

const KeyMaterial *
QUICPacketProtection::get_key(QUICKeyPhase phase) const
{
  return this->_key_chain[static_cast<int>(phase)].get();
}

QUICKeyPhase
QUICPacketProtection::key_phase() const
{
  return this->_key_phase;
}

//
// QUICPacketNumberProtector
//

bool
QUICPacketNumberProtector::protect(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn,
                                   uint8_t unprotected_pn_len, const uint8_t *sample, QUICKeyPhase phase) const
{
  // FIXME HandshakeProtocol shouldn't do this. The logic should be moved from there to here.
  return this->_hs_protocol->encrypt_pn(protected_pn, protected_pn_len, unprotected_pn, unprotected_pn_len, sample, phase);
}

bool
QUICPacketNumberProtector::unprotect(uint8_t *unprotected_pn, uint8_t &unprotected_pn_len, const uint8_t *protected_pn,
                                     uint8_t protected_pn_len, const uint8_t *sample, QUICKeyPhase phase) const
{
  // FIXME HandshakeProtocol shouldn't do this. The logic should be moved from there to here.
  return this->_hs_protocol->decrypt_pn(unprotected_pn, unprotected_pn_len, protected_pn, protected_pn_len, sample, phase);
}

void
QUICPacketNumberProtector::set_hs_protocol(QUICHandshakeProtocol *hs_protocol)
{
  this->_hs_protocol = hs_protocol;
}
