/** @file
   an example client context dump plugin
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
#include <cstring>
#include <string>
#include <string_view>

#ifdef OPENSSL_NO_SSL_INTERN
#undef OPENSSL_NO_SSL_INTERN
#endif

#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "ts/ts.h"
#include "tscpp/util/TextView.h"

#define PLUGIN_NAME "client_context_dump"
TSTextLogObject context_dump_log;

char *
asn1_string_extract(ASN1_STRING *s)
{
#if OPENSSL_VERSION_NUMBER >= 0x010100000
  return reinterpret_cast<char *>(const_cast<unsigned char *>(ASN1_STRING_get0_data(s)));
#else
  return reinterpret_cast<char *>(ASN1_STRING_data(s));
#endif
}

// For 1.0.2, needs access to internal structure
// For 1.1.0 and 1.1.1, use API
void
dump_context(const char *ca_path, const char *ck_path)
{
  TSSslContext ctx = TSSslClientContextFindByName(ca_path, ck_path);
  if (ctx) {
    SSL *s = SSL_new(reinterpret_cast<SSL_CTX *>(ctx));
    if (s) {
      char *data  = nullptr;
      long length = 0;
      std::string subject_s, san_s, serial_s, time_s;
      X509 *cert = SSL_get_certificate(s);
      if (cert) {
        // Retrieve state info and write to log object
        // expiration date, serial number, common name, and subject alternative names
        const ASN1_TIME *not_after = X509_get_notAfter(cert);
        const ASN1_INTEGER *serial = X509_get_serialNumber(cert);
        X509_NAME *subject_name    = X509_get_subject_name(cert);

        // Subject name
        BIO *subject_bio = BIO_new(BIO_s_mem());
        X509_NAME_print_ex(subject_bio, subject_name, 0, XN_FLAG_RFC2253);
        length = BIO_get_mem_data(subject_bio, &data);
        if (length > 0 && data) {
          subject_s = std::string(data, length);
        }
        length = 0;
        data   = nullptr;
        BIO_free(subject_bio);

        // Subject Alternative Name
        GENERAL_NAMES *names = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
        if (names) {
          unsigned count = sk_GENERAL_NAME_num(names);
          for (unsigned i = 0; i < count; ++i) {
            GENERAL_NAME *name = sk_GENERAL_NAME_value(names, i);
            data               = nullptr;
            length             = 0;
            switch (name->type) {
            case (GEN_EMAIL): {
              data   = asn1_string_extract(name->d.rfc822Name);
              length = ASN1_STRING_length(name->d.rfc822Name);
              break;
            }
            case (GEN_DNS): {
              data   = asn1_string_extract(name->d.dNSName);
              length = ASN1_STRING_length(name->d.dNSName);
              break;
            }
            case (GEN_URI): {
              data   = asn1_string_extract(name->d.uniformResourceIdentifier);
              length = ASN1_STRING_length(name->d.uniformResourceIdentifier);
              break;
            }
            default:
              break;
            }
            if (data) {
              san_s.append(data, length);
              san_s.push_back(',');
            }
          }
          if (san_s.back() == ',') {
            san_s.pop_back();
          }
        }

        // Serial number
        int64_t sn = 0;
#if OPENSSL_VERSION_NUMBER >= 0x010100000
        ASN1_INTEGER_get_int64(&sn, serial);
#else
        sn = ASN1_INTEGER_get(serial);
#endif
        if (sn != 0 && sn != -1) {
          serial_s = std::to_string(sn);
        }

        // Expiration
        BIO *time_bio = BIO_new(BIO_s_mem());
        ASN1_TIME_print(time_bio, not_after);
        length = BIO_get_mem_data(time_bio, &data);
        time_s = std::string(data, length);
        BIO_free(time_bio);
        TSDebug(PLUGIN_NAME, "LookupName: %s:%s, Subject: %s. SAN: %s. Serial: %s. NotAfter: %s.", ca_path, ck_path,
                subject_s.c_str(), san_s.c_str(), serial_s.c_str(), time_s.c_str());
        TSTextLogObjectWrite(context_dump_log, "LookupName: %s:%s, Subject: %s. SAN: %s. Serial: %s. NotAfter: %s.", ca_path,
                             ck_path, subject_s.c_str(), san_s.c_str(), serial_s.c_str(), time_s.c_str());
      }
    }
    SSL_free(s);
    TSSslContextDestroy(ctx);
  }
}

// Plugin Message Continuation
int
CB_context_dump(TSCont, TSEvent, void *edata)
{
  TSPluginMsg *msg = static_cast<TSPluginMsg *>(edata);
  static constexpr std::string_view PLUGIN_PREFIX("client_context_dump."_sv);

  std::string_view tag(msg->tag, strlen(msg->tag));

  if (tag.substr(0, PLUGIN_PREFIX.size()) == PLUGIN_PREFIX) {
    tag.remove_prefix(PLUGIN_PREFIX.size());
    // Grab all keys by API and dump to log file according to arg passed in
    int count = 0;
    TSSslClientContextsNamesGet(0, nullptr, &count);
    if (count > 0) {
      char const **results = static_cast<char const **>(malloc(sizeof(const char *) * count));
      TSSslClientContextsNamesGet(count, results, nullptr);
      for (int i = 0; i < count; i += 2) {
        dump_context(results[i], results[i + 1]);
      }
    }
  }
  TSTextLogObjectFlush(context_dump_log);
  return TS_SUCCESS;
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
    return;
  }
  if (TSTextLogObjectCreate(PLUGIN_NAME, TS_LOG_MODE_ADD_TIMESTAMP, &context_dump_log) != TS_SUCCESS || !context_dump_log) {
    TSError("[%s] Failed to create log file", PLUGIN_NAME);
    return;
  }
  TSDebug(PLUGIN_NAME, "Initialized.");
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(CB_context_dump, nullptr));
}
