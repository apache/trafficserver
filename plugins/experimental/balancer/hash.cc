/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "balancer.h"
#include <cstdlib>
#include <openssl/md5.h>
#include <netinet/in.h>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
size_t
sockaddrlen(const struct sockaddr *sa)
{
  switch (sa->sa_family) {
  case AF_INET:
    return sizeof(struct sockaddr_in);
  case AF_INET6:
    return sizeof(struct sockaddr_in6);
  default:
    TSReleaseAssert(0 && "unsupported socket type");
  }

  return 0;
}

struct md5_key {
  md5_key() {}
  md5_key(const BalancerTarget &target, unsigned i)
  {
    MD5_CTX ctx;

    MD5_Init(&ctx);
    MD5_Update(&ctx, target.name.data(), target.name.size());
    MD5_Update(&ctx, &target.port, sizeof(target.port));
    MD5_Update(&ctx, &i, sizeof(i));
    MD5_Final(this->key, &ctx);
  }

  bool
  operator<(const md5_key &rhs) const
  {
    return memcmp(this->key, rhs.key, sizeof(this->key)) < 0;
  }

  unsigned char key[MD5_DIGEST_LENGTH];
};

using HashComponent = void (*)(TSHttpTxn, TSRemapRequestInfo *, MD5_CTX *);

// Hash on the source (client) IP address.
void
HashTxnSrcaddr(TSHttpTxn txn, TSRemapRequestInfo *, MD5_CTX *ctx)
{
  struct sockaddr const *sa;

  sa = TSHttpTxnClientAddrGet(txn);
  if (sa) {
    MD5_Update(ctx, sa, sockaddrlen(sa));
    TSDebug("balancer", "%s(addr[%zu]]", __func__, sockaddrlen(sa));
  }
}

// Hash on the destination (server) IP address;
void
HashTxnDstaddr(TSHttpTxn txn, TSRemapRequestInfo *, MD5_CTX *ctx)
{
  struct sockaddr const *sa;

  sa = TSHttpTxnIncomingAddrGet(txn);
  if (sa) {
    MD5_Update(ctx, sa, sockaddrlen(sa));
    TSDebug("balancer", "%s(addr[%zu]]", __func__, sockaddrlen(sa));
  }
}

// Hash on the request URL.
void
HashTxnUrl(TSHttpTxn txn, TSRemapRequestInfo *, MD5_CTX *ctx)
{
  char *url;
  int len;

  url = TSHttpTxnEffectiveUrlStringGet(txn, &len);
  if (url && len) {
    MD5_Update(ctx, url, len);
    TSDebug("balancer", "%s(%.*s)", __func__, len, url);
  }

  TSfree(url);
}

// Hash on the cache key. This is not typically set at remap time, unless by another plugin.
void
HashTxnKey(TSHttpTxn txn, TSRemapRequestInfo *rri, MD5_CTX *ctx)
{
  TSMLoc url = TS_NULL_MLOC;
  char *str  = nullptr;
  int len;

  if (TSUrlCreate(rri->requestBufp, &url) != TS_SUCCESS) {
    goto done;
  }

  if (TSHttpTxnCacheLookupUrlGet(txn, rri->requestBufp, url) != TS_SUCCESS) {
    TSDebug("balancer", "no cache key");
    goto done;
  }

  str = TSUrlStringGet(rri->requestBufp, url, &len);
  if (str && len) {
    TSDebug("balancer", "%s(%.*s)", __func__, len, str);
    MD5_Update(ctx, str, len);
  }

done:
  if (url != TS_NULL_MLOC) {
    TSHandleMLocRelease(rri->requestBufp, TS_NULL_MLOC, url);
  }

  TSfree(str);
}

struct HashBalancer : public BalancerInstance {
  typedef std::map<md5_key, BalancerTarget> hash_ring_type;
  using hash_part_type = std::vector<HashComponent>;

  enum {
    iterations = 10,
  };

  HashBalancer() { this->hash_parts.push_back(HashTxnUrl); }
  void
  push_target(const BalancerTarget &target) override
  {
    for (unsigned i = 0; i < iterations; ++i) {
      this->hash_ring.insert(std::make_pair(md5_key(target, i), target));
    }
  }

  const BalancerTarget &
  balance(TSHttpTxn txn, TSRemapRequestInfo *rri) override
  {
    md5_key key;
    MD5_CTX ctx;
    hash_ring_type::const_iterator loc;

    // We'd better have some hash functions set by now ...
    TSReleaseAssert(!hash_parts.empty());

    MD5_Init(&ctx);

    for (hash_part_type::const_iterator i = this->hash_parts.begin(); i != this->hash_parts.end(); ++i) {
      (*i)(txn, rri, &ctx);
    }

    MD5_Final(key.key, &ctx);

    // OK, now look up this hash in the hash ring. lower_bound() finds the first element that is not less than the
    // target, so the element we find is the first key that is greater than our target. To visualize this in the
    // hash ring, that means that each node owns the preceding keyspace (ie. the node is at the end of each keyspace
    // range). This means that when we wrap, the first node owns the wrapping portion of the keyspace.
    loc = this->hash_ring.lower_bound(key);
    if (loc == this->hash_ring.end()) {
      loc = this->hash_ring.begin();
    }

    return loc->second;
  }

  hash_ring_type hash_ring;
  hash_part_type hash_parts;
};

} // namespace

BalancerInstance *
MakeHashBalancer(const char *options)
{
  HashBalancer *hash = new HashBalancer();
  char *opt;
  char *tmp;

  TSDebug("balancer", "making hash balancer with options '%s'", options);

  if (options) {
    hash->hash_parts.clear(); // clear the default hash type if we have options
    options = tmp = strdup(options);
    while ((opt = strsep(&tmp, ",")) != nullptr) {
      if (strcmp(opt, "key") == 0) {
        hash->hash_parts.push_back(HashTxnKey);
      } else if (strcmp(opt, "url") == 0) {
        hash->hash_parts.push_back(HashTxnUrl);
      } else if (strcmp(opt, "srcaddr") == 0) {
        hash->hash_parts.push_back(HashTxnSrcaddr);
      } else if (strcmp(opt, "dstaddr") == 0) {
        hash->hash_parts.push_back(HashTxnDstaddr);
      } else {
        TSError("[balancer] Ignoring invalid hash field '%s'", opt);
      }
    }

    free((void *)options);
  }

  return hash;
}
