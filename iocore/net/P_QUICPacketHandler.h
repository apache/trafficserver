/** @file

  A brief file description

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

#pragma once

#include "ts/ink_platform.h"
#include "P_Connection.h"
#include "P_NetAccept.h"
#include "quic/QUICTypes.h"

class QUICNetVConnection;
class QUICPacket;

struct QUICPacketHandler : public NetAccept {
public:
  QUICPacketHandler(const NetProcessor::AcceptOptions &opt, SSL_CTX *);
  virtual ~QUICPacketHandler();

  virtual NetProcessor *getNetProcessor() const override;
  virtual NetAccept *clone() const override;
  virtual int acceptEvent(int event, void *e) override;
  void init_accept(EThread *t) override;
  void send_packet(const QUICPacket &packet, QUICNetVConnection *vc);
  void send_packet(const QUICPacket &packet, UDPConnection *udp_con, IpEndpoint &addr, uint32_t pmtu);

private:
  void _recv_packet(int event, UDPPacket *udpPacket);
  bool _read_connection_id(QUICConnectionId &cid, IOBufferBlock *block);

  Map<int64_t, QUICNetVConnection *> _connections;
  SSL_CTX *_ssl_ctx;
};
