/** @file

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

#include "tscore/ink_config.h"

#include "P_Net.h"
#include "tscore/I_Layout.h"
#include "records/I_RecHttp.h"
#include "P_SSLUtils.h"
#include "P_OCSPStapling.h"
#include "P_SSLSNI.h"
#include "SSLStats.h"

//
// Global Data
//

SSLNetProcessor ssl_NetProcessor;
NetProcessor &sslNetProcessor = ssl_NetProcessor;

#if TS_USE_TLS_OCSP
struct OCSPContinuation : public Continuation {
  int
  mainEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    ocsp_update();

    return EVENT_CONT;
  }

  OCSPContinuation() : Continuation(new_ProxyMutex()) { SET_HANDLER(&OCSPContinuation::mainEvent); }
};
#endif /* TS_USE_TLS_OCSP */

void
SSLNetProcessor::cleanup()
{
}

int
SSLNetProcessor::start(int, size_t stacksize)
{
  // This initialization order matters ...
  SSLInitializeLibrary();
  SSLConfig::startup();
  SSLPostConfigInitialize();
  SNIConfig::startup();

  if (!SSLCertificateConfig::startup()) {
    return -1;
  }
  SSLTicketKeyConfig::startup();

  // Acquire a SSLConfigParams instance *after* we start SSL up.
  // SSLConfig::scoped_config params;

  // Initialize SSL statistics. This depends on an initial set of certificates being loaded above.
  SSLInitializeStatistics();

#if TS_USE_TLS_OCSP
  if (SSLConfigParams::ssl_ocsp_enabled) {
    // Call the update initially to get things populated
    ocsp_update();
    EventType ET_OCSP = eventProcessor.spawn_event_threads("ET_OCSP", 1, stacksize);
    eventProcessor.schedule_every(new OCSPContinuation(), HRTIME_SECONDS(SSLConfigParams::ssl_ocsp_update_period), ET_OCSP);
  }
#endif /* TS_USE_TLS_OCSP */

  // We have removed the difference between ET_SSL threads and ET_NET threads,
  // So just keep on chugging
  return 0;
}

NetAccept *
SSLNetProcessor::createNetAccept(const NetProcessor::AcceptOptions &opt)
{
  return (NetAccept *)new SSLNetAccept(opt);
}

NetVConnection *
SSLNetProcessor::allocate_vc(EThread *t)
{
  SSLNetVConnection *vc;

  if (t) {
    vc = THREAD_ALLOC_INIT(sslNetVCAllocator, t);
  } else {
    if (likely(vc = sslNetVCAllocator.alloc())) {
      vc->from_accept_thread = true;
    }
  }

  return vc;
}

SSLNetProcessor::SSLNetProcessor() {}

SSLNetProcessor::~SSLNetProcessor()
{
  cleanup();
}
