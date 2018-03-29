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

#define PN "ssl_hook_test"
#define PCP "[" PN " Plugin] "

int
ReenableSSL(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(TSContDataGet(cont));
  TSDebug(PN, "Callback reenable ssl_vc=%p", ssl_vc);
  TSVConnReenable(ssl_vc);
  TSContDestroy(cont);
  return TS_SUCCESS;
}

int
CB_Pre_Accept(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  int count = reinterpret_cast<intptr_t>(TSContDataGet(cont));

  TSDebug(PN, "Pre accept callback %d %p - event is %s", count, ssl_vc, event == TS_EVENT_VCONN_START ? "good" : "bad");

  // All done, reactivate things
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

int
CB_Pre_Accept_Delay(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  int count = reinterpret_cast<intptr_t>(TSContDataGet(cont));

  TSDebug(PN, "Pre accept delay callback %d %p - event is %s", count, ssl_vc, event == TS_EVENT_VCONN_START ? "good" : "bad");

  TSCont cb = TSContCreate(&ReenableSSL, TSMutexCreate());

  TSContDataSet(cb, ssl_vc);

  // Schedule to reenable in a bit
  TSContSchedule(cb, 2000, TS_THREAD_POOL_NET);

  return TS_SUCCESS;
}

int
CB_SNI(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  int count = reinterpret_cast<intptr_t>(TSContDataGet(cont));

  TSDebug(PN, "SNI callback %d %p", count, ssl_vc);

  // All done, reactivate things
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

int
CB_Cert_Immediate(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  int count = reinterpret_cast<intptr_t>(TSContDataGet(cont));

  TSDebug(PN, "Cert callback %d ssl_vc=%p", count, ssl_vc);

  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

int
CB_Cert(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  int count = reinterpret_cast<intptr_t>(TSContDataGet(cont));

  TSDebug(PN, "Cert callback %d ssl_vc=%p", count, ssl_vc);

  TSCont cb = TSContCreate(&ReenableSSL, TSMutexCreate());

  TSContDataSet(cb, ssl_vc);

  // Schedule to reenable in a bit
  TSContSchedule(cb, 2000, TS_THREAD_POOL_NET);

  return TS_SUCCESS;
}

void
parse_callbacks(int argc, const char *argv[], int &preaccept_count, int &sni_count, int &cert_count, int &cert_count_immediate,
                int &preaccept_count_delay)
{
  int i = 0;
  const char *ptr;
  for (i = 0; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
      case 'p':
        ptr = index(argv[i], '=');
        if (ptr) {
          preaccept_count = atoi(ptr + 1);
        }
        break;
      case 's':
        ptr = index(argv[i], '=');
        if (ptr) {
          sni_count = atoi(ptr + 1);
        }
        break;
      case 'c':
        ptr = index(argv[i], '=');
        if (ptr) {
          cert_count = atoi(ptr + 1);
        }
        break;
      case 'd':
        ptr = index(argv[i], '=');
        if (ptr) {
          preaccept_count_delay = atoi(ptr + 1);
        }
        break;
      case 'i':
        ptr = index(argv[i], '=');
        if (ptr) {
          cert_count_immediate = atoi(ptr + 1);
        }
        break;
      }
    }
  }
}

void
setup_callbacks(TSHttpTxn txn, int preaccept_count, int sni_count, int cert_count, int cert_count_immediate,
                int preaccept_count_delay)
{
  TSCont cb = nullptr; // pre-accept callback continuation
  int i;

  TSDebug(PN, "Setup callbacks pa=%d sni=%d cert=%d cert_imm=%d pa_delay=%d", preaccept_count, sni_count, cert_count,
          cert_count_immediate, preaccept_count_delay);
  for (i = 0; i < preaccept_count; i++) {
    cb = TSContCreate(&CB_Pre_Accept, TSMutexCreate());
    TSContDataSet(cb, (void *)(intptr_t)i);
    if (txn) {
      TSHttpTxnHookAdd(txn, TS_VCONN_START_HOOK, cb);
    } else {
      TSHttpHookAdd(TS_VCONN_START_HOOK, cb);
    }
  }
  for (i = 0; i < preaccept_count_delay; i++) {
    cb = TSContCreate(&CB_Pre_Accept_Delay, TSMutexCreate());
    TSContDataSet(cb, (void *)(intptr_t)i);
    if (txn) {
      TSHttpTxnHookAdd(txn, TS_VCONN_START_HOOK, cb);
    } else {
      TSHttpHookAdd(TS_VCONN_START_HOOK, cb);
    }
  }
  for (i = 0; i < sni_count; i++) {
    cb = TSContCreate(&CB_SNI, TSMutexCreate());
    TSContDataSet(cb, (void *)(intptr_t)i);
    if (txn) {
      TSHttpTxnHookAdd(txn, TS_SSL_SERVERNAME_HOOK, cb);
    } else {
      TSHttpHookAdd(TS_SSL_SERVERNAME_HOOK, cb);
    }
  }
  for (i = 0; i < cert_count; i++) {
    cb = TSContCreate(&CB_Cert, TSMutexCreate());
    TSContDataSet(cb, (void *)(intptr_t)i);
    if (txn) {
      TSHttpTxnHookAdd(txn, TS_SSL_CERT_HOOK, cb);
    } else {
      TSHttpHookAdd(TS_SSL_CERT_HOOK, cb);
    }
  }
  for (i = 0; i < cert_count_immediate; i++) {
    cb = TSContCreate(&CB_Cert_Immediate, TSMutexCreate());
    TSContDataSet(cb, (void *)(intptr_t)i);
    if (txn) {
      TSHttpTxnHookAdd(txn, TS_SSL_CERT_HOOK, cb);
    } else {
      TSHttpHookAdd(TS_SSL_CERT_HOOK, cb);
    }
  }

  return;
}

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>("SSL hooks test");
  info.vendor_name   = const_cast<char *>("yahoo");
  info.support_email = const_cast<char *>("shinrich@yahoo-inc.com");
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PN);
  }

  int preaccept_count       = 0;
  int sni_count             = 0;
  int cert_count            = 0;
  int cert_count_immediate  = 0;
  int preaccept_count_delay = 0;
  parse_callbacks(argc, argv, preaccept_count, sni_count, cert_count, cert_count_immediate, preaccept_count_delay);
  setup_callbacks(nullptr, preaccept_count, sni_count, cert_count, cert_count_immediate, preaccept_count_delay);
  return;
}
