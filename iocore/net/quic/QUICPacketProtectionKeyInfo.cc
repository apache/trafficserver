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

const QUIC_EVP_CIPHER *
QUICPacketProtectionKeyInfo::get_cipher_for_hp(QUICEncryptionLevel level) const
{
  switch (level) {
  case QUICEncryptionLevel::INITIAL:
    return this->_cipher_for_hp_initial;
  default:
    return this->_cipher_for_hp;
  }
}

void
QUICPacketProtectionKeyInfo::set_cipher_for_hp_initial(const QUIC_EVP_CIPHER *cipher)
{
  this->_cipher_for_hp_initial = cipher;
}

void
QUICPacketProtectionKeyInfo::set_cipher_for_hp(const QUIC_EVP_CIPHER *cipher)
{
  this->_cipher_for_hp = cipher;
}

const uint8_t *
QUICPacketProtectionKeyInfo::encryption_key_for_hp(QUICEncryptionLevel level) const
{
  int index = static_cast<int>(level);
  if (this->_ctx == Context::SERVER) {
    return this->_server_key_for_hp[index];
  } else {
    return this->_client_key_for_hp[index];
  }
}

uint8_t *
QUICPacketProtectionKeyInfo::encryption_key_for_hp(QUICEncryptionLevel level)
{
  int index = static_cast<int>(level);
  if (this->_ctx == Context::SERVER) {
    return this->_server_key_for_hp[index];
  } else {
    return this->_client_key_for_hp[index];
  }
}

const uint8_t *
QUICPacketProtectionKeyInfo::decryption_key_for_hp(QUICEncryptionLevel level) const
{
  int index = static_cast<int>(level);
  if (this->_ctx == Context::SERVER) {
    return this->_client_key_for_hp[index];
  } else {
    return this->_server_key_for_hp[index];
  }
}

uint8_t *
QUICPacketProtectionKeyInfo::decryption_key_for_hp(QUICEncryptionLevel level)
{
  int index = static_cast<int>(level);
  if (this->_ctx == Context::SERVER) {
    return this->_client_key_for_hp[index];
  } else {
    return this->_server_key_for_hp[index];
  }
}

void
QUICPacketProtectionKeyInfo::decryption_key_for_pp(QUICEncryptionLevel level) const
{
}
