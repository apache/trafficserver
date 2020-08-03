/** @file

  session_process.cc - encrypt and decrypt sessions

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

#include <cmath>
#include <cstring>
#include <cerrno>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <ts/ts.h>

#include "session_process.h"
#include "ssl_utils.h"
#include "common.h"

const uint64_t protocol_version = 2;

int
encrypt_session(const char *session_data, int32_t session_data_len, const unsigned char *key, int key_length,
                std::string &encrypted_data)
{
  if (!key || !session_data) {
    return -1;
  }

  int data_len          = sizeof(int64_t) + sizeof(int32_t) + session_data_len;
  unsigned char *data   = new unsigned char[data_len];
  int offset            = 0;
  size_t encrypted_size = ENCODED_LEN(data_len + EVP_MAX_BLOCK_LENGTH * 2);
  size_t encrypted_len  = 0;
  char *encrypted       = new char[encrypted_size];
  int ret               = 0;

  // Transition the expiration time into a protocol version field.
  // Keeping it unnecessarily long at 64 bits to have consistency with previous version
  // Version 1, had a fixed expiration time of 2*3600 seconds
  std::memcpy(data + offset, &protocol_version, sizeof(int64_t));
  offset += sizeof(int64_t);
  std::memcpy(data + offset, &session_data_len, sizeof(int32_t));
  offset += sizeof(session_data_len);
  std::memcpy(data + offset, session_data, session_data_len);

  std::memset(encrypted, 0, encrypted_size);
  ret = encrypt_encode64(key, key_length, data, data_len, encrypted, encrypted_size, &encrypted_len);
  if (ret == 0) {
    encrypted_data.assign(encrypted, encrypted_len);
  } else {
    TSDebug(PLUGIN, "encrypt_session calling encrypt_encode64 failed, error: %d", ret);
  }

  delete[] data;
  delete[] encrypted;

  return ret;
}

/**
 * The initial value of session_data_len is the number of bytes in session_data
 * The return value of session_data_len is the number of bytes actually stored in session_data
 * The return value is -1 on error or the number of bytes in the decrypted session data (may be more
 * than the initial value of sesion_data_len
 */
int
decrypt_session(const std::string &encrypted_data, const unsigned char *key, int key_length, char *session_data,
                int32_t &session_data_len)
{
  if (!key || !session_data) {
    return -1;
  }

  unsigned char *ssl_sess_ptr = nullptr;
  size_t decrypted_size       = DECODED_LEN(encrypted_data.length()) + EVP_MAX_BLOCK_LENGTH * 2;
  size_t decrypted_len        = 0;
  unsigned char *decrypted    = new unsigned char[decrypted_size];
  int ret                     = -1;
  size_t len_all              = 0;

  std::memset(decrypted, 0, decrypted_size);
  if ((ret = decrypt_decode64(key, key_length, encrypted_data.c_str(), encrypted_data.length(), decrypted, decrypted_size,
                              &decrypted_len)) != 0) {
    TSDebug(PLUGIN, "decrypt_session calling decrypt_decode64 failed, error: %d", ret);
    goto Cleanup;
  }

  // Retrieve ssl_session
  ssl_sess_ptr = decrypted;

  // The first 64 bits are now the protocol version.  Make sure it matches what we expect
  if (protocol_version == *(reinterpret_cast<uint64_t *>(ssl_sess_ptr))) {
    // Move beyond the protocol version
    ssl_sess_ptr += sizeof(int64_t);

    // Length
    ret = *reinterpret_cast<int32_t *>(ssl_sess_ptr);
    ssl_sess_ptr += sizeof(int32_t);

    len_all = ret + sizeof(int64_t) + sizeof(int32_t);
    if (decrypted_len < len_all) {
      TSDebug(PLUGIN, "Session data length mismatch, got %lu, should be %lu.", decrypted_len, len_all);
      ret = -1;
      goto Cleanup;
    }

    // If there is less data than the maxiumum buffer size, reduce accordingly
    if (ret < session_data_len) {
      session_data_len = ret;
    }
    std::memcpy(session_data, ssl_sess_ptr, session_data_len);
  }

Cleanup:

  delete[] decrypted;

  return ret;
}

int
encode_id(const char *id, int idlen, std::string &encoded_data)
{
  char *encoded = new char[ENCODED_LEN(idlen)];
  memset(encoded, 0, ENCODED_LEN(idlen));
  size_t encoded_len = 0;
  if (TSBase64Encode(id, idlen, encoded, ENCODED_LEN(idlen), &encoded_len) != 0) {
    TSError("ID base 64 encoding failed.");
    if (encoded) {
      delete[] encoded;
    }
    return -1;
  }

  encoded_data.assign(encoded, encoded_len);

  if (encoded) {
    delete[] encoded;
  }

  return 0;
}

int
decode_id(const std::string &encoded_id, char *decoded_data, int &decoded_data_len)
{
  size_t decode_len = 0;
  memset(decoded_data, 0, decoded_data_len);
  if (TSBase64Decode(static_cast<const char *>(encoded_id.c_str()), encoded_id.length(),
                     reinterpret_cast<unsigned char *>(decoded_data), decoded_data_len, &decode_len) != 0) {
    TSError("ID base 64 decoding failed.");
    return -1;
  }
  decoded_data_len = decode_len;
  return 0;
}

int
add_session(char *session_id, int session_id_len, const std::string &encrypted_session)
{
  std::string session(session_id, session_id_len);
  TSDebug(PLUGIN, "add_session session_id: %s", hex_str(session).c_str());
  char session_data[SSL_SESSION_MAX_DER];
  int32_t session_data_len = SSL_SESSION_MAX_DER;
  int ret = decrypt_session(encrypted_session, (unsigned char *)get_key_ptr(), get_key_length(), session_data, session_data_len);
  if (ret < 0) {
    TSError("Failed to decrypt session %.*s, error: %d", session_id_len, hex_str(session).c_str(), ret);
    return ret;
  }
  const unsigned char *loc = reinterpret_cast<const unsigned char *>(session_data);
  SSL_SESSION *sess        = d2i_SSL_SESSION(nullptr, &loc, session_data_len);
  if (nullptr == sess) {
    TSError("Failed to transform session buffer %.*s", session_id_len, hex_str(session).c_str());
    return -1;
  }
  TSSslSessionID sid;
  memcpy(reinterpret_cast<char *>(sid.bytes), session_id, session_id_len);
  sid.len = session_id_len;
  if (sid.len > sizeof(sid.bytes)) {
    sid.len = sizeof(sid.bytes);
  }
  TSSslSessionInsert(&sid, reinterpret_cast<TSSslSession>(sess), nullptr);
  // Free the sesison object created by d2i_SSL_SESSION
  // We should make an API that just takes the ASN buffer
  SSL_SESSION_free(sess);
  return 0;
}
