
/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "I_EventSystem.h"
#include "I_NetVConnection.h"
#include "P_QUICNetProcessor.h"

#include "ts/ink_string.h"
#include "ts/I_Layout.h"

#define THREADS 1
#define DIAGS_LOG_FILE "diags.log"

constexpr size_t stacksize = 1048576;

// copy from iocore/utils/diags.i
static void
reconfigure_diags()
{
  int i;
  DiagsConfigState c;

  // initial value set to 0 or 1 based on command line tags
  c.enabled[DiagsTagType_Debug]  = (diags->base_debug_tags != nullptr);
  c.enabled[DiagsTagType_Action] = (diags->base_action_tags != nullptr);

  c.enabled[DiagsTagType_Debug]  = 1;
  c.enabled[DiagsTagType_Action] = 1;
  diags->show_location           = SHOW_LOCATION_ALL;

  // read output routing values
  for (i = 0; i < DL_Status; i++) {
    c.outputs[i].to_stdout   = 0;
    c.outputs[i].to_stderr   = 1;
    c.outputs[i].to_syslog   = 0;
    c.outputs[i].to_diagslog = 0;
  }

  for (i = DL_Status; i < DiagsLevel_Count; i++) {
    c.outputs[i].to_stdout   = 0;
    c.outputs[i].to_stderr   = 0;
    c.outputs[i].to_syslog   = 0;
    c.outputs[i].to_diagslog = 1;
  }

  //////////////////////////////
  // clear out old tag tables //
  //////////////////////////////

  diags->deactivate_all(DiagsTagType_Debug);
  diags->deactivate_all(DiagsTagType_Action);

  //////////////////////////////////////////////////////////////////////
  //                     add new tag tables
  //////////////////////////////////////////////////////////////////////

  if (diags->base_debug_tags)
    diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
  if (diags->base_action_tags)
    diags->activate_taglist(diags->base_action_tags, DiagsTagType_Action);

////////////////////////////////////
// change the diags config values //
////////////////////////////////////
#if !defined(__GNUC__) && !defined(hpux)
  diags->config = c;
#else
  memcpy(((void *)&diags->config), ((void *)&c), sizeof(DiagsConfigState));
#endif
}

static void
init_diags(const char *bdt, const char *bat)
{
  char diags_logpath[500];
  strcpy(diags_logpath, DIAGS_LOG_FILE);

  diags = new Diags("Client", bdt, bat, new BaseLogFile(diags_logpath));
  Status("opened %s", diags_logpath);

  reconfigure_diags();
}

class QUICClient : public Continuation
{
public:
  QUICClient() : Continuation(new_ProxyMutex()) { SET_HANDLER(&QUICClient::state_http_server_open); };
  void start();
  int state_http_server_open(int event, void *data);
};

// HttpSM::state_http_server_open(int event, void *data)
int
QUICClient::state_http_server_open(int event, void *data)
{
  switch (event) {
  case NET_EVENT_OPEN: {
    // TODO: create ProxyServerSession / ProxyServerTransaction
    // TODO: send HTTP/0.9 message
    Debug("quic_client", "start proxy server ssn/txn");
    break;
  }
  case NET_EVENT_OPEN_FAILED: {
    ink_assert(false);
    break;
  }
  default:
    ink_assert(false);
  }

  return 0;
}

// TODO: ip:port
void
QUICClient::start()
{
  sockaddr_in addr;
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port        = htons(4433);

  NetVCOptions opt;
  opt.ip_proto  = NetVCOptions::USE_UDP;
  opt.ip_family = addr.sin_family;

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  quic_NetProcessor.connect_re(this, reinterpret_cast<sockaddr const *>(&addr), &opt);
}

int
main(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  Layout::create();
  init_diags("quic|udp", nullptr);
  RecProcessInit(RECM_STAND_ALONE);

  SSLInitializeLibrary();
  SSLConfig::startup();

  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  eventProcessor.start(THREADS);
  udpNet.start(1, stacksize);
  quic_NetProcessor.start(-1, stacksize);

  Thread *main_thread = new EThread;
  main_thread->set_specific();

  QUICClient client;
  client.start();

  main_thread->execute();
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
