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

#include "QUICPacketProtectionKeyInfo.h"
#include "QUICPacketHeaderProtector.h"
#include "QUICTLS.h"
#include "QUICGlobals.h"
#include "Mock.h"

struct PollCont;
#include "P_UDPConnection.h"
#include "P_UnixNet.h"
#include "P_UnixNetVConnection.h"

#include "./server_cert.h"

TEST_CASE("QUICPacketHeaderProtector")
{
  // Client
  SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);
#ifndef OPENSSL_IS_BORINGSSL
  SSL_CTX_clear_options(client_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
#endif
#ifdef SSL_MODE_QUIC_HACK
  SSL_CTX_set_mode(client_ssl_ctx, SSL_MODE_QUIC_HACK);
#endif

  // Server
  SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
#ifndef OPENSSL_IS_BORINGSSL
  SSL_CTX_clear_options(server_ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
#endif
#ifdef SSL_MODE_QUIC_HACK
  SSL_CTX_set_mode(server_ssl_ctx, SSL_MODE_QUIC_HACK);
#endif
  BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
  X509 *x509 = PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr);
  SSL_CTX_use_certificate(server_ssl_ctx, x509);
  BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
  EVP_PKEY *pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
  SSL_CTX_use_PrivateKey(server_ssl_ctx, pkey);
  SSL_CTX_set_alpn_select_cb(
    server_ssl_ctx,
    [](SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned inlen, void *) {
      auto ret = SSL_select_next_proto(const_cast<unsigned char **>(out), outlen,
                                       reinterpret_cast<const unsigned char *>("\6h3-foo"), 7, in, inlen);
      if (ret == OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_OK;
      } else {
        *out    = nullptr;
        *outlen = 0;
        return SSL_TLSEXT_ERR_NOACK;
      }
    },
    nullptr);

  SECTION("Long header", "[quic]")
  {
    uint8_t original[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00,                                           // Token Length (i), Token (*)
      0x19,                                           // Length (Not 0x09 because it will have 16 bytes of AEAD tag)
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0x11, 0x22, 0x33, 0x44, 0x55,                   // Payload (dummy)
    };
    uint8_t tmp[64];
    memcpy(tmp, original, sizeof(original));

    QUICPacketProtectionKeyInfo pp_key_info_client;
    QUICPacketProtectionKeyInfo pp_key_info_server;
    NetVCOptions netvc_options_client;
    NetVCOptions netvc_options_server;
    netvc_options_client.alpn_protos = "\6h3-foo";
    QUICHandshakeProtocol *client    = new QUICTLS(pp_key_info_client, client_ssl_ctx, NET_VCONNECTION_OUT, netvc_options_client);
    QUICHandshakeProtocol *server    = new QUICTLS(pp_key_info_server, server_ssl_ctx, NET_VCONNECTION_IN, netvc_options_server);

    CHECK(client->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8},
                                           QUIC_SUPPORTED_VERSIONS[0]));
    CHECK(server->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8},
                                           QUIC_SUPPORTED_VERSIONS[0]));

    QUICPacketHeaderProtector client_ph_protector(pp_key_info_client);
    QUICPacketHeaderProtector server_ph_protector(pp_key_info_server);

    // ## Client -> Server
    REQUIRE(client_ph_protector.protect(tmp, sizeof(tmp), 18));
    CHECK(memcmp(original, tmp, sizeof(original)) != 0);
    REQUIRE(server_ph_protector.unprotect(tmp, sizeof(tmp)));
    CHECK(memcmp(original, tmp, sizeof(original)) == 0);
    // ## Server -> Client
    REQUIRE(server_ph_protector.protect(tmp, sizeof(tmp), 18));
    CHECK(memcmp(original, tmp, sizeof(original)) != 0);
    REQUIRE(client_ph_protector.unprotect(tmp, sizeof(tmp)));
    CHECK(memcmp(original, tmp, sizeof(original)) == 0);

    delete client;
    delete server;
  }

  SECTION("Short header", "[quic]")
  {
    uint8_t original[] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
      0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    };
    uint8_t tmp[48];
    memcpy(tmp, original, sizeof(tmp));

    QUICPacketProtectionKeyInfo pp_key_info_client;
    QUICPacketProtectionKeyInfo pp_key_info_server;
    NetVCOptions netvc_options_client;
    NetVCOptions netvc_options_server;
    netvc_options_client.alpn_protos = "\6h3-foo";
    MockQUICConnection mock_client_connection;
    MockQUICConnection mock_server_connection;
    QUICHandshakeProtocol *client = new QUICTLS(pp_key_info_client, client_ssl_ctx, NET_VCONNECTION_OUT, netvc_options_client);
    QUICHandshakeProtocol *server = new QUICTLS(pp_key_info_server, server_ssl_ctx, NET_VCONNECTION_IN, netvc_options_server);
    SSL_set_ex_data(static_cast<QUICTLS *>(client)->ssl_handle(), QUIC::ssl_quic_qc_index, &mock_client_connection);
    SSL_set_ex_data(static_cast<QUICTLS *>(server)->ssl_handle(), QUIC::ssl_quic_qc_index, &mock_server_connection);

    auto client_tp = std::make_shared<QUICTransportParametersInClientHello>();
    auto server_tp = std::make_shared<QUICTransportParametersInEncryptedExtensions>();
    client_tp->set(QUICTransportParameterId::MAX_IDLE_TIMEOUT, 10);
    server_tp->set(QUICTransportParameterId::MAX_IDLE_TIMEOUT, 10);
    client->set_local_transport_parameters(client_tp);
    server->set_local_transport_parameters(server_tp);

    CHECK(client->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8},
                                           QUIC_SUPPORTED_VERSIONS[0]));
    CHECK(server->initialize_key_materials({reinterpret_cast<const uint8_t *>("\x83\x94\xc8\xf0\x3e\x51\x57\x00"), 8},
                                           QUIC_SUPPORTED_VERSIONS[0]));

    QUICPacketHeaderProtector client_ph_protector(pp_key_info_client);
    QUICPacketHeaderProtector server_ph_protector(pp_key_info_server);

    // Handshake
    // CH
    QUICHandshakeMsgs msg0;
    msg0.offsets[0] = 0;
    msg0.offsets[1] = 0;
    msg0.offsets[2] = 0;
    msg0.offsets[3] = 0;
    msg0.offsets[4] = 0;

    QUICHandshakeMsgs *msg1 = nullptr;
    REQUIRE(client->handshake(&msg1, &msg0) == 1);
    REQUIRE(msg1);

    // SH, EE, CERT, CV, FIN
    QUICHandshakeMsgs *msg2 = nullptr;
    REQUIRE(server->handshake(&msg2, msg1) == 1);
    REQUIRE(msg2);

    // FIN
    QUICHandshakeMsgs *msg3 = nullptr;

    // SH
    QUICHandshakeMsgs msg2_1;
    uint8_t msg2_1_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
    msg2_1.buf                                = msg2_1_buf;
    msg2_1.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

    memcpy(msg2_1.buf, msg2->buf, msg2->offsets[1]);
    msg2_1.offsets[0] = 0;
    msg2_1.offsets[1] = msg2->offsets[1];
    msg2_1.offsets[2] = msg2->offsets[1];
    msg2_1.offsets[3] = msg2->offsets[1];
    msg2_1.offsets[4] = msg2->offsets[1];

    // EE - FIN
    QUICHandshakeMsgs msg2_2;
    uint8_t msg2_2_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
    msg2_2.buf                                = msg2_2_buf;
    msg2_2.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

    size_t len = msg2->offsets[3] - msg2->offsets[2];
    memcpy(msg2_2.buf, msg2->buf + msg2->offsets[1], len);
    msg2_2.offsets[0] = 0;
    msg2_2.offsets[1] = 0;
    msg2_2.offsets[2] = 0;
    msg2_2.offsets[3] = len;
    msg2_2.offsets[4] = len;

    REQUIRE(client->handshake(&msg3, &msg2_1) == 1);
    REQUIRE(client->handshake(&msg3, &msg2_2) == 1);
    REQUIRE(msg3);

    // NS
    QUICHandshakeMsgs *msg4 = nullptr;
    REQUIRE(server->handshake(&msg4, msg3) == 1);
    REQUIRE(msg4);

    QUICHandshakeMsgs *msg5 = nullptr;
    REQUIRE(client->handshake(&msg5, msg4) == 1);
    REQUIRE(msg5 == nullptr);

    // ## Client -> Server
    REQUIRE(client_ph_protector.protect(tmp, sizeof(tmp), 18));
    CHECK(memcmp(original, tmp, sizeof(original)) != 0);
    REQUIRE(server_ph_protector.unprotect(tmp, sizeof(tmp)));
    CHECK(memcmp(original, tmp, sizeof(original)) == 0);
    // ## Server -> Client
    REQUIRE(server_ph_protector.protect(tmp, sizeof(tmp), 18));
    CHECK(memcmp(original, tmp, sizeof(original)) != 0);
    REQUIRE(client_ph_protector.unprotect(tmp, sizeof(tmp)));
    CHECK(memcmp(original, tmp, sizeof(original)) == 0);

    delete client;
    delete server;
  }

  SSL_CTX_free(client_ssl_ctx);
  SSL_CTX_free(server_ssl_ctx);
  BIO_free(crt_bio);
  BIO_free(key_bio);
  X509_free(x509);
  EVP_PKEY_free(pkey);
}
