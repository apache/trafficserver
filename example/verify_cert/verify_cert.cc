/** @file

  SSL Preaccept test plugin.

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

#include <cstdio>
#include <memory.h>
#include <cinttypes>
#include <ts/ts.h>
#include <tsconfig/TsValue.h>
#include <openssl/ssl.h>
#include <getopt.h>

using ts::config::Configuration;
using ts::config::Value;

#define PLUGIN_NAME "verify_cert"
#define PCP "[" PLUGIN_NAME "] "

namespace
{
static void
debug_certificate(const char *msg, X509_NAME *name)
{
  BIO *bio;

  if (name == NULL) {
    return;
  }

  bio = BIO_new(BIO_s_mem());
  if (bio == NULL) {
    return;
  }

  if (X509_NAME_print_ex(bio, name, 0 /* indent */, XN_FLAG_ONELINE) > 0) {
    long len;
    char *ptr;
    len = BIO_get_mem_data(bio, &ptr);
    TSDebug(PLUGIN_NAME, "%s %.*s", msg, (int)len, ptr);
  }

  BIO_free(bio);
}

int
CB_clientcert(TSCont /* contp */, TSEvent /* event */, void *edata)
{
  TSVConn ssl_vc         = reinterpret_cast<TSVConn>(edata);
  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
  X509 *cert             = SSL_get_peer_certificate(ssl);
  TSDebug(PLUGIN_NAME, "plugin verify_cert verifying client certificate");
  if (cert) {
    debug_certificate("client certificate subject CN is %s", X509_get_subject_name(cert));
    debug_certificate("client certificate issuer CN is %s", X509_get_issuer_name(cert));
    X509_free(cert);
  }
  // All done, reactivate things
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

} // Anon namespace

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  bool success = false;
  TSPluginRegistrationInfo info;
  TSCont cb_cert                       = nullptr; // Certificate callback continuation
  static const struct option longopt[] = {
    {const_cast<char *>("config"), required_argument, nullptr, 'c'}, {nullptr, no_argument, nullptr, '\0'},
  };

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError(PCP "registration failed");
  } else if (nullptr == (cb_cert = TSContCreate(&CB_clientcert, TSMutexCreate()))) {
    TSError(PCP "Failed to create cert callback");
  } else {
    TSHttpHookAdd(TS_SSL_VERIFY_CLIENT_HOOK, cb_cert);
    success = true;
  }

  if (!success) {
    TSError(PCP "not initialized");
  }
  TSDebug(PLUGIN_NAME, "Plugin %s", success ? "online" : "offline");

  return;
}
