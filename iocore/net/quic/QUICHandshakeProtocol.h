/** @file
 *
 *  QUIC TLS
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

#include "QUICKeyGenerator.h"
#include "QUICTypes.h"

class QUICHandshakeProtocol;

class QUICPacketProtection
{
public:
  QUICPacketProtection(){};
  ~QUICPacketProtection();
  void set_key(std::unique_ptr<KeyMaterial> km, QUICKeyPhase phase);
  const KeyMaterial *get_key(QUICKeyPhase phase) const;
  QUICKeyPhase key_phase() const;

private:
  // TODO: discard keys
  std::unique_ptr<KeyMaterial> _key_chain[5] = {nullptr};
  QUICKeyPhase _key_phase                    = QUICKeyPhase::INITIAL;
};

class QUICPacketNumberProtector
{
public:
  bool protect(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn, uint8_t unprotected_pn_len,
               const uint8_t *sample, QUICKeyPhase phase) const;
  bool unprotect(uint8_t *unprotected_pn, uint8_t &unprotected_pn_len, const uint8_t *protected_pn, uint8_t protected_pn_len,
                 const uint8_t *sample, QUICKeyPhase phase) const;

  // FIXME We don't need QUICHandshakeProtocol here, and should pass QUICPacketProtection instead.
  void set_hs_protocol(QUICHandshakeProtocol *hs_protocol);

private:
  QUICHandshakeProtocol *_hs_protocol = nullptr;
};

struct QUICHandshakeMsgs {
  uint8_t *buf       = nullptr; //< pointer to the buffer
  size_t max_buf_len = 0;       //< size of buffer
  size_t offsets[5]  = {0};     //< offset to the each encryption level - {initial, zero_rtt, handshake, one_rtt, total length}
};

class QUICHandshakeProtocol
{
public:
  QUICHandshakeProtocol(){};
  virtual ~QUICHandshakeProtocol(){};

  virtual int handshake(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in)                           = 0;
  virtual bool is_handshake_finished() const                                                           = 0;
  virtual bool is_ready_to_derive() const                                                              = 0;
  virtual bool is_key_derived(QUICKeyPhase key_phase, bool for_encryption) const                       = 0;
  virtual int initialize_key_materials(QUICConnectionId cid)                                           = 0;
  virtual int update_key_materials()                                                                   = 0;
  virtual bool encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                       uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const   = 0;
  virtual bool decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                       uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const   = 0;
  virtual bool encrypt_pn(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn,
                          uint8_t unprotected_pn_len, const uint8_t *sample, QUICKeyPhase phase) const = 0;
  virtual bool decrypt_pn(uint8_t *unprotected_pn, uint8_t &unprotected_pn_len, const uint8_t *protected_pn,
                          uint8_t protected_pn_len, const uint8_t *sample, QUICKeyPhase phase) const   = 0;
  virtual QUICEncryptionLevel current_encryption_level() const                                         = 0;
  virtual void abort_handshake()                                                                       = 0;
};
