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

  void drop_keys(QUICKeyPhase phase);

  // Payload Protection (common)

  virtual const EVP_CIPHER *get_cipher(QUICKeyPhase phase) const;
  virtual size_t get_tag_len(QUICKeyPhase phase) const;
  virtual void set_cipher_initial(const EVP_CIPHER *cipher);
  virtual void set_cipher(const EVP_CIPHER *cipher, size_t tag_len);

  // Payload Protection (encryption)

  virtual bool is_encryption_key_available(QUICKeyPhase phase) const;
  virtual void set_encryption_key_available(QUICKeyPhase phase);

  virtual const uint8_t *encryption_key(QUICKeyPhase phase) const;
  virtual uint8_t *encryption_key(QUICKeyPhase phase);

  virtual size_t encryption_key_len(QUICKeyPhase phase) const;

  virtual const uint8_t *encryption_iv(QUICKeyPhase phase) const;
  virtual uint8_t *encryption_iv(QUICKeyPhase phase);

  virtual const size_t *encryption_iv_len(QUICKeyPhase phase) const;
  virtual size_t *encryption_iv_len(QUICKeyPhase phase);

  // Payload Protection (decryption)

  virtual bool is_decryption_key_available(QUICKeyPhase phase) const;
  virtual void set_decryption_key_available(QUICKeyPhase phase);

  virtual const uint8_t *decryption_key(QUICKeyPhase phase) const;
  virtual uint8_t *decryption_key(QUICKeyPhase phase);

  virtual size_t decryption_key_len(QUICKeyPhase phase) const;

  virtual const uint8_t *decryption_iv(QUICKeyPhase phase) const;
  virtual uint8_t *decryption_iv(QUICKeyPhase phase);

  virtual const size_t *decryption_iv_len(QUICKeyPhase phase) const;
  virtual size_t *decryption_iv_len(QUICKeyPhase phase);

  // Header Protection

  virtual const EVP_CIPHER *get_cipher_for_hp(QUICKeyPhase phase) const;
  virtual void set_cipher_for_hp_initial(const EVP_CIPHER *cipher);
  virtual void set_cipher_for_hp(const EVP_CIPHER *cipher);

  virtual const uint8_t *encryption_key_for_hp(QUICKeyPhase phase) const;
  virtual uint8_t *encryption_key_for_hp(QUICKeyPhase phase);

  virtual size_t encryption_key_for_hp_len(QUICKeyPhase phase) const;

  virtual const uint8_t *decryption_key_for_hp(QUICKeyPhase phase) const;
  virtual uint8_t *decryption_key_for_hp(QUICKeyPhase phase);

  virtual size_t decryption_key_for_hp_len(QUICKeyPhase phase) const;

private:
  Context _ctx = Context::SERVER;

  // Payload Protection

  const EVP_CIPHER *_cipher_initial = nullptr;
  const EVP_CIPHER *_cipher         = nullptr;
  size_t _tag_len                   = 0;

  bool _is_client_key_available[5] = {false};
  bool _is_server_key_available[5] = {false};

  // FIXME EVP_MAX_KEY_LENGTH and EVP_MAX_IV_LENGTH are not enough somehow
  uint8_t _client_key[5][512];
  uint8_t _server_key[5][512];

  uint8_t _client_iv[5][512];
  uint8_t _server_iv[5][512];

  size_t _client_iv_len[5];
  size_t _server_iv_len[5];

  // Header Protection

  const EVP_CIPHER *_cipher_for_hp_initial = nullptr;
  const EVP_CIPHER *_cipher_for_hp         = nullptr;

  uint8_t _client_key_for_hp[5][512];
  uint8_t _server_key_for_hp[5][512];
};
