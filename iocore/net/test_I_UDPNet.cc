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

#include "ts/I_Layout.h"

#include "I_EventSystem.h"
#include "I_Net.h"
#include "I_UDPNet.h"
#include "I_UDPPacket.h"
#include "I_UDPConnection.h"

#include "diags.i"

static const int port = 4443;

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
  addr.sin_port        = htons(port);

  udpNet.UDPBind(static_cast<Continuation *>(this), reinterpret_cast<sockaddr const *>(&addr), 1024000, 1024000);

  return true;
}

int
EchoServer::handle_packet(int event, void *data)
{
  switch (event) {
  case NET_EVENT_DATAGRAM_OPEN: {
    UDPConnection *con = reinterpret_cast<UDPConnection *>(data);
    std::cout << "port: " << con->getPortNum() << std::endl;
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

  default:
    std::cout << "got unknown event" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  return EVENT_DONE;
}

void
signal_handler(int signum)
{
  std::exit(EXIT_SUCCESS);
}

int
main(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  Layout::create();
  RecModeT mode_type = RECM_STAND_ALONE;
  RecProcessInit(mode_type);

  Thread *main_thread = new EThread();
  main_thread->set_specific();
  net_config_poll_timeout = 10;

  init_diags("udp-.*", nullptr);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  eventProcessor.start(2);
  udpNet.start(1, 1048576);

  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, signal_handler);

  EchoServer server;
  eventProcessor.schedule_imm(&server, ET_UDP);

  this_thread()->execute();
}

//
// stub
//
void
initialize_thread_for_http_sessions(EThread *, int)
{
  ink_assert(false);
}

#include "P_UnixNet.h"
#include "P_DNSConnection.h"
int
DNSConnection::close()
{
  ink_assert(false);
  return 0;
}

void
DNSConnection::trigger()
{
  ink_assert(false);
}

#include "StatPages.h"
void
StatPagesManager::register_http(char const *, Action *(*)(Continuation *, HTTPHdr *))
{
  ink_assert(false);
}

#include "ParentSelection.h"
void
SocksServerConfig::startup()
{
  ink_assert(false);
}

int SocksServerConfig::m_id = 0;

void
ParentConfigParams::findParent(HttpRequestData *, ParentResult *, unsigned int, unsigned int)
{
  ink_assert(false);
}

void
ParentConfigParams::nextParent(HttpRequestData *, ParentResult *, unsigned int, unsigned int)
{
  ink_assert(false);
}

#include "Log.h"
void
Log::trace_in(sockaddr const *, unsigned short, char const *, ...)
{
  ink_assert(false);
}

void
Log::trace_out(sockaddr const *, unsigned short, char const *, ...)
{
  ink_assert(false);
}

#include "InkAPIInternal.h"
int
APIHook::invoke(int, void *)
{
  ink_assert(false);
  return 0;
}

APIHook *
APIHook::next() const
{
  ink_assert(false);
  return nullptr;
}

APIHook *
APIHooks::get() const
{
  ink_assert(false);
  return nullptr;
}

void
ConfigUpdateCbTable::invoke(const char * /* name ATS_UNUSED */)
{
  ink_release_assert(false);
}

#include "ControlMatcher.h"
char *
HttpRequestData::get_string()
{
  ink_assert(false);
  return nullptr;
}

const char *
HttpRequestData::get_host()
{
  ink_assert(false);
  return nullptr;
}

sockaddr const *
HttpRequestData::get_ip()
{
  ink_assert(false);
  return nullptr;
}

sockaddr const *
HttpRequestData::get_client_ip()
{
  ink_assert(false);
  return nullptr;
}

SslAPIHooks *ssl_hooks = nullptr;
StatPagesManager statPagesManager;

#include "ProcessManager.h"
inkcoreapi ProcessManager *pmgmt = nullptr;

int
BaseManager::registerMgmtCallback(int, MgmtCallback, void *)
{
  ink_assert(false);
  return 0;
}

void
ProcessManager::signalManager(int, char const *, int)
{
  ink_assert(false);
  return;
}
