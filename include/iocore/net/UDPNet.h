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

/****************************************************************************

  I_UDPNet.h
  This file provides UDP interface. To be included in I_Net.h


 ****************************************************************************/

#pragma once

#include "tscore/Version.h"
#include "iocore/eventsystem/EventSystem.h"
#include "tscore/ink_inet.h"
#include "iocore/net/NetVCOptions.h"

/**
   UDP service

   You can create UDPConnections for asynchronous send/receive or call
   directly (inefficiently) into network layer.
 */
class UDPNetProcessor : public Processor
{
public:
  virtual EventType register_event_type()                               = 0;
  int               start(int n_upd_threads, size_t stacksize) override = 0;

  // this function was internal initially.. this is required for public and
  // interface probably should change.
  bool CreateUDPSocket(int *resfd, sockaddr const *remote_addr, Action **status, NetVCOptions const &opt);

  /**
     create UDPConnection

     Why was this implemented as an asynchronous call?  Just in case
     Windows requires it...
     <p>
     <b>Callbacks:</b><br>
     cont->handleEvent( NET_EVENT_DATAGRAM_OPEN, UDPConnection *) is
     called for new socket.

     @param c Continuation that is called back with newly created
     socket.
     @param addr Address to bind (includes port)
     @param fd File descriptor to use (if exists)
     @param send_bufsize (optional) Socket buffer size for sending.
     Limits how much outstanding data to OS before it is able to send
     to the NIC.
     @param recv_bufsize (optional) Socket buffer size for sending.
     Limits how much can be queued by OS before we read it.
     @return Action* Always returns ACTION_RESULT_DONE if socket was
     created successfully, or ACTION_IO_ERROR if not.
  */
  Action *UDPBind(Continuation *c, sockaddr const *addr, int fd = -1, int send_bufsize = 0, int recv_bufsize = 0);
};

extern UDPNetProcessor &udpNet;
extern EventType        ET_UDP;

#include "iocore/net/UDPPacket.h"
#include "iocore/net/UDPConnection.h"
