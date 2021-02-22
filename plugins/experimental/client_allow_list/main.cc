/** @file

  SSL client certificate verification plugin, main source file.

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

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/asn1.h>

#include <tscore/ink_config.h>

#include "client_allow_list.h"

using namespace client_allow_list_plugin;

#if TS_VERSION_MAJOR < 9
#define TSVConnSslConnectionGet TSVConnSSLConnectionGet
#endif

namespace
{
bool
check_names(std::vector<unsigned> const &matcher_idxs, X509 *cert)
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
      std::string_view subj_name{reinterpret_cast<char const *>(ASN1_STRING_get0_data(cn)),
                                 static_cast<std::size_t>(ASN1_STRING_length(cn))};
      TSDebug(PN, "checking cert name %.*s", static_cast<int>(subj_name.size()), subj_name.data());
      retval = check_name(matcher_idxs, subj_name);
    }
  }
  if (!retval) {
    // Check the subjectAltNanes (if present)
    auto names = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
    if (names) {
      unsigned count = sk_GENERAL_NAME_num(names);
      for (unsigned i = 0; i < count && !retval; ++i) {
        GENERAL_NAME *name = sk_GENERAL_NAME_value(names, i);
        if (name->type == GEN_DNS) {
          std::string_view dns{reinterpret_cast<char const *>(ASN1_STRING_get0_data(name->d.dNSName)),
                               static_cast<std::size_t>(ASN1_STRING_length(name->d.dNSName))};
          retval = check_name(matcher_idxs, dns);
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

  std::vector<unsigned> const *matcher_idxs = &none_matcher_idxs;

  // See if we should use a different list of matchers based on Server Name Indication.
  {
    TSSslConnection ssl_conn = TSVConnSslConnectionGet(ssl_vc);

    if (!ssl_conn) {
      TSDebug(PN, "Could not get SSL object for SSL TSVConn %p", ssl_vc);
    } else {
      auto ssl_obj   = reinterpret_cast<SSL *>(ssl_conn);
      int sname_type = SSL_get_servername_type(ssl_obj);
      if (sname_type < 0) {
        TSDebug(PN, "No SNI servername for SSL TSVConn %p", ssl_vc);
      } else {
        // In versions of OpenSSL older than 1.1.1e, this call will return incorrect results in rare cases.
        // But this call is used in core TS, and the failure cases have not caused problems in production.
        //
        char const *sname = SSL_get_servername(ssl_obj, sname_type);
        if (!sname) {
          TSDebug(PN, "SSL_get_servername() call failed for SSL TSVConn %p", ssl_vc);
        } else {
          auto p = sname_to_matcher_idxs.find(sname);
          if (p) {
            TSDebug(PN, "Using specific list of allowed client cert subject/associate names for SNI server name %s", sname);
            matcher_idxs = p;
          } else {
            TSDebug(PN, "No specific list of allowed client cert subject/associate named for SNI server name %s", sname);
            matcher_idxs = &other_matcher_idxs;
          }
        }
      }
    }
  }

  // Is this a good name or not?
  TSEvent reenable_event = TS_EVENT_CONTINUE;
  X509_STORE_CTX *ctx    = reinterpret_cast<X509_STORE_CTX *>(TSVConnSslVerifyCTXGet(ssl_vc));
  if (ctx) {
    STACK_OF(X509) *chain = X509_STORE_CTX_get1_chain(ctx);
    // X509 *cert = X509_STORE_CTX_get_current_cert(ctx);
    bool retval = false;
    for (int i = 0; i < static_cast<int>(sk_X509_num(chain)) && !retval; i++) {
      auto cert = sk_X509_value(chain, i);
      retval    = check_names(*matcher_idxs, cert);
    }
    if (!retval) {
      reenable_event = TS_EVENT_ERROR;
    }
    sk_X509_pop_free(chain, X509_free);
  } else {
    reenable_event = TS_EVENT_ERROR;
  }

  TSDebug(PN, "Client verify callback %p - event is %s %s", ssl_vc, event == TS_EVENT_SSL_VERIFY_CLIENT ? "good" : "bad",
          reenable_event == TS_EVENT_ERROR ? "error HS" : "good HS");

  // All done, reactivate things
  TSVConnReenableEx(ssl_vc, reenable_event);
  return TS_SUCCESS;
}

} // end anonymous namespace

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>("SSL client certificate CN allowlist");
  info.vendor_name   = const_cast<char *>("apache");
  info.support_email = const_cast<char *>("shinrich@apache.org");
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSEmergency("[%s] Plugin registration failed", PN);
  }

  Init()(argc, argv);

  TSCont cb = TSContCreate(&CB_client_verify, TSMutexCreate());
  TSHttpHookAdd(TS_SSL_VERIFY_CLIENT_HOOK, cb);

  TSDebug(PN, "TSPluginInit() complete");

  return;
}
