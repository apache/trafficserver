/** @file

  SSL Preaccept test plugin
  Implements blind tunneling based on the client IP address
  The client ip addresses are specified in the plugin's
  config file as an array of IP addresses or IP address ranges under the
  key "client-blind-tunnel"

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

#include <ts/ts.h>
#include <ts/remap.h>
#include <getopt.h>
#include <openssl/ssl.h>
#include <strings.h>
#include <string>
#include <map>

#define PN "ssl_verify_test"
#define PCP "[" PN " Plugin] "

std::map<std::string, int> bad_names;

int
CB_server_verify(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  int count = reinterpret_cast<intptr_t>(TSContDataGet(cont));

  // Is this a good name or not?
  TSEvent reenable_event       = TS_EVENT_CONTINUE;
  TSSslConnection const sslobj = TSVConnSslConnectionGet(ssl_vc);
  SSL const *const ssl         = reinterpret_cast<SSL *>(sslobj);
  char const *const sni_name   = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (sni_name) {
    std::string sni_string(sni_name);
    if (bad_names.find(sni_string) != bad_names.end()) {
      reenable_event = TS_EVENT_ERROR;
    }

    TSDebug(PN, "Server verify callback %d %p - event is %s SNI=%s %s", count, ssl_vc,
            event == TS_EVENT_SSL_VERIFY_SERVER ? "good" : "bad", sni_name,
            reenable_event == TS_EVENT_ERROR ? "error HS" : "good HS");

    int len;
    char const *const method2_name = TSVConnSslSniGet(ssl_vc, &len);
    TSDebug(PN, "Server verify callback SNI APIs match=%s", 0 == strncmp(method2_name, sni_name, len) ? "true" : "false");
  } else {
    TSDebug(PN, "SSL_get_servername failed");
    reenable_event = TS_EVENT_ERROR;
  }

  // All done, reactivate things
  TSVConnReenableEx(ssl_vc, reenable_event);
  return TS_SUCCESS;
}

void
parse_callbacks(int argc, const char *argv[], int &count)
{
  int i = 0;
  const char *ptr;
  for (i = 0; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
      case 'c':
        ptr = index(argv[i], '=');
        if (ptr) {
          count = atoi(ptr + 1);
        }
        break;
      case 'b':
        ptr = index(argv[i], '=');
        if (ptr) {
          bad_names.insert(std::pair<std::string, int>(std::string(ptr + 1), 1));
        }
        break;
      }
    }
  }
}

void
setup_callbacks(int count)
{
  TSCont cb = nullptr;
  int i;

  TSDebug(PN, "Setup callbacks count=%d", count);
  for (i = 0; i < count; i++) {
    cb = TSContCreate(&CB_server_verify, TSMutexCreate());
    TSContDataSet(cb, (void *)static_cast<intptr_t>(i));
    TSHttpHookAdd(TS_SSL_VERIFY_SERVER_HOOK, cb);
  }
  return;
}

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>("SSL verify server test");
  info.vendor_name   = const_cast<char *>("apache");
  info.support_email = const_cast<char *>("shinrich@apache.org");
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PN);
  }

  int verify_count = 0;
  parse_callbacks(argc, argv, verify_count);
  setup_callbacks(verify_count);
  return;
}
