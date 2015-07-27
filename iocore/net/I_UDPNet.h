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

#ifndef __UDPNET_H_
#define __UDPNET_H_

#include "ts/I_Version.h"
#include "I_EventSystem.h"
#include "ts/ink_inet.h"

/**
   UDP service

   You can create UDPConnections for asynchronous send/receive or call
   directly (inefficiently) into network layer.
 */
class UDPNetProcessor : public Processor
{
public:
  virtual int start(int n_upd_threads, size_t stacksize) = 0;

  // this function was interanal intially.. this is required for public and
  // interface probably should change.
  bool CreateUDPSocket(int *resfd, sockaddr const *remote_addr, sockaddr *local_addr, int *local_addr_len, Action **status,
                       int send_bufsize = 0, int recv_bufsize = 0);

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
     @param send_bufsize (optional) Socket buffer size for sending.
     Limits how much outstanding data to OS before it is able to send
     to the NIC.
     @param recv_bufsize (optional) Socket buffer size for sending.
     Limits how much can be queued by OS before we read it.
     @return Action* Always returns ACTION_RESULT_DONE if socket was
     created successfuly, or ACTION_IO_ERROR if not.
  */
  inkcoreapi Action *UDPBind(Continuation *c, sockaddr const *addr, int send_bufsize = 0, int recv_bufsize = 0);

  // Regarding sendto_re, sendmsg_re, recvfrom_re:
  // * You may be called back on 'c' with completion or error status.
  // * 'token' is an opaque which can be used by caller to match up the I/O
  //   with the completion event.
  // * If IOBufferBlock * is passed in the interface, it is reference
  //   counted internally.
  // * For recvfrom_re, data is written beginning at IOBufferBlock::end() and
  //   the IOBufferBlock is not fill()'ed until I/O actually occurs.  This
  //   kind of implies that you can only have one outstanding I/O per
  //   IOBufferBlock
  // Callback:
  // * callback signature is: handleEvent(int event,CompletionEvent *cevent);
  //   where event is one of:
  //  NET_EVENT_DATAGRAM_WRITE_COMPLETE
  //  NET_EVENT_DATAGRAM_WRITE_ERROR
  // * You can get the value of 'token' that you passed in by calling
  //   completionUtil::getHandle(cevent);
  // * You can get other info about the completed operation through use
  //   of the completionUtil class.
  Action *sendto_re(Continuation *c, void *token, int fd, sockaddr const *toaddr, int toaddrlen, IOBufferBlock *buf, int len);
  // I/O buffers referenced by msg must be pinned by the caller until
  // continuation is called back.
  Action *sendmsg_re(Continuation *c, void *token, int fd, struct msghdr *msg);

  Action *recvfrom_re(Continuation *c, void *token, int fd, sockaddr *fromaddr, socklen_t *fromaddrlen, IOBufferBlock *buf, int len,
                      bool useReadCont = true, int timeout = 0);
};

inkcoreapi extern UDPNetProcessor &udpNet;

#include "I_UDPPacket.h"
#include "I_UDPConnection.h"

#endif //__UDPNET_H_
