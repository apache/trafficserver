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

#include "tscore/ink_platform.h"
#include "P_Connection.h"
#include "P_NetAccept.h"
#include "quic/QUICTypes.h"
#include "quic/QUICConnectionTable.h"

class QUICClosedConCollector;
class QUICNetVConnection;
class QUICPacket;
class QUICPacketHeaderProtector;

class QUICPacketHandler
{
public:
  QUICPacketHandler();
  ~QUICPacketHandler();

  void send_packet(const QUICPacket &packet, QUICNetVConnection *vc, const QUICPacketHeaderProtector &pn_protector);
  void send_packet(QUICNetVConnection *vc, Ptr<IOBufferBlock> udp_payload);

  void close_connection(QUICNetVConnection *conn);

protected:
  void _send_packet(const QUICPacket &packet, UDPConnection *udp_con, IpEndpoint &addr, uint32_t pmtu,
                    const QUICPacketHeaderProtector *ph_protector, int dcil);
  void _send_packet(UDPConnection *udp_con, IpEndpoint &addr, Ptr<IOBufferBlock> udp_payload);

  // FIXME Remove this
  // QUICPacketHandler could be a continuation, but NetAccept is a contination too.
  virtual Continuation *_get_continuation() = 0;

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
  QUICPacketHandlerIn(const NetProcessor::AcceptOptions &opt, QUICConnectionTable &ctable);
  ~QUICPacketHandlerIn();

  // NetAccept
  virtual NetProcessor *getNetProcessor() const override;
  virtual NetAccept *clone() const override;
  virtual int acceptEvent(int event, void *e) override;
  void init_accept(EThread *t) override;

protected:
  // QUICPacketHandler
  Continuation *_get_continuation() override;

private:
  void _recv_packet(int event, UDPPacket *udp_packet) override;
  int _stateless_retry(const uint8_t *buf, uint64_t buf_len, UDPConnection *connection, IpEndpoint from, QUICConnectionId dcid,
                       QUICConnectionId scid, QUICConnectionId *original_cid);

  QUICConnectionTable &_ctable;
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

protected:
  // QUICPacketHandler
  Continuation *_get_continuation() override;

private:
  void _recv_packet(int event, UDPPacket *udp_packet) override;

  QUICNetVConnection *_vc = nullptr;
};
