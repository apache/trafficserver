/** @file

  SSL Context management

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

#include "ink_config.h"

#include "P_SSLCertLookup.h"
#include "P_UnixNet.h"
#include "I_Layout.h"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>
#include <openssl/ts.h>

#if (OPENSSL_VERSION_NUMBER >= 0x10000000L) // openssl returns a const SSL_METHOD
typedef const SSL_METHOD * ink_ssl_method_t;
#else
typedef SSL_METHOD * ink_ssl_method_t;
#endif

SSLCertLookup sslCertLookup;

static void
insert_ssl_certificate(InkHashTable *, SSL_CTX *, const char *);

#define SSL_IP_TAG "dest_ip"
#define SSL_CERT_TAG "ssl_cert_name"
#define SSL_PRIVATE_KEY_TAG "ssl_key_name"
#define SSL_CA_TAG "ssl_ca_name"
const char *moduleName = "SSLCertLookup";

const matcher_tags sslCertTags = {
  NULL, NULL, SSL_IP_TAG, NULL, NULL, false
};

SSLCertLookup::SSLCertLookup():
param(NULL), multipleCerts(false)
{
  SSLCertLookupHashTable = ink_hash_table_create(InkHashTableKeyType_String);
  *config_file_path = '\0';
}

void
SSLCertLookup::init(SslConfigParams * p)
{
  param = p;
  multipleCerts = buildTable();
}

bool
SSLCertLookup::buildTable()
{
  char *tok_state = NULL;
  char *line = NULL;
  const char *errPtr = NULL;
  char errBuf[1024];
  char *file_buf = NULL;
  int line_num = 0;
  bool ret = 0;
  char *addr = NULL;
  char *sslCert = NULL;
  char *sslCa = NULL;
  char *priKey = NULL;
  matcher_line line_info;
  bool alarmAlready = false;
  char *configFilePath = NULL;

  if (param != NULL)
    configFilePath = param->getConfigFilePath();

  // Table should be empty
//  ink_assert(num_el == 0);

  Debug("ssl", "ssl_multicert.config: %s", configFilePath);
  if (configFilePath)
    file_buf = readIntoBuffer(configFilePath, moduleName, NULL);

  if (file_buf == NULL) {
    Warning("%s Failed to read %s. Using default server cert for all connections", moduleName, configFilePath);
    return ret;
  }

  line = tokLine(file_buf, &tok_state);
  while (line != NULL) {

    line_num++;

    // skip all blank spaces at beginning of line
    while (*line && isspace(*line)) {
      line++;
    }

    if (*line != '\0' && *line != '#') {

      errPtr = parseConfigLine(line, &line_info, &sslCertTags);

      if (errPtr != NULL) {
        snprintf(errBuf, 1024, "%s discarding %s entry at line %d : %s",
                     moduleName, configFilePath, line_num, errPtr);
        IOCORE_SignalError(errBuf, alarmAlready);
      } else {
        ink_assert(line_info.type == MATCH_IP);

        errPtr = extractIPAndCert(&line_info, &addr, &sslCert, &sslCa, &priKey);

        if (errPtr != NULL) {
          snprintf(errBuf, 1024, "%s discarding %s entry at line %d : %s",
                       moduleName, configFilePath, line_num, errPtr);
          IOCORE_SignalError(errBuf, alarmAlready);
        } else {
          if (addr != NULL && sslCert != NULL) {
            addInfoToHash(addr, sslCert, sslCa, priKey);
            ret = 1;
          }
          ats_free(sslCert);
          ats_free(sslCa);
          ats_free(priKey);
          ats_free(addr);
          addr = NULL;
          sslCert = NULL;
          priKey = NULL;
        }
      }                         // else
    }                           // if(*line != '\0' && *line != '#')

    line = tokLine(NULL, &tok_state);
  }                             //  while(line != NULL)

/*  if(num_el == 0)
  {
    Warning("%s No entries in %s. Using default server cert for all connections",
	    moduleName, configFilePath);
  }

  if(is_debug_tag_set("ssl"))
  {
    Print();
  }
*/
  ats_free(file_buf);
  return ret;
}

const char *
SSLCertLookup::extractIPAndCert(matcher_line * line_info, char **addr, char **cert, char **ca, char **priKey) const
{
//  ip_addr_t testAddr;
  char *label;
  char *value;

  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {

    label = line_info->line[0][i];
    value = line_info->line[1][i];

    if (label == NULL) {
      continue;
    }

    if (strcasecmp(label, SSL_IP_TAG) == 0) {
      if (value != NULL) {
        int buf_len = sizeof(char) * (strlen(value) + 1);

        *addr = (char *)ats_malloc(buf_len);
        ink_strlcpy(*addr, (const char *) value, buf_len);
//              testAddr = inet_addr (addr);
      }
    }

    if (strcasecmp(label, SSL_CERT_TAG) == 0) {
      if (value != NULL) {
        int buf_len = sizeof(char) * (strlen(value) + 1);

        *cert = (char *)ats_malloc(buf_len);
        ink_strlcpy(*cert, (const char *) value, buf_len);
      }
    }

    if (strcasecmp(label, SSL_CA_TAG) == 0) {
      if (value != NULL) {
        int buf_len = sizeof(char) * (strlen(value) + 1);

        *ca = (char *)ats_malloc(buf_len);
        ink_strlcpy(*ca, (const char *) value, buf_len);
      }
    }

    if (strcasecmp(label, SSL_PRIVATE_KEY_TAG) == 0) {
      if (value != NULL) {
        int buf_len = sizeof(char) * (strlen(value) + 1);

        *priKey = (char *)ats_malloc(buf_len);
        ink_strlcpy(*priKey, (const char *) value, buf_len);
      }
    }
  }

  if ( /*testAddr == INADDR_NONE || */ addr != NULL && cert == NULL)
    return "Bad address or certificate.";
  else
    return NULL;
}

bool
SSLCertLookup::addInfoToHash(
    const char *strAddr, const char *cert,
    const char *caCert, const char *serverPrivateKey) const
{
  ink_ssl_method_t meth = NULL;

  meth = SSLv23_server_method();
  SSL_CTX *ctx = SSL_CTX_new(meth);
  if (!ctx) {
    SSLNetProcessor::logSSLError("Cannot create new server contex.");
    return (false);
  }
//  if (serverPrivateKey == NULL)
//      serverPrivateKey = cert;

  if (ssl_NetProcessor.initSSLServerCTX(ctx, this->param, cert, caCert, serverPrivateKey, false) == 0) {
    char * certpath = Layout::relative_to(this->param->getServerCertPathOnly(), cert);
    ink_hash_table_insert(SSLCertLookupHashTable, strAddr, (void *) ctx);

    // Insert additional mappings. Note that this maps multiple keys to the same value, so when
    // this code is updated to reconfigure the SSL certificates, it will need some sort of
    // refcounting or alternate way of avoiding double frees.
    insert_ssl_certificate(SSLCertLookupHashTable, ctx, certpath);

    ats_free(certpath);
    return (true);
  }

  SSL_CTX_free(ctx);
  return (false);
}

SSL_CTX *
SSLCertLookup::findInfoInHash(char *strAddr) const
{

  InkHashTableValue hash_value;
  if (ink_hash_table_lookup(SSLCertLookupHashTable, strAddr, &hash_value) == 0) {
    return NULL;
  } else {
    return (SSL_CTX *) hash_value;
  }
}

SSLCertLookup::~SSLCertLookup()
{
  // XXX This is completely broken. You can't use ats_free to free
  // a SSL_CTX *, you have to use SSL_CTX_free(). It doesn't matter
  // right now because sslCertLookup is a singleton and never destroyed.
  ink_hash_table_destroy_and_xfree_values(SSLCertLookupHashTable);
}

struct ats_x509_certificate
{
  explicit ats_x509_certificate(X509 * x) : x509(x) {}
  ~ats_x509_certificate() { if (x509) X509_free(x509); }

  X509 * x509;

private:
  ats_x509_certificate(const ats_x509_certificate&);
  ats_x509_certificate& operator=(const ats_x509_certificate&);
};

struct ats_file_bio
{
    ats_file_bio(const char * path, const char * mode)
      : bio(BIO_new_file(path, mode)) {
    }

    ~ats_file_bio() {
        (void)BIO_set_close(bio, BIO_CLOSE);
        BIO_free(bio);
    }

    operator bool() const {
        return bio != NULL;
    }

    BIO * bio;

private:
    ats_file_bio(const ats_file_bio&);
    ats_file_bio& operator=(const ats_file_bio&);
};

static char *
asn1_strdup(ASN1_STRING * s)
{
    // Make sure we have an 8-bit encoding.
    ink_assert(ASN1_STRING_type(s) == V_ASN1_IA5STRING ||
      ASN1_STRING_type(s) == V_ASN1_UTF8STRING ||
      ASN1_STRING_type(s) == V_ASN1_PRINTABLESTRING);

    return ats_strndup((const char *)ASN1_STRING_data(s), ASN1_STRING_length(s));
}

// Given a certificate and it's corresponding SSL_CTX context, insert hash
// table aliases for all of the subject and subjectAltNames. Note that we don't
// deal with wildcards (yet).
static void
insert_ssl_certificate(InkHashTable * htable, SSL_CTX * ctx, const char * certfile)
{
  GENERAL_NAMES * names = NULL;
  X509_NAME * subject = NULL;

  ats_file_bio bio(certfile, "r");
  ats_x509_certificate certificate(PEM_read_bio_X509_AUX(bio.bio, NULL, NULL, NULL));

  // Insert a key for the subject CN.
  subject = X509_get_subject_name(certificate.x509);
  if (subject) {
    int pos = -1;
    for (;;) {
      pos = X509_NAME_get_index_by_NID(subject, NID_commonName, pos);
      if (pos == -1) {
        break;
      }

      X509_NAME_ENTRY * e = X509_NAME_get_entry(subject, pos);
      ASN1_STRING * cn = X509_NAME_ENTRY_get_data(e);
      char * name = asn1_strdup(cn);

      Debug("ssl", "mapping '%s' to certificate %s", name, certfile);
      ink_hash_table_insert(htable, name, (void *)ctx);
      ats_free(name);
    }
  }

  // Traverse the subjectAltNames (if any) and insert additional keys for the SSL context.
  names = (GENERAL_NAMES *)X509_get_ext_d2i(certificate.x509, NID_subject_alt_name, NULL, NULL);
  if (names) {
    unsigned count = sk_GENERAL_NAME_num(names);
    for (unsigned i = 0; i < count; ++i) {
      GENERAL_NAME * name;
      char * dns;

      name = sk_GENERAL_NAME_value(names, i);
      switch (name->type) {
      case GEN_DNS:
        dns = asn1_strdup(name->d.dNSName);
        Debug("ssl", "mapping '%s' to certificate %s", dns, certfile);
        ink_hash_table_insert(htable, dns, (void *)ctx);
        ats_free(dns);
        break;
      }
    }

    GENERAL_NAMES_free(names);
  }
}
