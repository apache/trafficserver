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

#include "P_QUICNetProcessor.h"
#include "P_QUICNetVConnection.h"
#include "iocore/net/UDPPacket.h"

class NetHandler;
using NetContHandler = int (NetHandler::*)(int, void *);

void initialize_thread_for_quic_net(EThread *thread);

struct QUICPollEvent {
  QUICConnection *con;
  UDPPacket      *packet;
  void            init(QUICConnection *con, UDPPacket *packet);
  void            free();

  SLINK(QUICPollEvent, alink);
  LINK(QUICPollEvent, link);
};

struct QUICPollCont : public Continuation {
  NetHandler     *net_handler;
  PollDescriptor *pollDescriptor;

  QUICPollCont(Ptr<ProxyMutex> &m);
  QUICPollCont(Ptr<ProxyMutex> &m, NetHandler *nh);
  ~QUICPollCont();
  int pollEvent(int, Event *);

public:
  // Atomic Queue to save incoming packets
  ASLL(QUICPollEvent, alink) inQueue;

private:
  // Internal Queue to save Long Header Packet
  Que(UDPPacket, link) _longInQueue;

private:
#if TS_HAS_QUICHE
  void _process_packet(QUICPollEvent *e, NetHandler *nh);
#else
  void _process_short_header_packet(QUICPollEvent *e, NetHandler *nh);
  void _process_long_header_packet(QUICPollEvent *e, NetHandler *nh);
#endif
};

static inline QUICPollCont *
get_QUICPollCont(EThread *t)
{
  return static_cast<QUICPollCont *>(ETHREAD_GET_PTR(t, quic_NetProcessor.quicPollCont_offset));
}

extern ClassAllocator<QUICPollEvent> quicPollEventAllocator;
