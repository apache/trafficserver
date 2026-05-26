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
#include "P_QUICClosedConCollector.h"
#include "iocore/net/UDPConnection.h"
#include "tscore/ink_config.h"

#if TS_HAS_OPENSSL_QUIC
#include <openssl/ssl.h>
#elif TS_HAS_QUICHE
#include <quiche.h>
#endif

class QUICNetVConnection;
class QUICConnectionTable;

class QUICPacketHandler
{
public:
  QUICPacketHandler();
  virtual ~QUICPacketHandler();

  void send_packet(UDPConnection *udp_con, IpEndpoint &addr, Ptr<IOBufferBlock> udp_payload, uint16_t segment_size = 0,
                   struct timespec *send_at_hint = nullptr);
  void close_connection(QUICNetVConnection *conn);

protected:
  Event                                  *_collector_event      = nullptr;
  std::unique_ptr<QUICClosedConCollector> _closed_con_collector = nullptr;

  virtual Continuation *_get_continuation() = 0;

  virtual void _recv_packet(int event, UDPPacket *udpPacket) = 0;
};

class QUICPacketHandlerIn : public NetAccept, public QUICPacketHandler
{
public:
#if TS_HAS_OPENSSL_QUIC
  QUICPacketHandlerIn(NetProcessor::AcceptOptions const &opt);
#elif TS_HAS_QUICHE
  QUICPacketHandlerIn(NetProcessor::AcceptOptions const &opt, QUICConnectionTable &ctable, quiche_config &config);
#endif
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
#if TS_HAS_OPENSSL_QUIC
  SSL   *_listener = nullptr;
  Event *_event    = nullptr;
#elif TS_HAS_QUICHE
  QUICConnectionTable &_ctable;
  quiche_config       &_quiche_config;
#endif

  void _recv_packet(int event, UDPPacket *udpPacket) override;

#if TS_HAS_OPENSSL_QUIC
  bool _start_listener();
  void _service_listener();
  void _schedule_event();
#endif
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
