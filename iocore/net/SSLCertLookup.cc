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
#include "ts/MatcherUtils.h"
#include "ts/Regex.h"
#include "ts/Trie.h"
#include "ts/TestBox.h"

// Check if the ticket_key callback #define is available, and if so, enable session tickets.
#ifdef SSL_CTX_set_tlsext_ticket_key_cb

#define HAVE_OPENSSL_SESSION_TICKETS 1

#endif /* SSL_CTX_set_tlsext_ticket_key_cb */

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
  char key[(TS_IP6_SIZE * 2) /* hex addr */ + 1 /* dot */ + 4 /* port */ + 1 /* nullptr */];
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
  void printWildDomains() const;
  void freeWildDomains() const;
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

  /// We can only match one layer with the wildcards
  /// This table stores the wildcarded subdomain
  mutable HashMap<cchar *, StringHashFns, int> wilddomains;
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
ssl_ticket_key_block *
ticket_block_create(char *ticket_key_data, int ticket_key_len)
{
  ssl_ticket_key_block *keyblock = nullptr;
  unsigned num_ticket_keys       = ticket_key_len / sizeof(ssl_ticket_key_t);
  if (num_ticket_keys == 0) {
    Error("SSL session ticket key is too short (>= 48 bytes are required)");
    goto fail;
  }
  Debug("ssl", "Create %d ticket key blocks", num_ticket_keys);

  keyblock = ticket_block_alloc(num_ticket_keys);

  // Slurp all the keys in the ticket key file. We will encrypt with the first key, and decrypt
  // with any key (for rotation purposes).
  for (unsigned i = 0; i < num_ticket_keys; ++i) {
    const char *data = (const char *)ticket_key_data + (i * sizeof(ssl_ticket_key_t));

    memcpy(keyblock->keys[i].key_name, data, sizeof(keyblock->keys[i].key_name));
    memcpy(keyblock->keys[i].hmac_secret, data + sizeof(keyblock->keys[i].key_name), sizeof(keyblock->keys[i].hmac_secret));
    memcpy(keyblock->keys[i].aes_key, data + sizeof(keyblock->keys[i].key_name) + sizeof(keyblock->keys[i].hmac_secret),
           sizeof(keyblock->keys[i].aes_key));
  }

  return keyblock;

fail:
  ticket_block_free(keyblock);
  return nullptr;
}

ssl_ticket_key_block *
ssl_create_ticket_keyblock(const char *ticket_key_path)
{
#if HAVE_OPENSSL_SESSION_TICKETS
  ats_scoped_str ticket_key_data;
  int ticket_key_len;
  ssl_ticket_key_block *keyblock = nullptr;

  if (ticket_key_path != nullptr) {
    ticket_key_data = readIntoBuffer(ticket_key_path, __func__, &ticket_key_len);
    if (!ticket_key_data) {
      Error("failed to read SSL session ticket key from %s", (const char *)ticket_key_path);
      goto fail;
    }
    keyblock = ticket_block_create(ticket_key_data, ticket_key_len);
  } else {
    // Generate a random ticket key
    ssl_ticket_key_t key;
    RAND_bytes(reinterpret_cast<unsigned char *>(&key), sizeof(key));
    keyblock = ticket_block_create(reinterpret_cast<char *>(&key), sizeof(key));
  }

  return keyblock;

fail:
  ticket_block_free(keyblock);
  return nullptr;

#else  /* !HAVE_OPENSSL_SESSION_TICKETS */
  (void)ticket_key_path;
  return nullptr;
#endif /* HAVE_OPENSSL_SESSION_TICKETS */
}
void
SSLCertContext::release()
{
  if (keyblock) {
    ticket_block_free(keyblock);
    keyblock = nullptr;
  }

  SSLReleaseContext(ctx);
  ctx = nullptr;
}

SSLCertLookup::SSLCertLookup() : ssl_storage(new SSLContextStorage()), ssl_default(nullptr), is_valid(true) {}

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

  return nullptr;
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

static void
make_to_lower_case(const char *name, char *lower_case_name, int buf_len)
{
  int name_len = strlen(name);
  int i;
  if (name_len > (buf_len - 1)) {
    name_len = buf_len - 1;
  }
  for (i = 0; i < name_len; i++) {
    lower_case_name[i] = ParseRules::ink_tolower(name[i]);
  }
  lower_case_name[i] = '\0';
}

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
      return nullptr;
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
  make_to_lower_case(ptr, ptr, strlen(ptr) + 1);

  return ptr;
}

SSLContextStorage::SSLContextStorage() : wilddomains(-1), hostnames(ink_hash_table_create(InkHashTableKeyType_String)) {}

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
  SSL_CTX *last_ctx = nullptr;
  for (unsigned i = 0; i < this->ctx_store.length(); ++i) {
    if (this->ctx_store[i].ctx != last_ctx) {
      last_ctx = this->ctx_store[i].ctx;
      this->ctx_store[i].release();
    }
  }

  ink_hash_table_destroy(this->hostnames);
  freeWildDomains();
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
  if (idx < 0) {
    this->ctx_store.drop();
  }
  return idx;
}

int
SSLContextStorage::insert(const char *name, int idx)
{
  ats_wildcard_matcher wildcard;
  InkHashTableValue value;
  char lower_case_name[TS_MAX_HOST_NAME_LEN + 1];
  make_to_lower_case(name, lower_case_name, sizeof(lower_case_name));
  if (wildcard.match(lower_case_name)) {
    // Strip the wildcard and store the subdomain
    const char *subdomain = index(lower_case_name, '*');
    if (subdomain && subdomain[1] == '.') {
      subdomain += 2; // Move beyond the '.'
    } else {
      subdomain = nullptr;
    }
    if (subdomain) {
      auto index = this->wilddomains.get(subdomain);
      if (index != -1) {
        Warning("previously indexed '%s' with SSL_CTX #%d, cannot index it with SSL_CTX #%d now", lower_case_name, index, idx);
        idx = -1;
      } else {
        this->wilddomains.put(ats_strdup(subdomain), idx);
        Debug("ssl", "indexed '%s' with SSL_CTX %p [%d]", lower_case_name, this->ctx_store[idx].ctx, idx);
      }
    }
  } else {
    if (ink_hash_table_lookup(this->hostnames, lower_case_name, &value) && reinterpret_cast<InkHashTableValue>(idx) != value) {
      Warning("previously indexed '%s' with SSL_CTX %p, cannot index it with SSL_CTX #%d now", lower_case_name, value, idx);
      idx = -1;
    } else {
      ink_hash_table_insert(this->hostnames, lower_case_name, reinterpret_cast<void *>(static_cast<intptr_t>(idx)));
      Debug("ssl", "indexed '%s' with SSL_CTX %p [%d]", lower_case_name, this->ctx_store[idx].ctx, idx);
    }
  }
  return idx;
}

void
SSLContextStorage::printWildDomains() const
{
  Vec<cchar *> keys;
  this->wilddomains.get_keys(keys);
  for (size_t i = 0; i < keys.length(); i++) {
    Debug("ssl", "Stored wilddomain %s", keys.get(i));
  }
}

void
SSLContextStorage::freeWildDomains() const
{
  Vec<cchar *> keys;
  this->wilddomains.get_keys(keys);
  size_t n = keys.length();
  for (size_t i = 0; i < n; i++) {
    ats_free((char *)keys.get(i));
  }
  this->wilddomains.clear();
}

SSLCertContext *
SSLContextStorage::lookup(const char *name) const
{
  InkHashTableValue value;
  // First look for an exact name match
  if (ink_hash_table_lookup(const_cast<InkHashTable *>(this->hostnames), name, &value)) {
    return &(this->ctx_store[reinterpret_cast<intptr_t>(value)]);
  }
  // Try lower casing it
  char lower_case_name[TS_MAX_HOST_NAME_LEN + 1];
  make_to_lower_case(name, lower_case_name, sizeof(lower_case_name));
  if (ink_hash_table_lookup(const_cast<InkHashTable *>(this->hostnames), lower_case_name, &value)) {
    return &(this->ctx_store[reinterpret_cast<intptr_t>(value)]);
  }

  // Then strip off the top domain name and look for a wildcard domain match
  const char *subdomain = index(lower_case_name, '.');
  if (subdomain) {
    ++subdomain; // Move beyond the '.'
    auto index = this->wilddomains.get(subdomain);
    if (index >= 0) {
      return &(this->ctx_store[index]);
    }
  }
  return nullptr;
}

#if TS_HAS_TESTS

REGRESSION_TEST(SSLWildcardMatch)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  ats_wildcard_matcher wildcard;

  box = REGRESSION_TEST_PASSED;

  box.check(wildcard.match("foo.com") == false, "foo.com is not a wildcard");
  box.check(wildcard.match("*.foo.com") == true, "*.foo.com is a wildcard");
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
  box.check(strcmp(_R("foo.Com"), "Com.foo") != 0, "mixed case reversed foo.com mismatch");
  box.check(strcmp(_R("foo.Com"), "com.foo") == 0, "mixed case reversed foo.com match");

#undef _R
}

#endif // TS_HAS_TESTS
