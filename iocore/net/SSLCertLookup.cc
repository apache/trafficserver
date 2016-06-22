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

#include "ts/ink_config.h"

#include "P_SSLCertLookup.h"
#include "P_SSLUtils.h"
#include "P_SSLConfig.h"
#include "I_EventSystem.h"
#include "ts/I_Layout.h"
#include "ts/Regex.h"
#include "ts/Trie.h"
#include "ts/TestBox.h"

struct SSLAddressLookupKey {
  explicit SSLAddressLookupKey(const IpEndpoint &ip) : sep(0)
  {
    static const char hextab[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    int nbytes;
    uint16_t port = ntohs(ip.port());

    // For IP addresses, the cache key is the hex address with the port concatenated. This makes the lookup
    // insensitive to address formatting and also allow the longest match semantic to produce different matches
    // if there is a certificate on the port.
    nbytes = ats_ip_to_hex(&ip.sa, key, sizeof(key));
    if (port) {
      sep           = nbytes;
      key[nbytes++] = '.';
      key[nbytes++] = hextab[(port >> 12) & 0x000F];
      key[nbytes++] = hextab[(port >> 8) & 0x000F];
      key[nbytes++] = hextab[(port >> 4) & 0x000F];
      key[nbytes++] = hextab[(port)&0x000F];
    }
    key[nbytes++] = 0;
  }

  const char *
  get() const
  {
    return key;
  }
  void
  split()
  {
    key[sep] = '\0';
  }
  void
  unsplit()
  {
    key[sep] = '.';
  }

private:
  char key[(TS_IP6_SIZE * 2) /* hex addr */ + 1 /* dot */ + 4 /* port */ + 1 /* NULL */];
  unsigned char sep; // offset of address/port separator
};

struct SSLContextStorage {
public:
  SSLContextStorage();
  ~SSLContextStorage();

  /// Add a cert context to storage
  /// @return The @a host_store index or -1 on error.
  int insert(const char *name, SSLCertContext const &cc);

  /// Add a cert context to storage.
  /// @a idx must be a value returned by a previous call to insert.
  /// This creates an alias, a different @a name referring to the same
  /// cert context.
  /// @return @a idx
  int insert(const char *name, int idx);
  SSLCertContext *lookup(const char *name) const;
  unsigned
  count() const
  {
    return this->ctx_store.length();
  }
  SSLCertContext *
  get(unsigned i) const
  {
    return &this->ctx_store[i];
  }

private:
  /** A struct that can be stored a @c Trie.
      It contains the index of the real certificate and the
      linkage required by @c Trie.
  */
  struct ContextRef {
    ContextRef() : idx(-1) {}
    explicit ContextRef(int n) : idx(n) {}
    void
    Print() const
    {
      Debug("ssl", "Item=%p SSL_CTX=#%d", this, idx);
    }
    int idx;                ///< Index in the context store.
    LINK(ContextRef, link); ///< Require by @c Trie
  };

  /// Items tored by wildcard name
  Trie<ContextRef> wildcards;
  /// Contexts stored by IP address or FQDN
  InkHashTable *hostnames;
  /// List for cleanup.
  /// Exactly one pointer to each SSL context is stored here.
  Vec<SSLCertContext> ctx_store;

  /// Add a context to the clean up list.
  /// @return The index of the added context.
  int store(SSLCertContext const &cc);
};

// Zero out and free the heap space allocated for ticket keys to avoid leaking secrets.
// The first several bytes stores the number of keys and the rest stores the ticket keys.
void
ticket_block_free(void *ptr)
{
  if (ptr) {
    ssl_ticket_key_block *key_block_ptr = (ssl_ticket_key_block *)ptr;
    unsigned num_ticket_keys            = key_block_ptr->num_keys;
    memset(ptr, 0, sizeof(ssl_ticket_key_block) + num_ticket_keys * sizeof(ssl_ticket_key_t));
  }
  ats_free(ptr);
}

ssl_ticket_key_block *
ticket_block_alloc(unsigned count)
{
  ssl_ticket_key_block *ptr;
  size_t nbytes = sizeof(ssl_ticket_key_block) + count * sizeof(ssl_ticket_key_t);

  ptr = (ssl_ticket_key_block *)ats_malloc(nbytes);
  memset(ptr, 0, nbytes);
  ptr->num_keys = count;

  return ptr;
}

void
SSLCertContext::release()
{
  if (keyblock) {
    ticket_block_free(keyblock);
    keyblock = NULL;
  }
  if (ctx) {
    SSL_CTX_free(ctx);
    ctx = NULL;
  }
}

SSLCertLookup::SSLCertLookup() : ssl_storage(new SSLContextStorage()), ssl_default(NULL), is_valid(true)
{
}

SSLCertLookup::~SSLCertLookup()
{
  delete this->ssl_storage;
}

SSLCertContext *
SSLCertLookup::find(const char *address) const
{
  return this->ssl_storage->lookup(address);
}

SSLCertContext *
SSLCertLookup::find(const IpEndpoint &address) const
{
  SSLCertContext *cc;
  SSLAddressLookupKey key(address);

  // First try the full address.
  if ((cc = this->ssl_storage->lookup(key.get()))) {
    return cc;
  }

  // If that failed, try the address without the port.
  if (address.port()) {
    key.split();
    return this->ssl_storage->lookup(key.get());
  }

  return NULL;
}

int
SSLCertLookup::insert(const char *name, SSLCertContext const &cc)
{
  return this->ssl_storage->insert(name, cc);
}

int
SSLCertLookup::insert(const IpEndpoint &address, SSLCertContext const &cc)
{
  SSLAddressLookupKey key(address);
  return this->ssl_storage->insert(key.get(), cc);
}

unsigned
SSLCertLookup::count() const
{
  return ssl_storage->count();
}

SSLCertContext *
SSLCertLookup::get(unsigned i) const
{
  return ssl_storage->get(i);
}

struct ats_wildcard_matcher {
  ats_wildcard_matcher()
  {
    if (regex.compile("^\\*\\.[^\\*.]+") != 0) {
      Fatal("failed to compile TLS wildcard matching regex");
    }
  }

  ~ats_wildcard_matcher() {}
  bool
  match(const char *hostname) const
  {
    return regex.match(hostname) != -1;
  }

private:
  DFA regex;
};

static char *
reverse_dns_name(const char *hostname, char (&reversed)[TS_MAX_HOST_NAME_LEN + 1])
{
  char *ptr        = reversed + sizeof(reversed);
  const char *part = hostname;

  *(--ptr) = '\0'; // NUL-terminate

  while (*part) {
    ssize_t len    = strcspn(part, ".");
    ssize_t remain = ptr - reversed;

    if (remain < (len + 1)) {
      return NULL;
    }

    ptr -= len;
    memcpy(ptr, part, len);

    // Skip to the next domain component. This will take us to either a '.' or a NUL.
    // If it's a '.' we need to skip over it.
    part += len;
    if (*part == '.') {
      ++part;
      *(--ptr) = '.';
    }
  }

  return ptr;
}

SSLContextStorage::SSLContextStorage() : wildcards(), hostnames(ink_hash_table_create(InkHashTableKeyType_String))
{
}

bool
SSLCtxCompare(SSLCertContext const &cc1, SSLCertContext const &cc2)
{
  // Either they are both real ctx pointers and cc1 has the smaller pointer
  // Or only cc2 has a non-null pointer
  return cc1.ctx < cc2.ctx;
}

SSLContextStorage::~SSLContextStorage()
{
  // First sort the array so we can efficiently detect duplicates
  // and avoid the double free
  this->ctx_store.qsort(SSLCtxCompare);
  SSL_CTX *last_ctx = NULL;
  for (unsigned i = 0; i < this->ctx_store.length(); ++i) {
    if (this->ctx_store[i].ctx != last_ctx) {
      last_ctx = this->ctx_store[i].ctx;
      this->ctx_store[i].release();
    }
  }

  ink_hash_table_destroy(this->hostnames);
}

int
SSLContextStorage::store(SSLCertContext const &cc)
{
  int idx = this->ctx_store.length();
  this->ctx_store.add(cc);
  return idx;
}

int
SSLContextStorage::insert(const char *name, SSLCertContext const &cc)
{
  int idx = this->store(cc);
  idx     = this->insert(name, idx);
  if (idx < 0)
    this->ctx_store.drop();
  return idx;
}

int
SSLContextStorage::insert(const char *name, int idx)
{
  ats_wildcard_matcher wildcard;
  bool inserted = false;

  if (wildcard.match(name)) {
    // We turn wildcards into the reverse DNS form, then insert them into the trie
    // so that we can do a longest match lookup.
    char namebuf[TS_MAX_HOST_NAME_LEN + 1];
    char *reversed;
    ats_scoped_obj<ContextRef> ref;

    reversed = reverse_dns_name(name + 1, namebuf);
    if (!reversed) {
      Error("wildcard name '%s' is too long", name);
      return -1;
    }

    ref         = new ContextRef(idx);
    int ref_idx = (*ref).idx;
    inserted    = this->wildcards.Insert(reversed, ref, 0 /* rank */, -1 /* keylen */);
    if (!inserted) {
      ContextRef *found;

      // We fail to insert, so the longest wildcard match search should return the full match value.
      found = this->wildcards.Search(reversed);
      // Fail even if we are reinserting the exact same value
      // Otherwise we cannot detect and recover from a double insert
      // into the references array
      if (found != NULL) {
        Warning("previously indexed wildcard certificate for '%s' as '%s', cannot index it with SSL_CTX #%d now", name, reversed,
                idx);
      }
      idx = -1;
    } else {
      ref.release(); // it's the hands of the Trie now, forget it and move on.
    }

    Debug("ssl", "%s wildcard certificate for '%s' as '%s' with SSL_CTX %p [%d]", idx >= 0 ? "index" : "failed to index", name,
          reversed, this->ctx_store[ref_idx].ctx, ref_idx);
  } else {
    InkHashTableValue value;

    if (ink_hash_table_lookup(this->hostnames, name, &value) && reinterpret_cast<InkHashTableValue>(idx) != value) {
      Warning("previously indexed '%s' with SSL_CTX %p, cannot index it with SSL_CTX #%d now", name, value, idx);
      idx = -1;
    } else {
      ink_hash_table_insert(this->hostnames, name, reinterpret_cast<void *>(static_cast<intptr_t>(idx)));
      Debug("ssl", "indexed '%s' with SSL_CTX %p [%d]", name, this->ctx_store[idx].ctx, idx);
    }
  }
  return idx;
}

SSLCertContext *
SSLContextStorage::lookup(const char *name) const
{
  InkHashTableValue value;

  if (ink_hash_table_lookup(const_cast<InkHashTable *>(this->hostnames), name, &value)) {
    return &(this->ctx_store[reinterpret_cast<intptr_t>(value)]);
  }

  if (!this->wildcards.Empty()) {
    char namebuf[TS_MAX_HOST_NAME_LEN + 1];
    char *reversed;
    ContextRef *ref;

    reversed = reverse_dns_name(name, namebuf);
    if (!reversed) {
      Error("failed to reverse hostname name '%s' is too long", name);
      return NULL;
    }

    Debug("ssl", "attempting wildcard match for %s", reversed);
    ref = this->wildcards.Search(reversed);
    if (ref) {
      return &(this->ctx_store[ref->idx]);
    }
  }

  return NULL;
}

#if TS_HAS_TESTS

REGRESSION_TEST(SSLWildcardMatch)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
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

REGRESSION_TEST(SSLReverseHostname)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
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
