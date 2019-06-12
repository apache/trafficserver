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

#define PN "ssl_rename_test"
#define PCP "[" PN " Plugin] "

std::map<std::string, int> bad_names;

int
CB_server_rename(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl               = (SSL *)sslobj;
  const char *sni_name   = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (!sni_name) {
    SSL_set_tlsext_host_name(ssl, "newname");
  }

  // All done, reactivate things
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>("SSL rename test");
  info.vendor_name   = const_cast<char *>("apache");
  info.support_email = const_cast<char *>("shinrich@apache.org");
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PN);
  }
  TSCont cb = TSContCreate(&CB_server_rename, TSMutexCreate());
  TSHttpHookAdd(TS_SSL_SERVERNAME_HOOK, cb);

  return;
}
