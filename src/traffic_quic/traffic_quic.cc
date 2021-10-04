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

#include "tscore/ink_string.h"
#include "tscore/ink_args.h"
#include "tscore/I_Layout.h"
#include "tscore/I_Version.h"

#include "RecordsConfig.h"
#include "URL.h"
#include "MIME.h"
#include "HTTP.h"
#include "HuffmanCodec.h"
#include "Http3Config.h"

#include "diags.h"
#include "quic_client.h"

#include "P_SSLUtils.h"
#include "P_SSLConfig.h"

#define THREADS 1

constexpr size_t stacksize = 1048576;

// TODO: Support QUIC version, cipher suite ...etc
// TODO: Support qdrive tests
//   https://github.com/ekr/qdrive
//   https://github.com/mcmanus/mozquic/tree/master/tests/qdrive
int
main(int argc, const char **argv)
{
  // Before accessing file system initialize Layout engine
  Layout::create();

  // Set up the application version info
  AppVersionInfo appVersionInfo;
  appVersionInfo.setup(PACKAGE_NAME, "traffic_quic", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  QUICClientConfig config;

  const ArgumentDescription argument_descriptions[] = {
    {"addr", 'a', "Address", "S1023", config.addr, nullptr, nullptr},
    {"output", 'o', "Write to FILE instead of stdout", "S1023", config.output, nullptr, nullptr},
    {"port", 'p', "Port", "S15", config.port, nullptr, nullptr},
    {"path", 'P', "Path", "S1017", config.path, nullptr, nullptr},
    {"server", 's', "Server name", "S127", config.server_name, nullptr, nullptr},
    {"debug", 'T', "Vertical-bar-separated Debug Tags", "S1023", config.debug_tags, nullptr, nullptr},
    {"close", 'c', "Enable connection close exercise", "F", &config.close, nullptr, nullptr},
    {"reset", 'r', "Enable stateless reset exercise", "F", &config.reset, nullptr, nullptr},
    {"http0_9", '-', "Enable HTTP/0.9", "T", &config.http0_9, nullptr, nullptr},
    {"http3", '-', "Enable HTTP/3", "F", &config.http3, nullptr, nullptr},

    HELP_ARGUMENT_DESCRIPTION(),
    VERSION_ARGUMENT_DESCRIPTION(),
    RUNROOT_ARGUMENT_DESCRIPTION(),
  };

  // Process command line arguments and dump into variables
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  if (config.http3) {
    config.http0_9 = false;
  }

  init_diags(config.debug_tags, nullptr);
  RecProcessInit(RECM_STAND_ALONE);
  LibRecordsConfigInit();

  Debug("quic_client", "Load configs from %s", RecConfigReadConfigDir().c_str());

  Thread *main_thread = new EThread;
  main_thread->set_specific();
  net_config_poll_timeout = 10;
  ink_net_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));

  SSLInitializeLibrary();
  SSLConfig::startup();

  netProcessor.init();
  quic_NetProcessor.init();

  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(THREADS);
  udpNet.start(1, stacksize);
  quic_NetProcessor.start(-1, stacksize);

  // Same to init_http_header(); in traffic_server.cc
  url_init();
  mime_init();
  http_init();
  hpack_huffman_init();

  Http3Config::startup();

  QUICClient client(&config);
  eventProcessor.schedule_in(&client, 1, ET_NET);

  this_thread()->execute();
}

// FIXME: remove stub
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
  //  ink_assert(false);
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

APIHook *
APIHook::next() const
{
  ink_assert(false);
  return nullptr;
}

void
APIHooks::clear()
{
  ink_abort("do not call stub");
}

void
APIHooks::append(INKContInternal *)
{
  ink_abort("do not call stub");
}

int
APIHook::invoke(int, void *) const
{
  ink_assert(false);
  return 0;
}

APIHook *
APIHooks::head() const
{
  return nullptr;
}

HttpHookState::HttpHookState() {}

void
HttpHookState::init(TSHttpHookID id, HttpAPIHooks const *global, HttpAPIHooks const *ssn, HttpAPIHooks const *txn)
{
}

APIHook const *
HttpHookState::getNext()
{
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

SslAPIHooks *ssl_hooks             = nullptr;
LifecycleAPIHooks *lifecycle_hooks = nullptr;
StatPagesManager statPagesManager;

#include "HttpDebugNames.h"
const char *
HttpDebugNames::get_api_hook_name(TSHttpHookID t)
{
  return "dummy";
}
const char *
HttpDebugNames::get_event_name(int)
{
  return "dummy";
}

#include "HttpSM.h"
HttpSM::HttpSM() : Continuation(nullptr), vc_table(this) {}

void
HttpSM::cleanup()
{
  ink_abort("do not call stub");
}

void
HttpSM::destroy()
{
  ink_abort("do not call stub");
}

void
HttpSM::set_next_state()
{
  ink_abort("do not call stub");
}

void
HttpSM::handle_api_return()
{
  ink_abort("do not call stub");
}

void
HttpSM::attach_client_session(ProxyTransaction *)
{
  ink_abort("do not call stub");
}

void
HttpSM::init(bool from_early_data)
{
  ink_abort("do not call stub");
}

ClassAllocator<HttpSM> httpSMAllocator("httpSMAllocator");
HttpAPIHooks *http_global_hooks;

HttpVCTable::HttpVCTable(HttpSM *) {}

PostDataBuffers::~PostDataBuffers() {}

#include "HttpTunnel.h"
HttpTunnel::HttpTunnel() : Continuation(nullptr) {}
HttpTunnelConsumer::HttpTunnelConsumer() {}
HttpTunnelProducer::HttpTunnelProducer() {}
ChunkedHandler::ChunkedHandler() {}

#include "HttpCacheSM.h"
HttpCacheSM::HttpCacheSM() {}

HttpCacheAction::HttpCacheAction() : sm(nullptr) {}
void
HttpCacheAction::cancel(Continuation *c)
{
}

#include "PreWarmManager.h"
void
PreWarmManager::reconfigure()
{
}

PreWarmManager prewarmManager;
