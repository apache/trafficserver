/** @file

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

#pragma once

#include "tscore/List.h"
#include "tscore/ink_mutex.h"
#include "P_EventSystem.h"
#include "records/I_RecProcess.h"
#include "tscore/ink_platform.h"
#include "P_SSLUtils.h"
#include "ts/apidefs.h"
#include <openssl/ssl.h>
#include <mutex>
#include <shared_mutex>

#define SSL_MAX_SESSION_SIZE 256

struct ssl_session_cache_exdata {
  ssl_curve_id curve = 0;
};

inline void
hash_combine(uint64_t &seed, uint64_t hash)
{
  // using boost's version of hash combine, substituting magic number with a 64bit version
  // https://www.boost.org/doc/libs/1_43_0/doc/html/hash/reference.html#boost.hash_combine
  seed ^= hash + 0x9E3779B97F4A7C15 + (seed << 6) + (seed >> 2);
}

struct SSLSessionID : public TSSslSessionID {
  SSLSessionID(const unsigned char *s, size_t l)
  {
    len = l;
    ink_release_assert(l <= sizeof(bytes));
    memcpy(bytes, s, l);
    hash();
  }

  SSLSessionID(const SSLSessionID &other)
  {
    if (other.len)
      memcpy(bytes, other.bytes, other.len);

    len = other.len;
    hash();
  }

  bool
  operator<(const SSLSessionID &other) const
  {
    if (len != other.len)
      return len < other.len;

    return (memcmp(bytes, other.bytes, len) < 0);
  }

  SSLSessionID &
  operator=(const SSLSessionID &other)
  {
    if (other.len)
      memcpy(bytes, other.bytes, other.len);

    len = other.len;
    return *this;
  }

  bool
  operator==(const SSLSessionID &other) const
  {
    if (len != other.len)
      return false;

    // memcmp returns 0 on equal
    return (memcmp(bytes, other.bytes, len) == 0);
  }

  const char *
  toString(char *buf, size_t buflen) const
  {
    char *cur_pos = buf;
    for (size_t i = 0; i < len && buflen > 0; ++i) {
      if (buflen > 2) { // we have enough space for 3 bytes, 2 hex and 1 null terminator
        snprintf(cur_pos, 3 /* including a null terminator */, "%02hhX", static_cast<unsigned char>(bytes[i]));
        cur_pos += 2;
        buflen -= 2;
      } else { // not enough space for any more hex bytes, just null terminate
        *cur_pos = '\0';
        break;
      }
    }
    return buf;
  }

  uint64_t
  hash() const
  {
    if (hash_value == 0) {
      // because the session ids should be uniformly random, we can treat the bits as a hash value
      // however we need to combine them if the length is longer than 64bits
      if (len >= sizeof(uint64_t)) {
        uint64_t seed = 0;
        for (uint64_t i = 0; i < len; i += sizeof(uint64_t)) {
          hash_combine(seed, static_cast<uint64_t>(bytes[i]));
        }
        hash_value = seed;
      } else if (len) {
        hash_value = static_cast<uint64_t>(bytes[0]);
      } else {
        hash_value = 0;
      }
    }
    return hash_value;
  }

private:
  mutable uint64_t hash_value = 0;
};

struct SSLSessionIDHash {
  uint64_t
  operator()(const SSLSessionID &id) const
  {
    return id.hash();
  }
};

class SSLSession
{
public:
  SSLSessionID session_id;
  Ptr<IOBufferData> asn1_data; /* this is the ASN1 representation of the SSL_CTX */
  size_t len_asn1_data;
  Ptr<IOBufferData> extra_data;
  ink_hrtime time_stamp;

  SSLSession(const SSLSessionID &id, const Ptr<IOBufferData> &ssl_asn1_data, size_t len_asn1, Ptr<IOBufferData> &exdata,
             ink_hrtime ts)
    : session_id(id), asn1_data(ssl_asn1_data), len_asn1_data(len_asn1), extra_data(exdata), time_stamp(ts)
  {
  }

  LINK(SSLSession, link);
};

class SSLSessionBucket
{
public:
  SSLSessionBucket();
  ~SSLSessionBucket();
  void insertSession(const SSLSessionID &sid, SSL_SESSION *sess, SSL *ssl);
  bool getSession(const SSLSessionID &sid, SSL_SESSION **sess, ssl_session_cache_exdata **data);
  int getSessionBuffer(const SSLSessionID &sid, char *buffer, int &len);
  void removeSession(const SSLSessionID &sid);

private:
  /* these method must be used while hold the lock */
  void print(const char *) const;
  void removeOldestSession(const std::unique_lock<std::shared_mutex> &lock);

  mutable std::shared_mutex mutex;
  std::unordered_map<SSLSessionID, SSLSession *, SSLSessionIDHash> bucket_data;
  std::map<ink_hrtime, SSLSession *> bucket_data_ts;
};

class SSLSessionCache
{
public:
  bool getSession(const SSLSessionID &sid, SSL_SESSION **sess, ssl_session_cache_exdata **data) const;
  int getSessionBuffer(const SSLSessionID &sid, char *buffer, int &len) const;
  void insertSession(const SSLSessionID &sid, SSL_SESSION *sess, SSL *ssl);
  void removeSession(const SSLSessionID &sid);
  SSLSessionCache();
  ~SSLSessionCache();

  SSLSessionCache(const SSLSessionCache &) = delete;
  SSLSessionCache &operator=(const SSLSessionCache &) = delete;

private:
  SSLSessionBucket *session_bucket = nullptr;
  size_t nbuckets;
};
