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

#include <stdio.h>
#include <stdlib.h>

#include "I_Net.h"
#include "ts/List.h"

#include "diags.i"

/*This implements a standard Unix echo server: just send every udp packet you
  get back to where it came from*/

class EchoServer : public Continuation
{
  int sock_fd;
  UDPConnection *conn;

  int handlePacket(int event, void *data);

public:
  EchoServer() : Continuation(new_ProxyMutex()){};

  bool Start(int port);
};

bool
EchoServer::Start(int port)
{
  Action *action;
  sockaddr_in addr;

  if (!udpNet.CreateUDPSocket(&sock_fd, &addr, &action, port))
    return false;

  conn = new_UDPConnection(sock_fd);
  conn->bindToThread(this);

  SET_HANDLER(&EchoServer::handlePacket);

  conn->recv(this);
}

int
EchoServer::handlePacket(int event, void *data)
{
  switch (event) {
  case NET_EVENT_DATAGRAM_READ_READY: {
    Queue<UDPPacket> *q = (Queue<UDPPacket> *)data;
    UDPPacket *p;

    // send what ever we get back to the client
    while (p = q->pop()) {
      p->m_to = p->m_from;
      conn->send(this, p);
    }
    break;
  }

  case NET_EVENT_DATAGRAM_READ_ERROR:
    printf("got Read Error exiting\n");
    _exit(1);

  case NET_EVENT_DATAGRAM_WRITE_ERROR:
    printf("got write error: %d\n", (int)data);
    break;

  default:
    printf("got unknown event\n");
  }

  return EVENT_DONE;
}

int
main(int argc, char *argv[])
{
  int port = (argc > 1) ? atoi(argv[1]) : 39680;

  init_diags((argc > 2) ? argv[2] : "udp.*", NULL);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  eventProcessor.start(2);
  udpNet.start(1);

  signal(SIGPIPE, SIG_IGN);

  EchoServer server;
  server.Start(port);

  this_thread()->execute();
}
