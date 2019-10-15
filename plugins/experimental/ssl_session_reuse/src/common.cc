/** @file

  common.cc - Some common functions everyone needs

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

#include <cstdio>
#include <cstring>
#include <openssl/ssl.h>
#include <ts/ts.h>
#include <ts/apidefs.h>

#include "common.h"

const unsigned char salt[] = {115, 97, 108, 117, 0, 85, 137, 229};

std::string
hex_str(std::string str)
{
  std::string hex_str = "";
  for (char &c : str) {
    std::stringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(2) << (0xFF & static_cast<unsigned int>(c));
    hex_str += stream.str();
  }
  return hex_str;
}

int
encrypt_encode64(const unsigned char *key, int key_length, const unsigned char *in_data, int in_data_len, char *out_data,
                 size_t out_data_size, size_t *out_data_len)
{
  if (!key || !in_data || !out_data || !out_data_len) {
    return -1;
  }

  int cipher_block_size    = 0;
  unsigned char *encrypted = nullptr;
  int encrypted_len        = 0;
  int encrypted_len_extra  = 0;
  int ret                  = -1;

  // Initialize context
  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  unsigned char iv[EVP_MAX_IV_LENGTH];
  unsigned char gen_key[EVP_MAX_KEY_LENGTH];

  // generate key and iv
  if (EVP_BytesToKey(EVP_aes_256_cbc(), EVP_md5(), salt, key, key_length, 1, gen_key, iv) <= 0) {
    TSDebug(PLUGIN, "Error generating key.");
  }

  // Set context AES128 with the generated key and iv
  if (1 != EVP_EncryptInit_ex(context, EVP_aes_256_cbc(), nullptr, gen_key, iv)) {
    TSDebug(PLUGIN, "EVP_EncryptInit_ex failed.");
    goto Cleanup;
  }

  // https://www.openssl.org/docs/manmaster/man3/EVP_EncryptUpdate.html
  // EVP_EncryptUpdate() needs (inl + cipher_block_size - 1) bytes.
  // EVP_EncryptFinal_ex needs (inl + cipher_block_size) bytes.
  cipher_block_size = EVP_CIPHER_CTX_block_size(context);
  encrypted         = new unsigned char[in_data_len + cipher_block_size * 2];
  if (1 != EVP_EncryptUpdate(context, encrypted, &encrypted_len, in_data, in_data_len)) {
    TSDebug(PLUGIN, "EVP_EncryptUpdate failed.");
    goto Cleanup;
  }

  if (1 != EVP_EncryptFinal_ex(context, encrypted + encrypted_len, &encrypted_len_extra)) {
    TSDebug(PLUGIN, "EVP_EncryptFinal_ex failed.");
    goto Cleanup;
  }

  // We must encode it to base64 here, since the encryption doesn't guarantee that there are no
  // null bytes in the output. Which will cause a problem when sending it through redis since
  // the redis command needs to be formatted to a C string.
  if (TSBase64Encode(reinterpret_cast<char *>(encrypted), encrypted_len + encrypted_len_extra, out_data, out_data_size,
                     out_data_len) != 0) {
    TSDebug(PLUGIN, "Base 64 encoding failed.");
    goto Cleanup;
  }

  TSDebug(PLUGIN, "Encrypted buffer of size %d to buffer of size %lu.", in_data_len, *out_data_len);
  ret = 0;

Cleanup:

  if (encrypted) {
    delete[] encrypted;
  }

  if (context) {
    EVP_CIPHER_CTX_free(context);
  }

  return ret;
}

int
decrypt_decode64(const unsigned char *key, int key_length, const char *in_data, int in_data_len, unsigned char *out_data,
                 size_t out_data_size, size_t *out_data_len)
{
  if (!key || !in_data || !out_data || !out_data_len) {
    return -1;
  }

  size_t decoded_size     = DECODED_LEN(in_data_len);
  size_t decoded_len      = 0;
  unsigned char *decoded  = new unsigned char[decoded_size];
  int decrypted_len       = 0;
  int decrypted_len_extra = 0;
  int ret                 = -1;

  // Initialize context
  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  unsigned char iv[EVP_MAX_IV_LENGTH];
  unsigned char gen_key[EVP_MAX_KEY_LENGTH];

  // Decode base64
  std::memset(decoded, 0, decoded_size);
  if (TSBase64Decode(in_data, in_data_len, decoded, decoded_size, &decoded_len) != 0) {
    TSDebug(PLUGIN, "Base 64 decoding failed.");
    goto Cleanup;
  }

  // generate key and iv
  if (EVP_BytesToKey(EVP_aes_256_cbc(), EVP_md5(), salt, key, key_length, 1, gen_key, iv) <= 0) {
    TSDebug(PLUGIN, "Error generating key.");
  }
  // set context with the generated key and iv
  if (1 != EVP_DecryptInit_ex(context, EVP_aes_256_cbc(), nullptr, gen_key, iv)) {
    TSDebug(PLUGIN, "EVP_DecryptInit_ex failed.");
    goto Cleanup;
  }

  // https://www.openssl.org/docs/manmaster/man3/EVP_DecryptUpdate.html
  // EVP_DecryptUpdate() and EVP_DecryptFinal_ex() have the exact same requirements as their encrypt counterparts.
  if (1 != EVP_DecryptUpdate(context, out_data, &decrypted_len, decoded, decoded_len)) {
    TSDebug(PLUGIN, "EVP_DecryptUpdate failed.");
    goto Cleanup;
  }

  if (1 != EVP_DecryptFinal_ex(context, out_data + decrypted_len, &decrypted_len_extra)) {
    TSDebug(PLUGIN, "EVP_DecryptFinal_ex failed.");
    goto Cleanup;
  }

  *out_data_len = decrypted_len + decrypted_len_extra;

  TSDebug(PLUGIN, "Decrypted buffer of size %d to buffer of size %lu.", in_data_len, *out_data_len);
  ret = 0;

Cleanup:

  if (decoded) {
    delete[] decoded;
  }

  if (context) {
    EVP_CIPHER_CTX_free(context);
  }

  return ret;
}
