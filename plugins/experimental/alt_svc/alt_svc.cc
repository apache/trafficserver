/**
  @file
  @brief Adds an Alt-Svc header based on admin-provided routing configuration.

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

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/PluginInit.h>
#include "ip_host_map.h"
#include "default.h"

using namespace atscppapi;
using namespace std;

// plugin registration info
static char vendor_name[]   = "Yahoo! Inc.";
static char support_email[] = "ats-devel@yahoo-inc.com";

namespace
{
GlobalPlugin *plugin;
}

class AltSvcHeaderPlugin : public GlobalPlugin
{
private:
  unique_ptr<IpHostMap> _hostmap;

public:
  AltSvcHeaderPlugin(unique_ptr<IpHostMap> hostmap)
  {
    // Only if the map is initialized properly do we then add the hook.
    registerHook(HOOK_SEND_RESPONSE_HEADERS);
    this->_hostmap = move(hostmap);
  }

  void
  handleSendResponseHeaders(Transaction &transaction)
  {
    if (this->_hostmap->isValid()) {
      const sockaddr *client_address = transaction.getClientAddress();
      string host                    = this->_hostmap->findHostForIP(client_address);
      if (!host.empty()) {
        TS_DEBUG(PLUGIN_NAME, "Found hostname %s", host.c_str());
        transaction.getClientResponse().getHeaders().append("Alt-Svc", "h2=\"" + host + ":443\"");
      }

      transaction.resume();
    }
  }
};

void
TSPluginInit(int argc, const char *argv[])
{
  RegisterGlobalPlugin(PLUGIN_NAME, vendor_name, support_email);
  // assert argc == 2, argv[1] is our filename. TODO how to write asserts?
  if (argc > 1) {
    unique_ptr<IpHostMap> hostmap(new SingleServiceFileMap(argv[1]));
    plugin = new AltSvcHeaderPlugin(move(hostmap));
  } else {
    TS_DEBUG(PLUGIN_NAME, "File not found at location %s", argv[1]);
  }
}
