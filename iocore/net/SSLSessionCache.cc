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

#include "P_SSLConfig.h"
#include "SSLSessionCache.h"
#include "SSLStats.h"

#include <cstring>

#define SSLSESSIONCACHE_STRINGIFY0(x) #x
#define SSLSESSIONCACHE_STRINGIFY(x) SSLSESSIONCACHE_STRINGIFY0(x)
#define SSLSESSIONCACHE_LINENO SSLSESSIONCACHE_STRINGIFY(__LINE__)

#ifdef DEBUG
#define PRINT_BUCKET(x) this->print(x " at " __FILE__ ":" SSLSESSIONCACHE_LINENO);
#else
#define PRINT_BUCKET(x)
#endif

/* Session Cache */
SSLSessionCache::SSLSessionCache() : nbuckets(SSLConfigParams::session_cache_number_buckets)
{
  Debug("ssl.session_cache", "Created new ssl session cache %p with %zu buckets each with size max size %zu", this, nbuckets,
        SSLConfigParams::session_cache_max_bucket_size);

  session_bucket = new SSLSessionBucket[nbuckets];
}

SSLSessionCache::~SSLSessionCache()
{
  delete[] session_bucket;
}

int
SSLSessionCache::getSessionBuffer(const SSLSessionID &sid, char *buffer, int &len) const
{
  uint64_t hash            = sid.hash();
  uint64_t target_bucket   = hash % nbuckets;
  SSLSessionBucket *bucket = &session_bucket[target_bucket];

  return bucket->getSessionBuffer(sid, buffer, len);
}

bool
SSLSessionCache::getSession(const SSLSessionID &sid, SSL_SESSION **sess, ssl_session_cache_exdata **data) const
{
  uint64_t hash            = sid.hash();
  uint64_t target_bucket   = hash % nbuckets;
  SSLSessionBucket *bucket = &session_bucket[target_bucket];

  if (is_debug_tag_set("ssl.session_cache")) {
    char buf[sid.len * 2 + 1];
    sid.toString(buf, sizeof(buf));
    Debug("ssl.session_cache.get", "SessionCache looking in bucket %" PRId64 " (%p) for session '%s' (hash: %" PRIX64 ").",
          target_bucket, bucket, buf, hash);
  }

  return bucket->getSession(sid, sess, data);
}

void
SSLSessionCache::removeSession(const SSLSessionID &sid)
{
  uint64_t hash            = sid.hash();
  uint64_t target_bucket   = hash % nbuckets;
  SSLSessionBucket *bucket = &session_bucket[target_bucket];

  if (is_debug_tag_set("ssl.session_cache")) {
    char buf[sid.len * 2 + 1];
    sid.toString(buf, sizeof(buf));
    Debug("ssl.session_cache.remove", "SessionCache using bucket %" PRId64 " (%p): Removing session '%s' (hash: %" PRIX64 ").",
          target_bucket, bucket, buf, hash);
  }

  if (ssl_rsb) {
    SSL_INCREMENT_DYN_STAT(ssl_session_cache_eviction);
  }
  bucket->removeSession(sid);
}

void
SSLSessionCache::insertSession(const SSLSessionID &sid, SSL_SESSION *sess, SSL *ssl)
{
  uint64_t hash            = sid.hash();
  uint64_t target_bucket   = hash % nbuckets;
  SSLSessionBucket *bucket = &session_bucket[target_bucket];

  if (is_debug_tag_set("ssl.session_cache")) {
    char buf[sid.len * 2 + 1];
    sid.toString(buf, sizeof(buf));
    Debug("ssl.session_cache.insert", "SessionCache using bucket %" PRId64 " (%p): Inserting session '%s' (hash: %" PRIX64 ").",
          target_bucket, bucket, buf, hash);
  }

  bucket->insertSession(sid, sess, ssl);
}

void
SSLSessionBucket::insertSession(const SSLSessionID &id, SSL_SESSION *sess, SSL *ssl)
{
  size_t len = i2d_SSL_SESSION(sess, nullptr); // make sure we're not going to need more than SSL_MAX_SESSION_SIZE bytes
  /* do not cache a session that's too big. */
  if (len > static_cast<size_t>(SSL_MAX_SESSION_SIZE)) {
    Debug("ssl.session_cache", "Unable to save SSL session because size of %zd exceeds the max of %d", len, SSL_MAX_SESSION_SIZE);
    return;
  }

  if (is_debug_tag_set("ssl.session_cache")) {
    char buf[id.len * 2 + 1];
    id.toString(buf, sizeof(buf));
    Debug("ssl.session_cache", "Inserting session '%s' to bucket %p.", buf, this);
  }

  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  if (!lock.is_locked()) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_lock_contention);
    }
    if (SSLConfigParams::session_cache_skip_on_lock_contention) {
      return;
    }
    lock.acquire(this_ethread());
  }

  PRINT_BUCKET("insertSession before")
  if (queue.size >= static_cast<int>(SSLConfigParams::session_cache_max_bucket_size)) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_eviction);
    }
    removeOldestSession();
  }

  // Don't insert if it is already there
  SSLSession *node = queue.tail;
  while (node) {
    if (node->session_id == id) {
      return;
    }
    node = node->link.prev;
  }

  Ptr<IOBufferData> buf;
  Ptr<IOBufferData> buf_exdata;
  size_t len_exdata = sizeof(ssl_session_cache_exdata);
  buf               = new_IOBufferData(buffer_size_to_index(len, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  ink_release_assert(static_cast<size_t>(buf->block_size()) >= len);
  unsigned char *loc = reinterpret_cast<unsigned char *>(buf->data());
  i2d_SSL_SESSION(sess, &loc);
  buf_exdata = new_IOBufferData(buffer_size_to_index(len, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  ink_release_assert(static_cast<size_t>(buf_exdata->block_size()) >= len_exdata);
  ssl_session_cache_exdata *exdata = reinterpret_cast<ssl_session_cache_exdata *>(buf_exdata->data());
  // This could be moved to a function in charge of populating exdata
  exdata->curve = (ssl == nullptr) ? 0 : SSLGetCurveNID(ssl);

  ats_scoped_obj<SSLSession> ssl_session(new SSLSession(id, buf, len, buf_exdata));

  /* do the actual insert */
  queue.enqueue(ssl_session.release());

  PRINT_BUCKET("insertSession after")
}

int
SSLSessionBucket::getSessionBuffer(const SSLSessionID &id, char *buffer, int &len)
{
  int true_len = 0;
  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  if (!lock.is_locked()) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_lock_contention);
    }
    if (SSLConfigParams::session_cache_skip_on_lock_contention) {
      return true_len;
    }

    lock.acquire(this_ethread());
  }

  // We work backwards because that's the most likely place we'll find our session...
  SSLSession *node = queue.tail;
  while (node) {
    if (node->session_id == id) {
      true_len = node->len_asn1_data;
      if (buffer) {
        const unsigned char *loc = reinterpret_cast<const unsigned char *>(node->asn1_data->data());
        if (true_len < len) {
          len = true_len;
        }
        memcpy(buffer, loc, len);
        return true_len;
      }
    }
    node = node->link.prev;
  }
  return 0;
}

bool
SSLSessionBucket::getSession(const SSLSessionID &id, SSL_SESSION **sess, ssl_session_cache_exdata **data)
{
  char buf[id.len * 2 + 1];
  buf[0] = '\0'; // just to be safe.
  if (is_debug_tag_set("ssl.session_cache")) {
    id.toString(buf, sizeof(buf));
  }

  Debug("ssl.session_cache", "Looking for session with id '%s' in bucket %p", buf, this);

  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  if (!lock.is_locked()) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_lock_contention);
    }
    if (SSLConfigParams::session_cache_skip_on_lock_contention) {
      return false;
    }

    lock.acquire(this_ethread());
  }

  PRINT_BUCKET("getSession")

  // We work backwards because that's the most likely place we'll find our session...
  SSLSession *node = queue.tail;
  while (node) {
    if (node->session_id == id) {
      const unsigned char *loc = reinterpret_cast<const unsigned char *>(node->asn1_data->data());
      *sess                    = d2i_SSL_SESSION(nullptr, &loc, node->len_asn1_data);
      if (data != nullptr) {
        ssl_session_cache_exdata *exdata = reinterpret_cast<ssl_session_cache_exdata *>(node->extra_data->data());
        *data                            = exdata;
      }

      return true;
    }
    node = node->link.prev;
  }

  Debug("ssl.session_cache", "Session with id '%s' not found in bucket %p.", buf, this);
  return false;
}

void inline SSLSessionBucket::print(const char *ref_str) const
{
  /* NOTE: This method assumes you're already holding the bucket lock */
  if (!is_debug_tag_set("ssl.session_cache.bucket")) {
    return;
  }

  fprintf(stderr, "-------------- BUCKET %p (%s) ----------------\n", this, ref_str);
  fprintf(stderr, "Current Size: %d, Max Size: %zd\n", queue.size, SSLConfigParams::session_cache_max_bucket_size);
  fprintf(stderr, "Queue: \n");

  SSLSession *node = queue.head;
  while (node) {
    char s_buf[2 * node->session_id.len + 1];
    node->session_id.toString(s_buf, sizeof(s_buf));
    fprintf(stderr, "  %s\n", s_buf);
    node = node->link.next;
  }
}

void inline SSLSessionBucket::removeOldestSession()
{
  // Caller must hold the bucket lock.
  ink_assert(this_ethread() == mutex->thread_holding);

  PRINT_BUCKET("removeOldestSession before")
  while (queue.head && queue.size >= static_cast<int>(SSLConfigParams::session_cache_max_bucket_size)) {
    SSLSession *old_head = queue.pop();
    if (is_debug_tag_set("ssl.session_cache")) {
      char buf[old_head->session_id.len * 2 + 1];
      old_head->session_id.toString(buf, sizeof(buf));
      Debug("ssl.session_cache", "Removing session '%s' from bucket %p because the bucket has size %d and max %zd", buf, this,
            (queue.size + 1), SSLConfigParams::session_cache_max_bucket_size);
    }
    delete old_head;
  }
  PRINT_BUCKET("removeOldestSession after")
}

void
SSLSessionBucket::removeSession(const SSLSessionID &id)
{
  SCOPED_MUTEX_LOCK(lock, mutex, this_ethread()); // We can't bail on contention here because this session MUST be removed.
  SSLSession *node = queue.head;
  while (node) {
    if (node->session_id == id) {
      queue.remove(node);
      delete node;
      return;
    }
    node = node->link.next;
  }
}

/* Session Bucket */
SSLSessionBucket::SSLSessionBucket() : mutex(new_ProxyMutex()) {}

SSLSessionBucket::~SSLSessionBucket() {}
