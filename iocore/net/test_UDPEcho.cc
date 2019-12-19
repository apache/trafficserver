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
#include "UDPConnection.h"
#include "UDPProcessor.h"
#include "records/I_RecProcess.h"
#include "RecordsConfig.h"

#include "diags.i"

void
signal_handler(int signum)
{
  std::exit(EXIT_SUCCESS);
}

// StatPagesManager statPagesManager;
class CloseCont : public Continuation
{
public:
  int
  mainEvent(int event, void *data)
  {
    signal_handler(0);
    return 0;
  }

  CloseCont() { SET_HANDLER(&CloseCont::mainEvent); }
};

static CloseCont close_cont;

static constexpr char payload[] = "hello world";
in_port_t port                  = 0;
int pfd[2]; // Pipe used to signal client with transient port.

class EchoServer : public Continuation
{
public:
  int
  mainEvent(int event, void *data)
  {
    switch (event) {
    case NET_EVENT_DATAGRAM_READ_READY: {
      UDP2Connection *con = static_cast<UDP2Connection *>(data);
      ink_assert(this->_conn == con);
      auto packet = this->_conn->recv();
      this->_data = packet->chain;
      std::string str(packet->chain->start(), packet->chain->read_avail());
      std::cout << "receive msg: " << str << std::endl;
      this->_peer  = packet->from;
      packet->from = packet->to;
      packet->to   = this->_peer;
      ink_assert(this->_conn->send(packet) != EVENT_CONT);
      break;
    }
    case NET_EVENT_DATAGRAM_WRITE_READY: {
      if (this->_data != nullptr) {
        UDP2Packet *new_p = new UDP2Packet;
        new_p->chain      = this->_data;
        this->_data       = nullptr;
        ink_assert(this->_conn->connect(&this->_peer.sa) == 0);
        ink_assert(this->_conn->is_connected());
        ink_assert(this->_conn->send(new_p) != EVENT_CONT);
        this_ethread()->schedule_in(&close_cont, 1 * HRTIME_SECOND);
      } else {
        this->_conn->close();
        this->_conn = nullptr;
      }
      break;
    }
    }
    return 0;
  }

  EchoServer()
  {
    SET_HANDLER(&EchoServer::mainEvent);
    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;

    this->_conn = new UDP2ConnectionImpl(this);
    int res     = this->_conn->create_socket(reinterpret_cast<sockaddr *const>(&addr));
    if (res < 0) {
      std::cout << "create socket error [" << strerror(errno) << "]" << std::endl;
      std::exit(EXIT_FAILURE);
    }

    std::cout << "bind to port: " << ats_ip_port_host_order(this->_conn->from()) << std::endl;
    this->_conn->refcount_inc();
    this->_conn->start_io();
    int port = ats_ip_port_host_order(this->_conn->from());
    ink_release_assert(write(pfd[1], &port, sizeof(port)) == sizeof(port));
  }
  ~EchoServer() { this->_data = nullptr; }

private:
  UDP2ConnectionImpl *_conn;
  Ptr<IOBufferBlock> _data;
  IpEndpoint _peer{};
};

void
udp_client(TestBox &box)
{
  char buf[128] = {0};
  int sock      = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::cout << "Couldn't create socket" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  struct timeval tv;
  tv.tv_sec  = 20;
  tv.tv_usec = 0;

  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&tv), sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&tv), sizeof(tv));

  sockaddr_in addr;
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port        = htons(port);

  ssize_t n = sendto(sock, payload, sizeof(payload), 0, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
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
  std::cout << "client recv: " << buf << std::endl;
  box.check(strncmp(buf, payload, sizeof(payload)) == 0, "echo doesn't match");

  memset(buf, 0, 128);
  l = recv(sock, buf, sizeof(buf), 0);
  if (l < 0) {
    std::cout << "Couldn't recv udp packet" << std::endl;
    close(sock);
    std::exit(EXIT_FAILURE);
  }
  std::cout << "client recv2: " << buf << std::endl;
  box.check(strncmp(buf, payload, sizeof(payload)) == 0, "echo connect doesn't match");

  close(sock);
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
  RecProcessInit(RECM_STAND_ALONE);
  LibRecordsConfigInit();
  ink_net_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));

  // statPagesManager.init();
  init_diags("udp", nullptr);
  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  netProcessor.init();
  eventProcessor.start(1);
  udp2Net.start(1, 1048576);

  initialize_thread_for_net(this_ethread());

  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, signal_handler);

  EchoServer *server = new EchoServer;
  (void)server;
  // eventProcessor.schedule_in(server, 1);

  this_thread()->execute();
}

REGRESSION_TEST(UDPNet_echo)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

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
    Debug("udp_echo", "client get ports: %d", port);
    udp_client(box);

    // kill(pid, SIGTERM);
    int status;
    wait(&status);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
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
