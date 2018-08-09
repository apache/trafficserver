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

// #include "Mock.h"
#include "QUICTLS.h"

// depends on size of cert
static constexpr uint32_t MAX_HANDSHAKE_MSG_LEN = 8192;

#include "./server_cert.h"

static void
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

static const uint8_t original[] = {
  0x41, 0x70, 0x61, 0x63, 0x68, 0x65, 0x20, 0x54, 0x72, 0x61, 0x66, 0x66, 0x69, 0x63, 0x20, 0x53,
  0x65, 0x72, 0x76, 0x65, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint64_t pkt_num = 0x123456789;
static const uint8_t ad[]     = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

// /* Fixed value used in the ServerHello random field to identify an HRR */
// const unsigned char hrr_random[] = {
//   0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
//   0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e, 0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c,
// };

// static const bool
// is_hrr(uint8_t *msg, size_t msg_len)
// {
//   return memmem(msg, msg_len, hrr_random, sizeof(hrr_random)) != nullptr;
// }

// // dummy token to simplify test
// static uint8_t token[] = {0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef,
//                           0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef,
//                           0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef,
//                           0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef};

// static int
// generate_cookie_callback(SSL * /* ssl */, unsigned char *cookie, size_t *cookie_len)
// {
//   memcpy(cookie, token, sizeof(token));
//   *cookie_len = sizeof(token);

//   return 1;
// }

// static int
// verify_cookie_callback(SSL *ssl, const unsigned char *cookie, size_t cookie_len)
// {
//   if (memcmp(token, cookie, sizeof(token)) == 0) {
//     return 1;
//   } else {
//     return 0;
//   }
// }

// TEST_CASE("QUICHndshakeProtocol Cleartext", "[quic]")
// {
//   // Client
//   SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
//   SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_clear_options(client_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
//   QUICHandshakeProtocol *client = new QUICTLS(SSL_new(client_ssl_ctx), NET_VCONNECTION_OUT);

//   // Server
//   SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
//   SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_clear_options(server_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
//   BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
//   SSL_CTX_use_certificate(server_ssl_ctx, PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr));
//   BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
//   SSL_CTX_use_PrivateKey(server_ssl_ctx, PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));
//   QUICHandshakeProtocol *server = new QUICTLS(SSL_new(server_ssl_ctx), NET_VCONNECTION_IN);

//   CHECK(client->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8}));
//   CHECK(server->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8}));

//   // encrypt - decrypt
//   // client (encrypt) - server (decrypt)
//   std::cout << "### Original Text" << std::endl;
//   print_hex(original, sizeof(original));

//   uint8_t cipher[128] = {0}; // >= original len + EVP_AEAD_max_overhead
//   size_t cipher_len   = 0;
//   CHECK(client->encrypt(cipher, cipher_len, sizeof(cipher), original, sizeof(original), pkt_num, ad, sizeof(ad),
//                         QUICKeyPhase::INITIAL));

//   std::cout << "### Encrypted Text" << std::endl;
//   print_hex(cipher, cipher_len);

//   uint8_t plain[128] = {0};
//   size_t plain_len   = 0;
//   CHECK(server->decrypt(plain, plain_len, sizeof(plain), cipher, cipher_len, pkt_num, ad, sizeof(ad), QUICKeyPhase::INITIAL));

//   std::cout << "### Decrypted Text" << std::endl;
//   print_hex(plain, plain_len);

//   CHECK(sizeof(original) == (plain_len));
//   CHECK(memcmp(original, plain, plain_len) == 0);

//   // Teardown
//   delete client;
//   delete server;
// }

TEST_CASE("QUICHandshakeProtocol Full Handshake", "[quic]")
{
  // Client
  SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_clear_options(client_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
#ifdef SSL_MODE_QUIC_HACK
  SSL_CTX_set_mode(client_ssl_ctx, SSL_MODE_QUIC_HACK);
#endif
  QUICHandshakeProtocol *client = new QUICTLS(SSL_new(client_ssl_ctx), NET_VCONNECTION_OUT);

  // Server
  SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_clear_options(server_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
#ifdef SSL_MODE_QUIC_HACK
  SSL_CTX_set_mode(server_ssl_ctx, SSL_MODE_QUIC_HACK);
#endif
  BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
  X509 *x509 = PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr);
  SSL_CTX_use_certificate(server_ssl_ctx, x509);
  BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
  EVP_PKEY *pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
  SSL_CTX_use_PrivateKey(server_ssl_ctx, pkey);
  QUICHandshakeProtocol *server = new QUICTLS(SSL_new(server_ssl_ctx), NET_VCONNECTION_IN);

  BIO_free(crt_bio);
  BIO_free(key_bio);

  X509_free(x509);
  EVP_PKEY_free(pkey);

  CHECK(client->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8}));
  CHECK(server->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8}));

  // CH
  QUICHandshakeMsgs msg1;
  uint8_t msg1_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
  msg1.buf                                = msg1_buf;
  msg1.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

  REQUIRE(client->handshake(&msg1, nullptr) == 1);
  std::cout << "### Messages from client" << std::endl;
  print_hex(msg1.buf, msg1.offsets[4]);

  // SH, EE, CERT, CV, FIN
  QUICHandshakeMsgs msg2;
  uint8_t msg2_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
  msg2.buf                                = msg2_buf;
  msg2.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

  REQUIRE(server->handshake(&msg2, &msg1) == 1);
  std::cout << "### Messages from server" << std::endl;
  print_hex(msg2.buf, msg2.offsets[4]);

  // FIN
  QUICHandshakeMsgs msg3;
  uint8_t msg3_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
  msg3.buf                                = msg3_buf;
  msg3.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

#ifdef SSL_MODE_QUIC_HACK
  // -- Hacks for OpenSSL with SSL_MODE_QUIC_HACK --
  // SH
  QUICHandshakeMsgs msg2_1;
  uint8_t msg2_1_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
  msg2_1.buf                                = msg2_1_buf;
  msg2_1.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

  memcpy(msg2_1.buf, msg2.buf, msg2.offsets[1]);
  msg2_1.offsets[0] = 0;
  msg2_1.offsets[1] = msg2.offsets[1];
  msg2_1.offsets[2] = msg2.offsets[1];
  msg2_1.offsets[3] = msg2.offsets[1];
  msg2_1.offsets[4] = msg2.offsets[1];

  // EE - FIN
  QUICHandshakeMsgs msg2_2;
  uint8_t msg2_2_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
  msg2_2.buf                                = msg2_2_buf;
  msg2_2.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

  size_t len = msg2.offsets[3] - msg2.offsets[2];
  memcpy(msg2_2.buf, msg2.buf + msg2.offsets[1], len);
  msg2_2.offsets[0] = 0;
  msg2_2.offsets[1] = 0;
  msg2_2.offsets[2] = 0;
  msg2_2.offsets[3] = len;
  msg2_2.offsets[4] = len;

  REQUIRE(client->handshake(&msg3, &msg2_1) == 1);
  REQUIRE(client->handshake(&msg3, &msg2_2) == 1);
#else
  REQUIRE(client->handshake(&msg3, &msg2) == 1);
#endif
  std::cout << "### Messages from client" << std::endl;
  print_hex(msg3.buf, msg3.offsets[4]);

  // NS
  QUICHandshakeMsgs msg4;
  uint8_t msg4_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
  msg4.buf                                = msg4_buf;
  msg4.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

  REQUIRE(server->handshake(&msg4, &msg3) == 1);
  std::cout << "### Messages from server" << std::endl;
  print_hex(msg4.buf, msg4.offsets[4]);

  // encrypt - decrypt
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

  SSL_CTX_free(server_ssl_ctx);
  SSL_CTX_free(client_ssl_ctx);
}

// // HRR - Incorrect DHE Share
// // NOTE: This is *NOT* client address validation.
// //       https://tools.ietf.org/html/draft-ietf-tls-tls13-26 - 2.1.  Incorrect DHE Share
// TEST_CASE("QUICHandshakeProtocol 1-RTT HRR key_share mismatch", "[quic]")
// {
//   // Client
//   SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
//   SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_clear_options(client_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
//
//   QUICHandshakeProtocol *client = new QUICTLS(SSL_new(client_ssl_ctx), NET_VCONNECTION_OUT);
//
//   // Server
//   SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
//   SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_clear_options(server_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
//   BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
//   SSL_CTX_use_certificate(server_ssl_ctx, PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr));
//   BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
//   SSL_CTX_use_PrivateKey(server_ssl_ctx, PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));
//
//   // client key_share will be X25519 (default of OpenSSL)
//   if (SSL_CTX_set1_groups_list(server_ssl_ctx, "P-521:P-384:P-256") != 1) {
//     REQUIRE(false);
//   }
//
//   QUICHandshakeProtocol *server = new QUICTLS(SSL_new(server_ssl_ctx), NET_VCONNECTION_IN);
//
//   CHECK(client->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8}));
//   CHECK(server->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8}));
//
//   // Client Hello
//   uint8_t client_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t client_hello_len                     = 0;
//   REQUIRE(client->handshake(client_hello, client_hello_len, MAX_HANDSHAKE_MSG_LEN, nullptr, 0) == SSL_ERROR_WANT_READ);
//   REQUIRE(client_hello_len > 0);
//   std::cout << "### Client Hello" << std::endl;
//   print_hex(client_hello, client_hello_len);
//
//   // Hello Retry Request w/o cookie
//   uint8_t retry[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t retry_len                     = 0;
//   REQUIRE(server->handshake(retry, retry_len, MAX_HANDSHAKE_MSG_LEN, client_hello, client_hello_len) == SSL_ERROR_WANT_READ);
//   REQUIRE(retry_len > 0);
//   REQUIRE(is_hrr(retry, retry_len));
//   std::cout << "### HRR" << std::endl;
//   print_hex(retry, retry_len);
//
//   // Client Hello w/ cookie
//   memset(client_hello, 0, MAX_HANDSHAKE_MSG_LEN);
//   client_hello_len = 0;
//   REQUIRE(client->handshake(client_hello, client_hello_len, MAX_HANDSHAKE_MSG_LEN, retry, retry_len) == SSL_ERROR_WANT_READ);
//   REQUIRE(client_hello_len > 0);
//   std::cout << "### Client Hello" << std::endl;
//   print_hex(client_hello, client_hello_len);
//
//   // Server Hello
//   uint8_t server_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t server_hello_len                     = 0;
//   REQUIRE(server->handshake(server_hello, server_hello_len, MAX_HANDSHAKE_MSG_LEN, client_hello, client_hello_len) ==
//           SSL_ERROR_WANT_READ);
//   REQUIRE(server_hello_len > 0);
//   std::cout << "### Server Hello" << std::endl;
//   print_hex(server_hello, server_hello_len);
//
//   // Client Fnished
//   uint8_t client_finished[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t client_finished_len                     = 0;
//   REQUIRE(client->handshake(client_finished, client_finished_len, MAX_HANDSHAKE_MSG_LEN, server_hello, server_hello_len) ==
//           SSL_ERROR_NONE);
//   REQUIRE(client_finished_len > 0);
//   std::cout << "### Client Finished" << std::endl;
//   print_hex(client_finished, client_finished_len);
//
//   CHECK(client->update_key_materials());
//
//   // Post Handshake Msg
//   uint8_t post_handshake_msg[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t post_handshake_msg_len                     = 0;
//   REQUIRE(server->handshake(post_handshake_msg, post_handshake_msg_len, MAX_HANDSHAKE_MSG_LEN, client_finished,
//                             client_finished_len) == SSL_ERROR_NONE);
//   std::cout << "### Post Handshake Message" << std::endl;
//   print_hex(post_handshake_msg, post_handshake_msg_len);
//
//   CHECK(server->update_key_materials());
//
//   // encrypt - decrypt
//   // client (encrypt) - server (decrypt)
//   std::cout << "### Original Text" << std::endl;
//   print_hex(original, sizeof(original));
//
//   uint8_t cipher[128] = {0}; // >= original len + EVP_AEAD_max_overhead
//   size_t cipher_len   = 0;
//   CHECK(client->encrypt(cipher, cipher_len, sizeof(cipher), original, sizeof(original), pkt_num, ad, sizeof(ad),
//                         QUICKeyPhase::PHASE_0));
//
//   std::cout << "### Encrypted Text" << std::endl;
//   print_hex(cipher, cipher_len);
//
//   uint8_t plain[128] = {0};
//   size_t plain_len   = 0;
//   CHECK(server->decrypt(plain, plain_len, sizeof(plain), cipher, cipher_len, pkt_num, ad, sizeof(ad), QUICKeyPhase::PHASE_0));
//
//   std::cout << "### Decrypted Text" << std::endl;
//   print_hex(plain, plain_len);
//
//   CHECK(sizeof(original) == (plain_len));
//   CHECK(memcmp(original, plain, plain_len) == 0);
//
//   // Teardown
//   delete client;
//   delete server;
// }
//
//
// TEST_CASE("QUICHandshakeProtocol PNE", "[quic]")
// {
//   // Client
//   SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
//   SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_clear_options(client_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
//   QUICHandshakeProtocol *client = new QUICTLS(SSL_new(client_ssl_ctx), NET_VCONNECTION_OUT);
//
//   // Server
//   SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
//   SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
//   SSL_CTX_clear_options(server_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
//   BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
//   SSL_CTX_use_certificate(server_ssl_ctx, PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr));
//   BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
//   SSL_CTX_use_PrivateKey(server_ssl_ctx, PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));
//   QUICHandshakeProtocol *server = new QUICTLS(SSL_new(server_ssl_ctx), NET_VCONNECTION_IN);
//
//   uint8_t expected[] = {0x01, 0x02, 0x03, 0x04, 0x05};
//   uint8_t sample[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
//   uint8_t protected_pn[18], unprotected_pn[18];
//   uint8_t protected_pn_len = 0, unprotected_pn_len = 0;
//
//   // # Before handshake
//   CHECK(client->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8}));
//   CHECK(server->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8}));
//
//   // ## Client -> Server
//   client->encrypt_pn(protected_pn, protected_pn_len, expected, sizeof(expected), sample, QUICKeyPhase::INITIAL);
//   server->decrypt_pn(unprotected_pn, unprotected_pn_len, protected_pn, protected_pn_len, sample, QUICKeyPhase::INITIAL);
//   CHECK(unprotected_pn_len == sizeof(expected));
//   CHECK(memcmp(unprotected_pn, expected, sizeof(expected)) == 0);
//   // ## Server -> Client
//   server->encrypt_pn(protected_pn, protected_pn_len, expected, sizeof(expected), sample, QUICKeyPhase::INITIAL);
//   client->decrypt_pn(unprotected_pn, unprotected_pn_len, protected_pn, protected_pn_len, sample, QUICKeyPhase::INITIAL);
//   CHECK(unprotected_pn_len == sizeof(expected));
//   CHECK(memcmp(unprotected_pn, expected, sizeof(expected)) == 0);
//
//   // # After handshake
//   uint8_t client_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t client_hello_len                     = 0;
//   CHECK(client->handshake(client_hello, client_hello_len, MAX_HANDSHAKE_MSG_LEN, nullptr, 0) == SSL_ERROR_WANT_READ);
//   std::cout << "### Client Hello" << std::endl;
//   print_hex(client_hello, client_hello_len);
//
//   // Server Hello
//   uint8_t server_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t server_hello_len                     = 0;
//   CHECK(server->handshake(server_hello, server_hello_len, MAX_HANDSHAKE_MSG_LEN, client_hello, client_hello_len) ==
//         SSL_ERROR_WANT_READ);
//   std::cout << "### Server Hello" << std::endl;
//   print_hex(server_hello, server_hello_len);
//
//   // Client Fnished
//   uint8_t client_finished[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t client_finished_len                     = 0;
//   CHECK(client->handshake(client_finished, client_finished_len, MAX_HANDSHAKE_MSG_LEN, server_hello, server_hello_len) ==
//         SSL_ERROR_NONE);
//   std::cout << "### Client Finished" << std::endl;
//   print_hex(client_finished, client_finished_len);
//
//   CHECK(client->update_key_materials());
//
//   // Post Handshake Msg
//   uint8_t post_handshake_msg[MAX_HANDSHAKE_MSG_LEN] = {0};
//   size_t post_handshake_msg_len                     = 0;
//   CHECK(server->handshake(post_handshake_msg, post_handshake_msg_len, MAX_HANDSHAKE_MSG_LEN, client_finished,
//                           client_finished_len) == SSL_ERROR_NONE);
//   std::cout << "### Post Handshake Message" << std::endl;
//   print_hex(post_handshake_msg, post_handshake_msg_len);
//
//   CHECK(server->update_key_materials());
//
//   // ## Client -> Server
//   client->encrypt_pn(protected_pn, protected_pn_len, expected, sizeof(expected), sample, QUICKeyPhase::PHASE_0);
//   server->decrypt_pn(unprotected_pn, unprotected_pn_len, protected_pn, protected_pn_len, sample, QUICKeyPhase::PHASE_0);
//   CHECK(unprotected_pn_len == sizeof(expected));
//   CHECK(memcmp(unprotected_pn, expected, sizeof(expected)) == 0);
//   // ## Server -> Client
//   server->encrypt_pn(protected_pn, protected_pn_len, expected, sizeof(expected), sample, QUICKeyPhase::PHASE_0);
//   client->decrypt_pn(unprotected_pn, unprotected_pn_len, protected_pn, protected_pn_len, sample, QUICKeyPhase::PHASE_0);
//   CHECK(unprotected_pn_len == sizeof(expected));
//   CHECK(memcmp(unprotected_pn, expected, sizeof(expected)) == 0);
// }
