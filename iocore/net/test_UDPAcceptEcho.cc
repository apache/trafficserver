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
#include "UDPConnectionManager.h"

#include "diags.i"

static pid_t pid;

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

in_port_t port = 0;
int pfd[2]; // Pipe used to signal client with transient port.

class EchoServer : public Continuation
{
public:
  int
  mainEvent(int event, void *data)
  {
    switch (event) {
    case NET_EVENT_DATAGRAM_CONNECT_SUCCESS:
      this->_con = static_cast<UDP2ConnectionImpl *>(data);
      ink_release_assert(this->_con->is_connected());
      std::cout << "connect success" << std::endl;
      break;

    case NET_EVENT_DATAGRAM_READ_READY: {
      std::cout << "read ready event" << std::endl;
      while (true) {
        auto p = this->_con->recv();
        if (p == nullptr) {
          return 0;
        }
        ink_release_assert(p != nullptr);
        this->_con->send(p);
        this->count++;
        std::cout << "receive msg from echo: " << std::string(p->chain->start(), p->chain->read_avail());
        std::cout << " then send" << this->count << std::endl;
        if (this->count == 2) {
          this->_con->close();
          this->_con = nullptr;
          this_ethread()->schedule_in(&close_cont, 100 * HRTIME_MSECOND);
          return 0;
        }
      }
    }
    default:
      break;
    }
    return 0;
  }
  EchoServer() { SET_HANDLER(&EchoServer::mainEvent); }

private:
  int count                = 0;
  UDP2ConnectionImpl *_con = nullptr;
};

class AcceptServer : public Continuation
{
public:
  int
  createEvent(int event, void *data)
  {
    switch (event) {
    case NET_EVENT_DATAGRAM_WRITE_READY:
      return 0;
    case EVENT_INTERVAL:
      break;
    default:
      ink_assert(0);
      return 0;
    }
    std::cout << "Accept woke up" << std::endl;
    UDP2Packet *p = this->_packet;
    auto t        = eventProcessor.assign_thread(ET_NET);
    ink_assert(this->_sub_con == nullptr);
    this->_sub_con = this->_conn->create_sub_connection(this->_conn->from(), p->to, new EchoServer(), t);
    // this->_sub_con->dispatch(this->_packet);
    return 0;
  }

  int
  mainEvent(int event, void *data)
  {
    switch (event) {
    case NET_EVENT_DATAGRAM_READ_READY: {
      ink_assert(this->_conn == static_cast<AcceptUDP2ConnectionImpl *>(data));
      auto p        = this->_conn->recv();
      auto tmp      = p->from;
      p->from       = p->to;
      p->to         = tmp;
      this->_packet = p;
      auto new_p    = new UDP2Packet;
      *new_p        = *p;
      this->_conn->send(new_p);

      std::cout << "receive msg from accept: " << std::string(p->chain->start(), p->chain->read_avail()) << std::endl;
      // waiting for client send all packet to accept socket's buffer
      std::cout << "accept sleep" << std::endl;
      SET_HANDLER(&AcceptServer::createEvent);
      this_ethread()->schedule_in(this, HRTIME_MSECONDS(1));
      break;
    }
    case NET_EVENT_DATAGRAM_WRITE_READY:
      break;
    default:
      ink_release_assert(0);
      break;
    }
    return 0;
  }

  AcceptServer()
  {
    SET_HANDLER(&AcceptServer::mainEvent);
    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;

    auto udp_manager = get_UDP2ConnectionManager(eventProcessor.assign_thread(ET_UDP2));
    this->_conn      = udp_manager->create_accept_udp_connection(this, eventProcessor.assign_thread(ET_UDP2),
                                                            reinterpret_cast<sockaddr *const>(&addr));
    ink_release_assert(this->_conn != nullptr);
    std::cout << "bind to port: " << ats_ip_port_host_order(this->_conn->from()) << std::endl;
    int port = ats_ip_port_host_order(this->_conn->from());
    ink_release_assert(write(pfd[1], &port, sizeof(port)) == sizeof(port));
    this->mutex = this->_conn->mutex;
  }

private:
  AcceptUDP2ConnectionImpl *_conn = nullptr;
  UDP2ConnectionImpl *_sub_con    = nullptr;
  UDP2Packet *_packet             = nullptr;
};

void
udp_client(TestBox &box)
{
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::cout << "Couldn't create socket" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  const char payload[]  = "helloword";
  const char payload1[] = "helloword1";
  const char payload2[] = "helloword2";

  struct timeval tv;
  tv.tv_sec  = 20;
  tv.tv_usec = 0;

  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&tv), sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&tv), sizeof(tv));

  sockaddr_in addr;
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port        = htons(port);

  auto bsend = [sock, addr](const char *payload) {
    ssize_t n = sendto(sock, payload, strlen(payload), 0,
                       reinterpret_cast<struct sockaddr *>(const_cast<struct sockaddr_in *>(&addr)), sizeof(addr));
    if (n < 0) {
      std::cout << "Couldn't send udp packet" << std::endl;
      close(sock);
      std::exit(EXIT_FAILURE);
    }
  };

  auto brecv = [sock, box](const char *expect) -> bool {
    char buf[128] = {0};
    ssize_t l     = recv(sock, buf, sizeof(buf), 0);
    if (l < 0) {
      std::cout << "Couldn't recv udp packet" << std::endl;
      close(sock);
      const_cast<TestBox *>(&box)->check(false, "errno recv");
      return false;
    }
    std::cout << "client recv payload: " << buf << std::endl;
    const_cast<TestBox *>(&box)->check(strncmp(buf, expect, sizeof(payload)) == 0, "echo doesn't match");
    if (strncmp(buf, expect, sizeof(payload))) {
      kill(pid, SIGINT);
    }
    return strncmp(buf, expect, sizeof(payload)) == 0;
  };

#define CHECK_RECV(statement) \
  do {                        \
    if (!statement) {         \
      return;                 \
    }                         \
  } while (0)

  std::cout << "client send payload" << std::endl;
  bsend(payload);             // send payload to accept;
  CHECK_RECV(brecv(payload)); // accept reply the payload
  // CHECK_RECV(brecv(payload)); // sub udp connection send another one.

  // send to accept udp connection since we are sleeping in one second.
  std::cout << "client send payload1" << std::endl;
  bsend(payload1); // send to accept udp connection since we are sleeping in one second.

  std::cout << "client send payload2" << std::endl;
  bsend(payload2); // send to accept udp again.

  // recv from sub udp connection
  CHECK_RECV(brecv(payload1));
  CHECK_RECV(brecv(payload2));

  close(sock);
  return;
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

  AcceptServer *server = new AcceptServer;
  (void)server;

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

  pid = fork();
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
