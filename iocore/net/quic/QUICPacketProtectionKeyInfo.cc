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

#include "QUICPacketProtectionKeyInfo.h"

void
QUICPacketProtectionKeyInfo::set_context(Context ctx)
{
  this->_ctx = ctx;
}

void
QUICPacketProtectionKeyInfo::drop_keys(QUICKeyPhase phase)
{
  int index = static_cast<int>(phase);

  this->_is_client_key_available[index] = false;
  this->_is_server_key_available[index] = false;

  memset(this->_client_key[index], 0x00, sizeof(this->_client_key[index]));
  memset(this->_server_key[index], 0x00, sizeof(this->_server_key[index]));

  memset(this->_client_iv[index], 0x00, sizeof(this->_client_iv[index]));
  memset(this->_server_iv[index], 0x00, sizeof(this->_server_iv[index]));

  this->_client_iv_len[index] = 0;
  this->_server_iv_len[index] = 0;

  memset(this->_client_key_for_hp[index], 0x00, sizeof(this->_client_key_for_hp[index]));
  memset(this->_server_key_for_hp[index], 0x00, sizeof(this->_server_key_for_hp[index]));
}

const EVP_CIPHER *
QUICPacketProtectionKeyInfo::get_cipher(QUICKeyPhase phase) const
{
  switch (phase) {
  case QUICKeyPhase::INITIAL:
    return this->_cipher_initial;
  default:
    return this->_cipher;
  }
}

size_t
QUICPacketProtectionKeyInfo::get_tag_len(QUICKeyPhase phase) const
{
  switch (phase) {
  case QUICKeyPhase::INITIAL:
    return EVP_GCM_TLS_TAG_LEN;
  default:
    return this->_tag_len;
  }
}

bool
QUICPacketProtectionKeyInfo::is_encryption_key_available(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_is_server_key_available[index];
  } else {
    return this->_is_client_key_available[index];
  }
}

void
QUICPacketProtectionKeyInfo::set_encryption_key_available(QUICKeyPhase phase)
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    this->_is_server_key_available[index] = true;
  } else {
    this->_is_client_key_available[index] = true;
  }
}

const uint8_t *
QUICPacketProtectionKeyInfo::encryption_key(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_server_key[index];
  } else {
    return this->_client_key[index];
  }
}

uint8_t *
QUICPacketProtectionKeyInfo::encryption_key(QUICKeyPhase phase)
{
  return const_cast<uint8_t *>(const_cast<const QUICPacketProtectionKeyInfo *>(this)->encryption_key(phase));
}

size_t
QUICPacketProtectionKeyInfo::encryption_key_len(QUICKeyPhase phase) const
{
  const EVP_CIPHER *cipher;

  switch (phase) {
  case QUICKeyPhase::INITIAL:
    cipher = this->_cipher_initial;
    break;
  default:
    cipher = this->_cipher;
    break;
  }

  return EVP_CIPHER_key_length(cipher);
}

const uint8_t *
QUICPacketProtectionKeyInfo::encryption_iv(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_server_iv[index];
  } else {
    return this->_client_iv[index];
  }
}

uint8_t *
QUICPacketProtectionKeyInfo::encryption_iv(QUICKeyPhase phase)
{
  return const_cast<uint8_t *>(const_cast<const QUICPacketProtectionKeyInfo *>(this)->encryption_iv(phase));
}

const size_t *
QUICPacketProtectionKeyInfo::encryption_iv_len(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return &this->_server_iv_len[index];
  } else {
    return &this->_client_iv_len[index];
  }
}

size_t *
QUICPacketProtectionKeyInfo::encryption_iv_len(QUICKeyPhase phase)
{
  return const_cast<size_t *>(const_cast<const QUICPacketProtectionKeyInfo *>(this)->encryption_iv_len(phase));
}

bool
QUICPacketProtectionKeyInfo::is_decryption_key_available(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_is_client_key_available[index];
  } else {
    return this->_is_server_key_available[index];
  }
}

void
QUICPacketProtectionKeyInfo::set_decryption_key_available(QUICKeyPhase phase)
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    this->_is_client_key_available[index] = true;
  } else {
    this->_is_server_key_available[index] = true;
    ;
  }
}

const uint8_t *
QUICPacketProtectionKeyInfo::decryption_key(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_client_key[index];
  } else {
    return this->_server_key[index];
  }
}

uint8_t *
QUICPacketProtectionKeyInfo::decryption_key(QUICKeyPhase phase)
{
  return const_cast<uint8_t *>(const_cast<const QUICPacketProtectionKeyInfo *>(this)->decryption_key(phase));
}

size_t
QUICPacketProtectionKeyInfo::decryption_key_len(QUICKeyPhase phase) const
{
  const EVP_CIPHER *cipher;

  switch (phase) {
  case QUICKeyPhase::INITIAL:
    cipher = this->_cipher_initial;
    break;
  default:
    cipher = this->_cipher;
    break;
  }

  return EVP_CIPHER_key_length(cipher);
}

const uint8_t *
QUICPacketProtectionKeyInfo::decryption_iv(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_client_iv[index];
  } else {
    return this->_server_iv[index];
  }
}

uint8_t *
QUICPacketProtectionKeyInfo::decryption_iv(QUICKeyPhase phase)
{
  return const_cast<uint8_t *>(const_cast<const QUICPacketProtectionKeyInfo *>(this)->decryption_iv(phase));
}

const size_t *
QUICPacketProtectionKeyInfo::decryption_iv_len(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return &this->_client_iv_len[index];
  } else {
    return &this->_server_iv_len[index];
  }
}

size_t *
QUICPacketProtectionKeyInfo::decryption_iv_len(QUICKeyPhase phase)
{
  return const_cast<size_t *>(const_cast<const QUICPacketProtectionKeyInfo *>(this)->decryption_iv_len(phase));
}

void
QUICPacketProtectionKeyInfo::set_cipher_initial(const EVP_CIPHER *cipher)
{
  this->_cipher_initial = cipher;
}

void
QUICPacketProtectionKeyInfo::set_cipher(const EVP_CIPHER *cipher, size_t tag_len)
{
  this->_cipher  = cipher;
  this->_tag_len = tag_len;
}

const EVP_CIPHER *
QUICPacketProtectionKeyInfo::get_cipher_for_hp(QUICKeyPhase phase) const
{
  switch (phase) {
  case QUICKeyPhase::INITIAL:
    return this->_cipher_for_hp_initial;
  default:
    return this->_cipher_for_hp;
  }
}

void
QUICPacketProtectionKeyInfo::set_cipher_for_hp_initial(const EVP_CIPHER *cipher)
{
  this->_cipher_for_hp_initial = cipher;
}

void
QUICPacketProtectionKeyInfo::set_cipher_for_hp(const EVP_CIPHER *cipher)
{
  this->_cipher_for_hp = cipher;
}

const uint8_t *
QUICPacketProtectionKeyInfo::encryption_key_for_hp(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_server_key_for_hp[index];
  } else {
    return this->_client_key_for_hp[index];
  }
}

uint8_t *
QUICPacketProtectionKeyInfo::encryption_key_for_hp(QUICKeyPhase phase)
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_server_key_for_hp[index];
  } else {
    return this->_client_key_for_hp[index];
  }
}

size_t
QUICPacketProtectionKeyInfo::encryption_key_for_hp_len(QUICKeyPhase phase) const
{
  const EVP_CIPHER *cipher;

  switch (phase) {
  case QUICKeyPhase::INITIAL:
    cipher = this->_cipher_for_hp_initial;
    break;
  default:
    cipher = this->_cipher_for_hp;
    break;
  }

  return EVP_CIPHER_key_length(cipher);
}

const uint8_t *
QUICPacketProtectionKeyInfo::decryption_key_for_hp(QUICKeyPhase phase) const
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_client_key_for_hp[index];
  } else {
    return this->_server_key_for_hp[index];
  }
}

uint8_t *
QUICPacketProtectionKeyInfo::decryption_key_for_hp(QUICKeyPhase phase)
{
  int index = static_cast<int>(phase);
  if (this->_ctx == Context::SERVER) {
    return this->_client_key_for_hp[index];
  } else {
    return this->_server_key_for_hp[index];
  }
}

size_t
QUICPacketProtectionKeyInfo::decryption_key_for_hp_len(QUICKeyPhase phase) const
{
  const EVP_CIPHER *cipher;

  switch (phase) {
  case QUICKeyPhase::INITIAL:
    cipher = this->_cipher_for_hp_initial;
    break;
  default:
    cipher = this->_cipher_for_hp;
    break;
  }

  return EVP_CIPHER_key_length(cipher);
}
