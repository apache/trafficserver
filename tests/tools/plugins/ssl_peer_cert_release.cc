/** @file

  Count client certificates that OpenSSL actually frees.

  At transaction close, the inbound connection's peer certificate is tagged
  with X509 ex_data and the tagged metric is incremented. OpenSSL calls the
  ex_data free callback only when the last reference to the certificate is
  dropped, which then increments the released metric. If every reference
  taken while processing a connection is balanced, the two metrics match once
  the connections close. A reference that is never dropped keeps the
  certificate, and thus the released count, from ever catching up.

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
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace
{
constexpr char PN[] = "ssl_peer_cert_release";

DbgCtl dbg_ctl{PN};

int tagged_stat   = -1;
int released_stat = -1;
int ex_data_index = -1;

/// Address stored as the ex_data value to recognize certificates this plugin tagged.
char tag_marker;

void
on_x509_free(void * /* parent */, void *ptr, CRYPTO_EX_DATA * /* ad */, int /* idx */, long /* argl */, void * /* argp */)
{
  if (ptr == &tag_marker) {
    TSStatIntIncrement(released_stat, 1);
  }
}

/// Tag @a cert unless an earlier transaction on the same connection already did.
void
tag_certificate(X509 *cert)
{
  if (X509_get_ex_data(cert, ex_data_index) == nullptr) {
    X509_set_ex_data(cert, ex_data_index, &tag_marker);
    TSStatIntIncrement(tagged_stat, 1);
    Dbg(dbg_ctl, "Tagged peer certificate %p", cert);
  }
}

int
on_txn_close(TSCont /* contp */, TSEvent /* event */, void *edata)
{
  auto txnp = static_cast<TSHttpTxn>(edata);

  if (auto vc = TSHttpSsnClientVConnGet(TSHttpTxnSsnGet(txnp)); vc != nullptr && TSVConnIsSsl(vc)) {
    if (auto ssl = reinterpret_cast<SSL *>(TSVConnSslConnectionGet(vc)); ssl != nullptr) {
      // Avoid changing the certificate's reference count so the plugin cannot mask or cause a leak.
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
      if (X509 *cert = SSL_get0_peer_certificate(ssl); cert != nullptr) {
        tag_certificate(cert);
      }
#else
      if (X509 *cert = SSL_get_peer_certificate(ssl); cert != nullptr) {
        tag_certificate(cert);
        X509_free(cert);
      }
#endif
    }
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}
} // namespace

void
TSPluginInit(int /* argc */, const char * /* argv */[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PN;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PN);
    return;
  }

  ex_data_index = X509_get_ex_new_index(0, nullptr, nullptr, nullptr, on_x509_free);
  if (ex_data_index < 0) {
    TSError("[%s] Unable to allocate an X509 ex_data index", PN);
    return;
  }
  tagged_stat =
    TSStatCreate("plugin.ssl_peer_cert_release.tagged", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  released_stat =
    TSStatCreate("plugin.ssl_peer_cert_release.released", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, TSContCreate(on_txn_close, nullptr));
}
