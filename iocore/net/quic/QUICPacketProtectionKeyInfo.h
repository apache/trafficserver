/** @file
 *
 *  QUIC Packet Protection Key Info
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

#include "QUICTypes.h"
#include "QUICKeyGenerator.h"

class QUICPacketProtectionKeyInfo
{
public:
  enum class Context { SERVER, CLIENT };

  // FIXME This should be passed to the constructor but NetVC cannot pass it because it has set_context too.
  void set_context(Context ctx);

  void add_key(QUICEncryptionLevel level, int key, QUICKeyPhase phase);
  void update_key(QUICEncryptionLevel level, int key);
  void remove_key(QUICEncryptionLevel level);
  void remove_key(QUICEncryptionLevel level, QUICKeyPhase phase);

  void encryption_key_for_pp(QUICEncryptionLevel level) const;
  void decryption_key_for_pp(QUICEncryptionLevel level) const;

  // Header Protection

  const QUIC_EVP_CIPHER *get_cipher_for_hp_initial() const;
  void set_cipher_for_hp_initial(const QUIC_EVP_CIPHER *cipher);

  const QUIC_EVP_CIPHER *get_cipher_for_hp(QUICEncryptionLevel level) const;
  void set_cipher_for_hp(const QUIC_EVP_CIPHER *cipher);

  const uint8_t *encryption_key_for_hp(QUICEncryptionLevel level) const;
  uint8_t *encryption_key_for_hp(QUICEncryptionLevel level);

  const uint8_t *decryption_key_for_hp(QUICEncryptionLevel level) const;
  uint8_t *decryption_key_for_hp(QUICEncryptionLevel level);

private:
  Context _ctx = Context::SERVER;

  const QUIC_EVP_CIPHER *_cipher_for_hp_initial = nullptr;
  const QUIC_EVP_CIPHER *_cipher_for_hp         = nullptr;

  uint8_t _client_key_for_hp[4][512];
  uint8_t _server_key_for_hp[4][512];
};
