/**
  @file
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


# include <stdio.h>
# include <memory.h>
# include <inttypes.h>
# include <ts/ink_config.h>
# include <tsconfig/TsValue.h>
# include <openssl/ssl.h>
# include <getopt.h>
# include <ts/ts.h>

using ts::config::Configuration;
using ts::config::Value;

# define PN "ssl-session-test"
# define PCP "[" PN " Plugin] "

# if TS_USE_TLS_SNI

namespace {

/**
 * Test out the new TS_SSL_SESSION_HOOK
 */
int
CB_session(TSCont /* contp */, TSEvent event, void *edata)
{
  char buffer[1024];
  TSSslSessionID *session = reinterpret_cast<TSSslSessionID *>(edata);
  sprintf(buffer, "CB_session event=%d #bytes=%d session_id=", event, (int)session->len);
  for (size_t i = 0; i < session->len; i++) {
    char val[50];
    sprintf(val, "%x", session->bytes[i]);
    strcat(buffer, val);
  } 
  TSDebug("skh", buffer);
  if (event == TS_EVENT_SSL_SESSION_GET) {
    // Could update a stat or a last used timestamp
  } else if (event == TS_EVENT_SSL_SESSION_NEW) {
    // Turn around and fetch it again
    TSSslSession session2 = TSSslSessionGet(session);
    if (session2) {
      TSDebug("skh", "CB_session got session");
      SSL_SESSION_free((SSL_SESSION*)session2);
      int len = 0;
      int true_len = TSSslSessionGetBuffer(session, NULL, &len);
      TSDebug("skh", "CB_session serialized length %d", true_len);
    } else {
      TSDebug("skh", "CB_session failed to get session");
    }
  } else if (event == TS_EVENT_SSL_SESSION_REMOVE) {
    TSSslSession session2 = TSSslSessionGet(session);
    if (session2) {
      TSDebug("skh", "CB_session got removing session");
    } else {
      TSDebug("skh", "CB_session failed to get removing session");
    }
  }
  return TS_SUCCESS;
}

} // Anon namespace

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  bool success = false;
  TSPluginRegistrationInfo info;
  TSCont cb_session = 0;

  info.plugin_name = const_cast<char*>("SSL Session callback test");
  info.vendor_name = const_cast<char*>("Network Geographics");
  info.support_email = const_cast<char*>("shinrich@network-geographics.com");

  if (TS_SUCCESS != TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError(PCP "registration failed.");
  } else if (TSTrafficServerVersionGetMajor() < 2) {
    TSError(PCP "requires Traffic Server 2.0 or later.");
  } else if (0 == (cb_session = TSContCreate(&CB_session, TSMutexCreate()))) {
    TSError(PCP "Failed to create session callback.");
  } else {
    TSHttpHookAdd(TS_SSL_SESSION_HOOK, cb_session);
    success = true;
  }

  if (!success) {
    TSError(PCP "not initialized");
  }
  TSDebug(PN, "Plugin %s", success ? "online" : "offline");

  return;
}

# else // ! TS_USE_TLS_SNI

void
TSPluginInit(int, const char *[])
{
    TSError(PCP "requires TLS SNI which is not available.");
}

# endif // TS_USE_TLS_SNI
