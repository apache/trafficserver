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

#include "Mock.h"
#include "QUICHandshake.h"
#include "QUICConfig.h"

#include "./server_cert.h"

TEST_CASE("1-RTT handshake ", "[quic]")
{
  QUICConfig::startup();

  // setup client
  QUICConnection *client_qc = new MockQUICConnection(NET_VCONNECTION_OUT);

  SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);

  QUICConnectionId client_conn_id = 0x12345;

  QUICHandshake *client = new QUICHandshake(client_qc, client_ssl_ctx);

  // setup server
  QUICConnection *server_qc = new MockQUICConnection(NET_VCONNECTION_IN);

  SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
  SSL_CTX_use_certificate(server_ssl_ctx, PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr));
  BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
  SSL_CTX_use_PrivateKey(server_ssl_ctx, PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));

  QUICConnectionId conn_id = 0;
  QUICStatelessResetToken server_token;
  server_token.generate(conn_id, 0);

  QUICHandshake *server = new QUICHandshake(server_qc, server_ssl_ctx, server_token);

  // setup stream 0
  MockQUICFrameTransmitter tx;
  QUICStream *stream          = new MockQUICStream();
  MockQUICStreamIO *stream_io = new MockQUICStreamIO(nullptr, stream);

  client->set_stream(stream, stream_io);
  server->set_stream(stream, stream_io);

  SECTION("Basic Full Handshake")
  {
    // ClientHello
    client->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    CHECK(stream_io->transfer() > 0);
    client->handleEvent(QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE, nullptr);

    // ServerHello
    server->handleEvent(VC_EVENT_READ_READY, nullptr);
    CHECK(stream_io->transfer() > 0);

    client->handleEvent(VC_EVENT_READ_READY, nullptr);
    CHECK(stream_io->transfer() > 0);
    client->handleEvent(QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE, nullptr);

    // Finished
    server->handleEvent(VC_EVENT_READ_READY, nullptr);

    CHECK(client->is_completed());
    CHECK(server->is_completed());
  }

  delete stream_io;
}
