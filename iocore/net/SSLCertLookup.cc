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
#include "Regex.h"
#include "Trie.h"
#include "ts/TestBox.h"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>

#if HAVE_OPENSSL_TS_H
#include <openssl/ts.h>
#endif

#if (OPENSSL_VERSION_NUMBER >= 0x10000000L) // openssl returns a const SSL_METHOD
typedef const SSL_METHOD * ink_ssl_method_t;
#else
typedef SSL_METHOD * ink_ssl_method_t;
#endif

class SSLContextStorage
{

  struct SslEntry
  {
    explicit SslEntry(SSL_CTX * c) : ctx(c) {}

    void Print() const { printf("%p/%p", this, ctx); }

    SSL_CTX * ctx;
    LINK(SslEntry, link);
  };

  Trie<SslEntry>  wildcards;
  InkHashTable *  hostnames;

public:
  SSLContextStorage();
  ~SSLContextStorage();

  bool insert(SSL_CTX * ctx, const char * name);
  SSL_CTX * lookup(const char * name) const;

  // XXX although we take "ownership" of the SSL_CTX, we never actually free them when we are
  // done. Currently, this doesn't matter because altering the set of SSL certificates requires
  // a restart. When this changes, we will need to keep track of the SSL_CTX pointers exactly
  // once and call SSL_CTX_free() on them.

};

SSLCertLookup sslCertLookup;

static void
insert_ssl_certificate(SSLContextStorage *, SSL_CTX *, const char *);

#define SSL_IP_TAG            "dest_ip"
#define SSL_CERT_TAG          "ssl_cert_name"
#define SSL_PRIVATE_KEY_TAG   "ssl_key_name"
#define SSL_CA_TAG            "ssl_ca_name"

static const char *moduleName = "SSLCertLookup";

static const matcher_tags sslCertTags = {
  NULL, NULL, NULL, NULL, NULL, false
};

SSLCertLookup::SSLCertLookup()
  : param(NULL), multipleCerts(false), ssl_storage(NEW(new SSLContextStorage())), ssl_default(NULL)
{
  *config_file_path = '\0';
}

SSLCertLookup::~SSLCertLookup()
{
  delete this->ssl_storage;
}

SSL_CTX *
SSLCertLookup::findInfoInHash(const char * address) const
{
  return this->ssl_storage->lookup(address);
}

void
SSLCertLookup::init(SslConfigParams * p)
{
  param = p;
  multipleCerts = buildTable();

  // If there wasn't a default SSL context, make a default one. We need this to bootstrap
  // the SNI process and also to avoid crashing (which is generaly frowned upon).
  if (!this->ssl_default) {
    // XXX this leaks, but we're a singleton, so ....
    this->ssl_default = SSL_CTX_new(SSLv23_server_method());
  }
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
        errPtr = extractIPAndCert(&line_info, &addr, &sslCert, &sslCa, &priKey);

        if (errPtr != NULL) {
          snprintf(errBuf, 1024, "%s discarding %s entry at line %d : %s",
                       moduleName, configFilePath, line_num, errPtr);
          IOCORE_SignalError(errBuf, alarmAlready);
        } else {
          if (sslCert != NULL) {
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
    const char *caCert, const char *serverPrivateKey)
{
  ink_ssl_method_t meth = NULL;

  meth = SSLv23_server_method();
  SSL_CTX *ctx = SSL_CTX_new(meth);
  if (!ctx) {
    SSLNetProcessor::logSSLError("Cannot create new server contex.");
    return (false);
  }

  if (ssl_NetProcessor.initSSLServerCTX(ctx, this->param, cert, caCert, serverPrivateKey) == 0) {
    char * certpath = Layout::relative_to(this->param->getServerCertPathOnly(), cert);

    // Index this certificate by the specified IP(v6) address. If the address is "*", make it the default context.
    if (strAddr) {
      if (strcmp(strAddr, "*") == 0) {
        this->ssl_default = ctx;
      } else {
        this->ssl_storage->insert(ctx, strAddr);
      }
    }

    // Insert additional mappings. Note that this maps multiple keys to the same value, so when
    // this code is updated to reconfigure the SSL certificates, it will need some sort of
    // refcounting or alternate way of avoiding double frees.
    insert_ssl_certificate(this->ssl_storage, ctx, certpath);

    ats_free(certpath);
    return (true);
  }

  SSL_CTX_free(ctx);
  return (false);
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

struct ats_wildcard_matcher
{
  ats_wildcard_matcher() {
    if (regex.compile("^\\*\\.[^\\*.]+") != 0) {
      Fatal("failed to compile TLS wildcard matching regex");
    }
  }

  ~ats_wildcard_matcher() {
  }

  bool match(const char * hostname) const {
    return regex.match(hostname) != -1;
  }

private:
  DFA regex;
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
insert_ssl_certificate(SSLContextStorage * storage, SSL_CTX * ctx, const char * certfile)
{
  X509_NAME * subject = NULL;
  ats_wildcard_matcher wildcard;

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
      storage->insert(ctx, name);
      ats_free(name);
    }
  }

#if HAVE_OPENSSL_TS_H
  // Traverse the subjectAltNames (if any) and insert additional keys for the SSL context.
  GENERAL_NAMES * names = (GENERAL_NAMES *)X509_get_ext_d2i(certificate.x509, NID_subject_alt_name, NULL, NULL);
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
        storage->insert(ctx, dns);
        ats_free(dns);
        break;
      }
    }

    GENERAL_NAMES_free(names);
  }
#endif // HAVE_OPENSSL_TS_H

}

static char *
reverse_dns_name(const char * hostname, char (&reversed)[TS_MAX_HOST_NAME_LEN+1])
{
  char * ptr = reversed + sizeof(reversed);
  const char * part = hostname;

  *(--ptr) = '\0'; // NUL-terminate

  while (*part) {
    ssize_t len = strcspn(part, ".");
    ssize_t remain = ptr - reversed;

    // We are going to put the '.' separator back for all components except the first.
    if (*ptr == '\0') {
      if (remain < len) {
        return NULL;
      }
    } else {
      if (remain < (len + 1)) {
        return NULL;
      }
      *(--ptr) = '.';
    }

    ptr -= len;
    memcpy(ptr, part, len);

    // Skip to the next domain component. This will take us to either a '.' or a NUL.
    // If it's a '.' we need to skip over it.
    part += len;
    if (*part == '.') {
      ++part;
    }
  }

  return ptr;
}

SSLContextStorage::SSLContextStorage()
  :wildcards(), hostnames(ink_hash_table_create(InkHashTableKeyType_String))
{
}

SSLContextStorage::~SSLContextStorage()
{
  ink_hash_table_destroy(this->hostnames);
}

bool
SSLContextStorage::insert(SSL_CTX * ctx, const char * name)
{
  ats_wildcard_matcher wildcard;

  if (wildcard.match(name)) {
    // We turn wildcards into the reverse DNS form, then insert them into the trie
    // so that we can do a longest match lookup.
    char namebuf[TS_MAX_HOST_NAME_LEN + 1];
    char * reversed;

    reversed = reverse_dns_name(name + 2, namebuf);
    if (!reversed) {
      Error("wildcard name '%s' is too long", name);
      return false;
    }

    Debug("indexed wildcard certificate for '%s' as '%s'", name, reversed);
    this->wildcards.Insert(reversed, new SslEntry(ctx), 0 /* rank */, -1 /* keylen */);
  } else {
    ink_hash_table_insert(this->hostnames, name, (void *)ctx);
  }

  return true;
}

SSL_CTX *
SSLContextStorage::lookup(const char * name) const
{
  InkHashTableValue value;

  if (ink_hash_table_lookup(const_cast<InkHashTable *>(this->hostnames), name, &value)) {
    return (SSL_CTX *)value;
  }

  if (!this->wildcards.Empty()) {
    char namebuf[TS_MAX_HOST_NAME_LEN + 1];
    char * reversed;
    SslEntry * entry;

    reversed = reverse_dns_name(name, namebuf);
    if (!reversed) {
      Error("failed to reverse hostname name '%s' is too long", name);
      return NULL;
    }

    entry = this->wildcards.Search(reversed);
    if (entry) {
      return entry->ctx;
    }
  }

  return NULL;
}

#if TS_HAS_TESTS

REGRESSION_TEST(SslHostLookup)(RegressionTest* t, int atype, int * pstatus)
{
  TestBox           tb(t, pstatus);
  SSLContextStorage storage;
  ink_ssl_method_t  methods = SSLv23_server_method();

  SSL_CTX * wild = SSL_CTX_new(methods);
  SSL_CTX * notwild = SSL_CTX_new(methods);
  SSL_CTX * foo = SSL_CTX_new(methods);

  *pstatus = REGRESSION_TEST_PASSED;

  tb.check(storage.insert(foo, "www.foo.com"), "insert host context");
  tb.check(storage.insert(wild, "*.wild.com"), "insert wildcard context");
  tb.check(storage.insert(notwild, "*.notwild.com"), "insert wildcard context");

  // Basic wildcard cases.
  tb.check(storage.lookup("a.wild.com") == wild, "wildcard lookup for a.wild.com");
  tb.check(storage.lookup("b.wild.com") == wild, "wildcard lookup for b.wild.com");
  tb.check(storage.lookup("wild.com") == wild, "wildcard lookup for wild.com");

  // Varify that wildcard does longest match.
  tb.check(storage.lookup("a.notwild.com") == notwild, "wildcard lookup for a.notwild.com");
  tb.check(storage.lookup("notwild.com") == notwild, "wildcard lookup for notwild.com");

  // Basic hostname cases.
  tb.check(storage.lookup("www.foo.com") == foo, "host lookup for www.foo.com");
  tb.check(storage.lookup("www.bar.com") == NULL, "host lookup for www.bar.com");

  // XXX Stop free'ing these once SSLContextStorage does it.
  SSL_CTX_free(wild);
  SSL_CTX_free(notwild);
  SSL_CTX_free(foo);
}

#endif // TS_HAS_TESTS
