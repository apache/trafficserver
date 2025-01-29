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

#include "P_NetAccept.h"
#include "iocore/net/UDPConnection.h"

#include <quiche.h>

class QUICNetVConnection;
class QUICConnectionTable;
class QUICClosedConCollector;

class QUICPacketHandler
{
public:
  QUICPacketHandler();
  virtual ~QUICPacketHandler();

  void send_packet(UDPConnection *udp_con, IpEndpoint &addr, Ptr<IOBufferBlock> udp_payload, uint16_t segment_size = 0,
                   struct timespec *send_at_hint = nullptr);
  void close_connection(QUICNetVConnection *conn);

protected:
  Event                  *_collector_event      = nullptr;
  QUICClosedConCollector *_closed_con_collector = nullptr;

  virtual Continuation *_get_continuation() = 0;

  virtual void _recv_packet(int event, UDPPacket *udpPacket) = 0;
};

class QUICPacketHandlerIn : public NetAccept, public QUICPacketHandler
{
public:
  QUICPacketHandlerIn(const NetProcessor::AcceptOptions &opt, QUICConnectionTable &ctable, quiche_config &config);
  ~QUICPacketHandlerIn();

  // NetAccept
  NetProcessor *getNetProcessor() const override;
  NetAccept    *clone() const override;
  int           acceptEvent(int event, void *e) override;
  void          init_accept(EThread *t) override;

protected:
  // QUICPacketHandler
  Continuation *_get_continuation() override;

private:
  QUICConnectionTable &_ctable;
  quiche_config       &_quiche_config;

  void _recv_packet(int event, UDPPacket *udpPacket) override;
};

class QUICPacketHandlerOut : public Continuation, public QUICPacketHandler
{
public:
  QUICPacketHandlerOut(){};
  ~QUICPacketHandlerOut(){};

  void init(QUICNetVConnection *vc);

protected:
  // QUICPacketHandler
  Continuation *_get_continuation() override;

private:
  void _recv_packet(int event, UDPPacket *udp_packet) override;
};
