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

#include "ink_config.h"

#include "P_Net.h"
#include "I_Layout.h"
#include "I_RecHttp.h"
#include "P_SSLUtils.h"

//
// Global Data
//

SSLNetProcessor   ssl_NetProcessor;
NetProcessor&     sslNetProcessor = ssl_NetProcessor;
EventType         SSLNetProcessor::ET_SSL;

void
SSLNetProcessor::cleanup(void)
{
  if (client_ctx) {
    SSL_CTX_free(client_ctx);
    client_ctx = NULL;
  }
}

int
SSLNetProcessor::start(int number_of_ssl_threads, size_t stacksize)
{
  // This initialization order matters ...
  SSLInitializeLibrary();
  SSLConfig::startup();

  if (HttpProxyPort::hasSSL()) {
    SSLCertificateConfig::startup();
  }

  // Acquire a SSLConfigParams instance *after* we start SSL up.
  SSLConfig::scoped_config params;

  // Enable client regardless of config file setttings as remap file
  // can cause HTTP layer to connect using SSL. But only if SSL
  // initialization hasn't failed already.
  client_ctx = SSLInitClientContext(params);
  if (!client_ctx) {
    SSLError("Can't initialize the SSL client, HTTPS in remap rules will not function");
  }

  if (number_of_ssl_threads < 1) {
    return -1;
  }

  SSLNetProcessor::ET_SSL = eventProcessor.spawn_event_threads(number_of_ssl_threads, "ET_SSL", stacksize);
  return UnixNetProcessor::start(0, stacksize);
}

NetAccept *
SSLNetProcessor::createNetAccept()
{
  return ((NetAccept *) NEW(new SSLNetAccept));
}

// Virtual function allows etype to be upgraded to ET_SSL for SSLNetProcessor.  Does
// nothing for NetProcessor
void
SSLNetProcessor::upgradeEtype(EventType & etype)
{
  if (etype == ET_NET) {
    etype = ET_SSL;
  }
}

// Functions all THREAD_FREE and THREAD_ALLOC to be performed
// for both SSL and regular NetVConnection transparent to
// netProcessor connect functions. Yes it looks goofy to
// have them in both places, but it saves a bunch of
// connect code from being duplicated.
UnixNetVConnection *
SSLNetProcessor::allocateThread(EThread *t)
{
  return ((UnixNetVConnection *) THREAD_ALLOC(sslNetVCAllocator, t));
}

void
SSLNetProcessor::freeThread(UnixNetVConnection *vc, EThread *t)
{
  ink_assert(!vc->from_accept_thread);
  THREAD_FREE((SSLNetVConnection *) vc, sslNetVCAllocator, t);
}

SSLNetProcessor::SSLNetProcessor()
  : client_ctx(NULL)
{
}

SSLNetProcessor::~SSLNetProcessor()
{
  cleanup();
}
