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
#include "quic/QUICConnectionTable.h"

class QUICClosedConCollector;
class QUICNetVConnection;
class QUICPacket;

class QUICPacketHandler
{
public:
  QUICPacketHandler();
  ~QUICPacketHandler();

  virtual void send_packet(const QUICPacket &packet, QUICNetVConnection *vc) = 0;
  virtual void close_conenction(QUICNetVConnection *conn);

protected:
  static void _send_packet(Continuation *c, const QUICPacket &packet, UDPConnection *udp_con, IpEndpoint &addr, uint32_t pmtu);

  Event *_collector_event                       = nullptr;
  QUICClosedConCollector *_closed_con_collector = nullptr;

  virtual void _recv_packet(int event, UDPPacket *udpPacket) = 0;
};

/*
 * @class QUICPacketHanderIn
 * @brief QUIC Packet Handler for incoming connections
 */
class QUICPacketHandlerIn : public NetAccept, public QUICPacketHandler
{
public:
  QUICPacketHandlerIn(const NetProcessor::AcceptOptions &opt);
  ~QUICPacketHandlerIn();

  // NetAccept
  virtual NetProcessor *getNetProcessor() const override;
  virtual NetAccept *clone() const override;
  virtual int acceptEvent(int event, void *e) override;
  void init_accept(EThread *t) override;

  // QUICPacketHandler
  virtual void send_packet(const QUICPacket &packet, QUICNetVConnection *vc) override;

private:
  void _recv_packet(int event, UDPPacket *udp_packet) override;

  QUICConnectionTable *_ctable = nullptr;
};

/*
 * @class QUICPacketHanderOut
 * @brief QUIC Packet Handler for outgoing connections
 */
class QUICPacketHandlerOut : public Continuation, public QUICPacketHandler
{
public:
  QUICPacketHandlerOut();
  ~QUICPacketHandlerOut(){};

  void init(QUICNetVConnection *vc);
  int event_handler(int event, Event *data);

  // QUICPacketHandler
  virtual void send_packet(const QUICPacket &packet, QUICNetVConnection *vc) override;

private:
  void _recv_packet(int event, UDPPacket *udp_packet) override;

  QUICNetVConnection *_vc = nullptr;
};
