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
#include "P_SSLUtils.h"
#include "P_SSLConfig.h"
#include "I_EventSystem.h"
#include "I_Layout.h"
#include "Regex.h"
#include "Trie.h"
#include "ts/TestBox.h"

struct SSLAddressLookupKey
{
  explicit
  SSLAddressLookupKey(const IpEndpoint& ip) : sep(0)
  {
    static const char hextab[16] = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };

    int nbytes;
    uint16_t port = ntohs(ip.port());

    // For IP addresses, the cache key is the hex address with the port concatenated. This makes the lookup
    // insensitive to address formatting and also allow the longest match semantic to produce different matches
    // if there is a certificate on the port.
    nbytes = ats_ip_to_hex(&ip.sa, key, sizeof(key));
    if (port) {
      sep = nbytes;
      key[nbytes++] = '.';
      key[nbytes++] = hextab[ (port >> 12) & 0x000F ];
      key[nbytes++] = hextab[ (port >>  8) & 0x000F ];
      key[nbytes++] = hextab[ (port >>  4) & 0x000F ];
      key[nbytes++] = hextab[ (port      ) & 0x000F ];
    }
    key[nbytes++] = 0;
  }

  const char * get() const { return key; }
  void split() { key[sep] = '\0'; }
  void unsplit() { key[sep] = '.'; }

private:
  char key[(TS_IP6_SIZE * 2) /* hex addr */ + 1 /* dot */ + 4 /* port */ + 1 /* NULL */];
  unsigned char sep; // offset of address/port separator
};

struct SSLContextStorage
{
  SSLContextStorage();
  ~SSLContextStorage();

  bool insert(SSL_CTX * ctx, const char * name);
  SSL_CTX * lookup(const char * name) const;

private:
  struct SSLEntry
  {
    explicit SSLEntry(SSL_CTX * c) : ctx(c) {}

    void Print() const { Debug("ssl", "SSLEntry=%p SSL_CTX=%p", this, ctx); }

    SSL_CTX * ctx;
    LINK(SSLEntry, link);
  };

  Trie<SSLEntry>  wildcards;
  InkHashTable *  hostnames;
  Vec<SSL_CTX *>  references;
};

SSLCertLookup::SSLCertLookup()
  : ssl_storage(NEW(new SSLContextStorage())), ssl_default(NULL)
{
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

SSL_CTX *
SSLCertLookup::findInfoInHash(const IpEndpoint& address) const
{
  SSL_CTX * ctx;
  SSLAddressLookupKey key(address);

  // First try the full address.
  if ((ctx = this->ssl_storage->lookup(key.get()))) {
    return ctx;
  }

  // If that failed, try the address without the port.
  if (address.port()) {
    key.split();
    return this->ssl_storage->lookup(key.get());
  }

  return NULL;
}

bool
SSLCertLookup::insert(SSL_CTX * ctx, const char * name)
{
  return this->ssl_storage->insert(ctx, name);
}

bool
SSLCertLookup::insert(SSL_CTX * ctx, const IpEndpoint& address)
{
  SSLAddressLookupKey key(address);
  return this->ssl_storage->insert(ctx, key.get());
}

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
  for (unsigned i = 0; i < this->references.count(); ++i) {
    SSLReleaseContext(this->references[i]);
  }

  ink_hash_table_destroy(this->hostnames);
}

bool
SSLContextStorage::insert(SSL_CTX * ctx, const char * name)
{
  ats_wildcard_matcher wildcard;
  bool inserted = false;

  if (wildcard.match(name)) {
    // We turn wildcards into the reverse DNS form, then insert them into the trie
    // so that we can do a longest match lookup.
    char namebuf[TS_MAX_HOST_NAME_LEN + 1];
    char * reversed;
    xptr<SSLEntry> entry;

    reversed = reverse_dns_name(name + 2, namebuf);
    if (!reversed) {
      Error("wildcard name '%s' is too long", name);
      return false;
    }

    entry = new SSLEntry(ctx);
    inserted = this->wildcards.Insert(reversed, entry, 0 /* rank */, -1 /* keylen */);
    if (!inserted) {
      SSLEntry * found;

      // We fail to insert, so the longest wildcard match search should return the full match value.
      found = this->wildcards.Search(reversed);
      if (found != NULL && found->ctx != ctx) {
        Warning("previously indexed wildcard certificate for '%s' as '%s', cannot index it with SSL_CTX %p now",
            name, reversed, ctx);
      }

      goto done;
    }

    Debug("ssl", "indexed wildcard certificate for '%s' as '%s' with SSL_CTX %p", name, reversed, ctx);
    entry.release();
  } else {
    InkHashTableValue value;

    if (ink_hash_table_lookup(this->hostnames, name, &value) && (void *)ctx != value) {
      Warning("previously indexed '%s' with SSL_CTX %p, cannot index it with SSL_CTX %p now", name, value, ctx);
      goto done;
    }

    inserted = true;
    ink_hash_table_insert(this->hostnames, name, (void *)ctx);
    Debug("ssl", "indexed '%s' with SSL_CTX %p", name, ctx);
  }

done:
  // Keep a unique reference to the SSL_CTX, so that we can free it later. Since we index by name, multiple
  // certificates can be indexed for the same name. If this happens, we will overwrite the previous pointer
  // and leak a context. So if we insert a certificate, keep an ownership reference to it.
  if (inserted) {
    if (this->references.in(ctx) == NULL) {
      this->references.push_back(ctx);
    }
  }

  return inserted;
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
    SSLEntry * entry;

    reversed = reverse_dns_name(name, namebuf);
    if (!reversed) {
      Error("failed to reverse hostname name '%s' is too long", name);
      return NULL;
    }

    Debug("ssl", "attempting wildcard match for %s", reversed);
    entry = this->wildcards.Search(reversed);
    if (entry) {
      return entry->ctx;
    }
  }

  return NULL;
}

#if TS_HAS_TESTS

REGRESSION_TEST(SSLWildcardMatch)(RegressionTest * t, int /* atype ATS_UNUSED */, int * pstatus)
{
  TestBox box(t, pstatus);
  ats_wildcard_matcher wildcard;

  box = REGRESSION_TEST_PASSED;

  box.check(wildcard.match("foo.com") == false, "foo.com is not a wildcard");
  box.check(wildcard.match("*.foo.com") == true, "*.foo.com not a wildcard");
  box.check(wildcard.match("bar*.foo.com") == false, "bar*.foo.com not a wildcard");
  box.check(wildcard.match("*") == false, "* is not a wildcard");
  box.check(wildcard.match("") == false, "'' is not a wildcard");
}

REGRESSION_TEST(SSLReverseHostname)(RegressionTest * t, int /* atype ATS_UNUSED */, int * pstatus)
{
  TestBox box(t, pstatus);

  char reversed[TS_MAX_HOST_NAME_LEN + 1];

#define _R(name) reverse_dns_name(name, reversed)

  box = REGRESSION_TEST_PASSED;

  box.check(strcmp(_R("foo.com"), "com.foo") == 0, "reversed foo.com");
  box.check(strcmp(_R("bar.foo.com"), "com.foo.bar") == 0, "reversed bar.foo.com");
  box.check(strcmp(_R("foo"), "foo") == 0, "reversed foo");

#undef _R
}

#endif // TS_HAS_TESTS
