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

#include "ts/Map.h"
#include "ts/List.h"
#include "ts/ink_mutex.h"
#include "P_EventSystem.h"
#include "P_AIO.h"
#include "I_RecProcess.h"
#include "ts/ink_platform.h"
#include "P_SSLUtils.h"
#include "ts/apidefs.h"
#include <openssl/ssl.h>

#define SSL_MAX_SESSION_SIZE 256

struct SSLSessionID : public TSSslSessionID {
  SSLSessionID(const unsigned char *s, size_t l)
  {
    len = l;
    ink_release_assert(l <= sizeof(bytes));
    memcpy(bytes, s, l);
  }

  SSLSessionID(const SSLSessionID &other)
  {
    if (other.len)
      memcpy(bytes, other.bytes, other.len);

    len = other.len;
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
    // because the session ids should be uniformly random let's just use the last 64 bits as the hash.
    // The first bytes could be interpreted as a name, and so not random.
    if (len >= sizeof(uint64_t)) {
      return *reinterpret_cast<uint64_t *>(const_cast<char *>(bytes + len - sizeof(uint64_t)));
    } else if (len) {
      return static_cast<uint64_t>(bytes[0]);
    } else {
      return 0;
    }
  }
};

class SSLSession
{
public:
  SSLSessionID session_id;
  Ptr<IOBufferData> asn1_data; /* this is the ASN1 representation of the SSL_CTX */
  size_t len_asn1_data;

  SSLSession(const SSLSessionID &id, Ptr<IOBufferData> ssl_asn1_data, size_t len_asn1)
    : session_id(id), asn1_data(ssl_asn1_data), len_asn1_data(len_asn1)
  {
  }

  LINK(SSLSession, link);
};

class SSLSessionBucket
{
public:
  SSLSessionBucket();
  ~SSLSessionBucket();
  void insertSession(const SSLSessionID &, SSL_SESSION *ctx);
  bool getSession(const SSLSessionID &, SSL_SESSION **ctx);
  int getSessionBuffer(const SSLSessionID &, char *buffer, int &len);
  void removeSession(const SSLSessionID &);

private:
  /* these method must be used while hold the lock */
  void print(const char *) const;
  void removeOldestSession();

  Ptr<ProxyMutex> mutex;
  CountQueue<SSLSession> queue;
};

class SSLSessionCache
{
public:
  bool getSession(const SSLSessionID &sid, SSL_SESSION **sess) const;
  int getSessionBuffer(const SSLSessionID &sid, char *buffer, int &len) const;
  void insertSession(const SSLSessionID &sid, SSL_SESSION *sess);
  void removeSession(const SSLSessionID &sid);
  SSLSessionCache();
  ~SSLSessionCache();

private:
  SSLSessionBucket *session_bucket;
  size_t nbuckets;
};
