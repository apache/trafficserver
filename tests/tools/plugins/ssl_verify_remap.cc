/** @file

  A plugin verifies DNS name in X509 certificate.

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

#include "ts/ts.h"
#include "ts/remap.h"
#include <cstring>
#include <openssl/x509v3.h>

#define PLUGIN_NAME "ssl_verify_remap"

int
ssl_verify_callback(void *arg, int preverify_ok, X509_STORE_CTX *x509_ctx)
{
  if (preverify_ok == 0) {
    return 0;
  }

  const int depth = X509_STORE_CTX_get_error_depth(x509_ctx);
  if (depth != 0) {
    return preverify_ok;
  }

  X509 *cert                       = X509_STORE_CTX_get_current_cert(x509_ctx);
  GENERAL_NAMES *subject_alt_names = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
  if (subject_alt_names == nullptr) {
    return 0;
  }

  const char *dns_to_match = static_cast<char *>(arg);
  const int alt_name_count = sk_GENERAL_NAME_num(subject_alt_names);
  bool found               = false;
  for (int i = 0; i < alt_name_count; ++i) {
    const GENERAL_NAME *current_name = sk_GENERAL_NAME_value(subject_alt_names, i);
    if (current_name->type == GEN_DNS) {
      ASN1_IA5STRING *name_dns = current_name->d.ia5;
      const char *dns_str      = reinterpret_cast<const char *>(ASN1_STRING_get0_data(name_dns));
      const size_t dns_len     = static_cast<size_t>(ASN1_STRING_length(name_dns));
      if (strncmp(dns_to_match, dns_str, dns_len) == 0) {
        found = true;
        break;
      }
    }
  }
  if (found) {
    return 1;
  } else {
    return 0;
  }
}

int
create_verify_callback(TSCont contp, TSEvent event, void *edata)
{
  const TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  TSVConn vc = TSHttpTxnServerVConnGet(txnp);
  if (vc == nullptr) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_EVENT_NONE;
  }

  if (!TSVConnVerifyCallbackSet(vc, reinterpret_cast<void *>(&ssl_verify_callback), TSContDataGet(contp))) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_EVENT_NONE;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    TSError("[%s] Unable to initialize plugin (disabled)", PLUGIN_NAME);
  }

  return;
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_sizeATS_UNUSED */)
{
  if (argc < 3) {
    return TS_ERROR;
  } else {
    *ih = TSstrdup(argv[2]);
    return TS_SUCCESS;
  }
}

void
TSRemapDeleteInstance(void *ih)
{
  TSfree(ih);
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  if (ih != nullptr) {
    const TSCont create_verify_callback_contp = TSContCreate(create_verify_callback, nullptr);
    TSContDataSet(create_verify_callback_contp, ih);
    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, create_verify_callback_contp);
  }

  return TSREMAP_NO_REMAP;
}
