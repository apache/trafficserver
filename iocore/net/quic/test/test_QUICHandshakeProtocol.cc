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

#include "catch.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/base.h>
#endif

#include <openssl/ssl.h>

#include "Mock.h"
#include "QUICTLS.h"

static constexpr uint32_t MAX_HANDSHAKE_MSG_LEN = 2048;

#include "./server_cert.h"

void
print_hex(const uint8_t *v, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<uint32_t>(v[i]) << " ";

    if (i != 0 && (i + 1) % 32 == 0 && i != len - 1) {
      std::cout << std::endl;
    }
  }

  std::cout << std::endl;

  return;
}

TEST_CASE("QUICHndshakeProtocol Cleartext", "[quic]")
{
  // Client
  SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_clear_options(client_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
  QUICHandshakeProtocol *client = new QUICTLS(SSL_new(client_ssl_ctx), NET_VCONNECTION_OUT);

  // Server
  SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_clear_options(server_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
  BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
  SSL_CTX_use_certificate(server_ssl_ctx, PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr));
  BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
  SSL_CTX_use_PrivateKey(server_ssl_ctx, PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));
  QUICHandshakeProtocol *server = new QUICTLS(SSL_new(server_ssl_ctx), NET_VCONNECTION_IN);

  CHECK(client->initialize_key_materials(0x8394c8f03e515700));
  CHECK(server->initialize_key_materials(0x8394c8f03e515700));

  // encrypt - decrypt
  uint8_t original[] = {
    0x41, 0x70, 0x61, 0x63, 0x68, 0x65, 0x20, 0x54, 0x72, 0x61, 0x66, 0x66, 0x69, 0x63, 0x20, 0x53,
    0x65, 0x72, 0x76, 0x65, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  uint64_t pkt_num = 0x123456789;
  uint8_t ad[]     = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

  // client (encrypt) - server (decrypt)
  std::cout << "### Original Text" << std::endl;
  print_hex(original, sizeof(original));

  uint8_t cipher[128] = {0}; // >= original len + EVP_AEAD_max_overhead
  size_t cipher_len   = 0;
  CHECK(client->encrypt(cipher, cipher_len, sizeof(cipher), original, sizeof(original), pkt_num, ad, sizeof(ad),
                        QUICKeyPhase::CLEARTEXT));

  std::cout << "### Encrypted Text" << std::endl;
  print_hex(cipher, cipher_len);

  uint8_t plain[128] = {0};
  size_t plain_len   = 0;
  CHECK(server->decrypt(plain, plain_len, sizeof(plain), cipher, cipher_len, pkt_num, ad, sizeof(ad), QUICKeyPhase::CLEARTEXT));

  std::cout << "### Decrypted Text" << std::endl;
  print_hex(plain, plain_len);

  CHECK(sizeof(original) == (plain_len));
  CHECK(memcmp(original, plain, plain_len) == 0);

  // Teardown
  delete client;
  delete server;
}

TEST_CASE("QUICHandshakeProtocol 1-RTT", "[quic]")
{
  // Client
  SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_clear_options(client_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
  QUICHandshakeProtocol *client = new QUICTLS(SSL_new(client_ssl_ctx), NET_VCONNECTION_OUT);

  // Server
  SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_clear_options(server_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
  BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
  SSL_CTX_use_certificate(server_ssl_ctx, PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr));
  BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
  SSL_CTX_use_PrivateKey(server_ssl_ctx, PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));
  QUICHandshakeProtocol *server = new QUICTLS(SSL_new(server_ssl_ctx), NET_VCONNECTION_IN);

  CHECK(client->initialize_key_materials(0x8394c8f03e515708));
  CHECK(server->initialize_key_materials(0x8394c8f03e515708));

  // Client Hello
  uint8_t client_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t client_hello_len                     = 0;
  CHECK(client->handshake(client_hello, client_hello_len, MAX_HANDSHAKE_MSG_LEN, nullptr, 0) == SSL_ERROR_WANT_READ);
  std::cout << "### Client Hello" << std::endl;
  print_hex(client_hello, client_hello_len);

  // Server Hello
  uint8_t server_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t server_hello_len                     = 0;
  CHECK(server->handshake(server_hello, server_hello_len, MAX_HANDSHAKE_MSG_LEN, client_hello, client_hello_len) ==
        SSL_ERROR_WANT_READ);
  std::cout << "### Server Hello" << std::endl;
  print_hex(server_hello, server_hello_len);

  // Client Fnished
  uint8_t client_finished[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t client_finished_len                     = 0;
  CHECK(client->handshake(client_finished, client_finished_len, MAX_HANDSHAKE_MSG_LEN, server_hello, server_hello_len) ==
        SSL_ERROR_NONE);
  std::cout << "### Client Finished" << std::endl;
  print_hex(client_finished, client_finished_len);

  CHECK(client->update_key_materials());

  // Post Handshake Msg
  uint8_t post_handshake_msg[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t post_handshake_msg_len                     = 0;
  CHECK(server->handshake(post_handshake_msg, post_handshake_msg_len, MAX_HANDSHAKE_MSG_LEN, client_finished,
                          client_finished_len) == SSL_ERROR_NONE);
  std::cout << "### Post Handshake Message" << std::endl;
  print_hex(post_handshake_msg, post_handshake_msg_len);

  CHECK(server->update_key_materials());

  // encrypt - decrypt
  uint8_t original[] = {
    0x41, 0x70, 0x61, 0x63, 0x68, 0x65, 0x20, 0x54, 0x72, 0x61, 0x66, 0x66, 0x69, 0x63, 0x20, 0x53,
    0x65, 0x72, 0x76, 0x65, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  uint64_t pkt_num = 0x123456789;
  uint8_t ad[]     = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

  // client (encrypt) - server (decrypt)
  std::cout << "### Original Text" << std::endl;
  print_hex(original, sizeof(original));

  uint8_t cipher[128] = {0}; // >= original len + EVP_AEAD_max_overhead
  size_t cipher_len   = 0;
  CHECK(client->encrypt(cipher, cipher_len, sizeof(cipher), original, sizeof(original), pkt_num, ad, sizeof(ad),
                        QUICKeyPhase::PHASE_0));

  std::cout << "### Encrypted Text" << std::endl;
  print_hex(cipher, cipher_len);

  uint8_t plain[128] = {0};
  size_t plain_len   = 0;
  CHECK(server->decrypt(plain, plain_len, sizeof(plain), cipher, cipher_len, pkt_num, ad, sizeof(ad), QUICKeyPhase::PHASE_0));

  std::cout << "### Decrypted Text" << std::endl;
  print_hex(plain, plain_len);

  CHECK(sizeof(original) == (plain_len));
  CHECK(memcmp(original, plain, plain_len) == 0);

  // Teardown
  delete client;
  delete server;
}
