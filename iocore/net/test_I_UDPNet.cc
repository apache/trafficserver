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

#include <iostream>
#include <cstdlib>
#include <cstring>

#include "tscore/I_Layout.h"
#include "tscore/TestBox.h"

#include "I_EventSystem.h"
#include "I_Net.h"
#include "I_UDPNet.h"
#include "I_UDPPacket.h"
#include "I_UDPConnection.h"

#include "diags.i"

static const char payload[] = "hello";
in_port_t port              = 0;
int pfd[2]; // Pipe used to signal client with transient port.

/*This implements a standard Unix echo server: just send every udp packet you
  get back to where it came from*/

class EchoServer : public Continuation
{
public:
  EchoServer() : Continuation(new_ProxyMutex()) { SET_HANDLER(&EchoServer::start); };
  bool start();
  int handle_packet(int event, void *data);
};

bool
EchoServer::start()
{
  SET_HANDLER(&EchoServer::handle_packet);

  sockaddr_in addr;
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port        = 0;

  udpNet.UDPBind(static_cast<Continuation *>(this), reinterpret_cast<sockaddr const *>(&addr), 1048576, 1048576);

  return true;
}

int
EchoServer::handle_packet(int event, void *data)
{
  switch (event) {
  case NET_EVENT_DATAGRAM_OPEN: {
    UDPConnection *con = reinterpret_cast<UDPConnection *>(data);
    port               = con->getPortNum(); // store this for later signaling.
    /* For some reason the UDP packet handling isn't fully set up at this time. We need another
       pass through the event loop for that or the packet is never read even thought it arrives
       on the port (as reported by ss --udp --numeric --all).
    */
    eventProcessor.schedule_in(this, 1, ET_UDP);
    break;
  }

  case NET_EVENT_DATAGRAM_READ_READY: {
    Queue<UDPPacket> *q = reinterpret_cast<Queue<UDPPacket> *>(data);

    // send what ever we get back to the client
    while (UDPPacket *p = q->pop()) {
      p->to              = p->from;
      UDPConnection *con = p->getConnection();
      con->send(this, p);
    }
    break;
  }

  case NET_EVENT_DATAGRAM_READ_ERROR: {
    std::cout << "got Read Error exiting" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  case NET_EVENT_DATAGRAM_WRITE_ERROR: {
    std::cout << "got Write Error exiting" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  case EVENT_INTERVAL:
    // Done the extra event loop, signal the client to start.
    std::cout << "Echo Server port: " << port << std::endl;
    ink_release_assert(write(pfd[1], &port, sizeof(port)) == sizeof(port));
    break;

  default:
    std::cout << "got unknown event [" << event << "]" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  return EVENT_DONE;
}

void
signal_handler(int signum)
{
  std::exit(EXIT_SUCCESS);
}

void
udp_echo_server()
{
  Layout::create();
  RecModeT mode_type = RECM_STAND_ALONE;
  RecProcessInit(mode_type);

  Thread *main_thread = new EThread();
  main_thread->set_specific();
  net_config_poll_timeout = 10;

  init_diags("udp-.*", nullptr);
  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(2);
  udpNet.start(1, 1048576);

  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, signal_handler);

  EchoServer server;
  eventProcessor.schedule_in(&server, 1, ET_UDP);

  this_thread()->execute();
}

void
udp_client(char *buf)
{
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::cout << "Couldn't create socket" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  struct timeval tv;
  tv.tv_sec  = 20;
  tv.tv_usec = 0;

  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));

  sockaddr_in addr;
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port        = htons(port);

  ssize_t n = sendto(sock, payload, sizeof(payload), 0, (struct sockaddr *)&addr, sizeof(addr));
  if (n < 0) {
    std::cout << "Couldn't send udp packet" << std::endl;
    close(sock);
    std::exit(EXIT_FAILURE);
  }

  ssize_t l = recv(sock, buf, sizeof(buf), 0);
  if (l < 0) {
    std::cout << "Couldn't recv udp packet" << std::endl;
    close(sock);
    std::exit(EXIT_FAILURE);
  }

  close(sock);
}

REGRESSION_TEST(UDPNet_echo)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box         = REGRESSION_TEST_PASSED;
  char buf[8] = {0};

  int z = pipe(pfd);
  if (z < 0) {
    std::cout << "Unable to create pipe" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  pid_t pid = fork();
  if (pid < 0) {
    std::cout << "Couldn't fork" << std::endl;
    std::exit(EXIT_FAILURE);
  } else if (pid == 0) {
    close(pfd[0]);
    udp_echo_server();
  } else {
    close(pfd[1]);
    if (read(pfd[0], &port, sizeof(port)) <= 0) {
      std::cout << "Failed to get signal with port data [" << errno << ']' << std::endl;
      std::exit(EXIT_FAILURE);
    }
    udp_client(buf);

    kill(pid, SIGTERM);
    int status;
    wait(&status);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      box.check(strncmp(buf, payload, sizeof(payload)) == 0, "echo doesn't match");
    } else {
      std::cout << "UDP Echo Server exit failure" << std::endl;
      std::exit(EXIT_FAILURE);
    }
  }
}

int
main(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  RegressionTest::run("UDPNet", REGRESSION_TEST_QUICK);
  return RegressionTest::final_status == REGRESSION_TEST_PASSED ? 0 : 1;
}
