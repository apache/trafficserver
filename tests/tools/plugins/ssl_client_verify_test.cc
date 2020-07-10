/** @file

  SSL client certificate verification plugin
  Checks for specificate names in the client provided certificate and
  fails the handshake if none of the good names are present

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
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/asn1.h>
#include <strings.h>
#include <string.h>
#include <string>
#include <map>

#define PN "ssl_client_verify_test"
#define PCP "[" PN " Plugin] "

std::map<std::string, int> good_names;

bool
check_name(std::string name)
{
  auto entry = good_names.find(name);
  return entry != good_names.end();
}

bool
check_names(X509 *cert)
{
  bool retval = false;

  // Check the common name
  X509_NAME *subject = X509_get_subject_name(cert);
  if (subject) {
    int pos = -1;
    for (; !retval;) {
      pos = X509_NAME_get_index_by_NID(subject, NID_commonName, pos);
      if (pos == -1) {
        break;
      }

      X509_NAME_ENTRY *e = X509_NAME_get_entry(subject, pos);
      ASN1_STRING *cn    = X509_NAME_ENTRY_get_data(e);
#if OPENSSL_VERSION_NUMBER >= 0x010100000
      char *subj_name = strndup(reinterpret_cast<const char *>(ASN1_STRING_get0_data(cn)), ASN1_STRING_length(cn));
#else
      char *subj_name = strndup(reinterpret_cast<const char *>(ASN1_STRING_data(cn)), ASN1_STRING_length(cn));
#endif
      retval = check_name(subj_name);
      free(subj_name);
    }
  }
  if (!retval) {
    // Check the subjectAltNanes (if present)
    GENERAL_NAMES *names = (GENERAL_NAMES *)X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr);
    if (names) {
      unsigned count = sk_GENERAL_NAME_num(names);
      for (unsigned i = 0; i < count && !retval; ++i) {
        GENERAL_NAME *name;

        name = sk_GENERAL_NAME_value(names, i);
        if (name->type == GEN_DNS) {
          char *dns =
#if OPENSSL_VERSION_NUMBER >= 0x010100000
            strndup(reinterpret_cast<const char *>(ASN1_STRING_get0_data(name->d.dNSName)), ASN1_STRING_length(name->d.dNSName));
#else
            strndup(reinterpret_cast<const char *>(ASN1_STRING_data(name->d.dNSName)), ASN1_STRING_length(name->d.dNSName));
#endif
          retval = check_name(dns);
          free(dns);
        }
      }
      GENERAL_NAMES_free(names);
    }
  }
  return retval;
}

int
CB_client_verify(TSCont cont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  int count = reinterpret_cast<intptr_t>(TSContDataGet(cont));

  // Is this a good name or not?
  TSEvent reenable_event = TS_EVENT_CONTINUE;
  X509_STORE_CTX *ctx    = reinterpret_cast<X509_STORE_CTX *>(TSVConnSslVerifyCTXGet(ssl_vc));
  if (ctx) {
    STACK_OF(X509) *chain = X509_STORE_CTX_get1_chain(ctx);
    // X509 *cert = X509_STORE_CTX_get_current_cert(ctx);
    bool retval = false;
    // BoringSSL has sk_X509_num() return size_t.
    for (int i = 0; i < static_cast<int>(sk_X509_num(chain)) && !retval; i++) {
      auto cert = sk_X509_value(chain, i);
      retval    = check_names(cert);
    }
    if (!retval) {
      reenable_event = TS_EVENT_ERROR;
    }
  } else {
    reenable_event = TS_EVENT_ERROR;
  }

  TSDebug(PN, "Client verify callback %d %p - event is %s %s", count, ssl_vc, event == TS_EVENT_SSL_VERIFY_CLIENT ? "good" : "bad",
          reenable_event == TS_EVENT_ERROR ? "error HS" : "good HS");

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
      case 'g':
        ptr = index(argv[i], '=');
        if (ptr) {
          good_names.insert(std::pair<std::string, int>(std::string(ptr + 1), 1));
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
    cb = TSContCreate(&CB_client_verify, TSMutexCreate());
    TSContDataSet(cb, (void *)(intptr_t)i);
    TSHttpHookAdd(TS_SSL_VERIFY_CLIENT_HOOK, cb);
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
