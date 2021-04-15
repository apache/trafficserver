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
  exdata->curve  = (ssl == nullptr) ? 0 : SSLGetCurveNID(ssl);
  ink_hrtime now = Thread::get_hrtime_updated();

  ats_scoped_obj<SSLSession> ssl_session(new SSLSession(id, buf, len, buf_exdata, now));

  std::unique_lock lock(mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_lock_contention);
    }
    if (SSLConfigParams::session_cache_skip_on_lock_contention) {
      return;
    }
    lock.lock();
  }

  PRINT_BUCKET("insertSession before")
  if (bucket_data.size() >= SSLConfigParams::session_cache_max_bucket_size) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_eviction);
    }
    removeOldestSession(lock);
  }

  // Don't insert if it is already there
  if (bucket_data.find(id) != bucket_data.end()) {
    return;
  }

  /* do the actual insert */
  auto node           = ssl_session.release();
  bucket_data[id]     = node;
  bucket_data_ts[now] = node;

  PRINT_BUCKET("insertSession after")
}

int
SSLSessionBucket::getSessionBuffer(const SSLSessionID &id, char *buffer, int &len)
{
  int true_len = 0;
  std::shared_lock lock(mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_lock_contention);
    }
    if (SSLConfigParams::session_cache_skip_on_lock_contention) {
      return true_len;
    }
    lock.lock();
  }

  auto node = bucket_data.find(id);
  if (buffer && node != bucket_data.end()) {
    true_len                 = node->second->len_asn1_data;
    const unsigned char *loc = reinterpret_cast<const unsigned char *>(node->second->asn1_data->data());
    if (true_len < len) {
      len = true_len;
    }
    memcpy(buffer, loc, len);
    return true_len;
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

  std::shared_lock lock(mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_lock_contention);
    }
    if (SSLConfigParams::session_cache_skip_on_lock_contention) {
      return false;
    }
    lock.lock();
  }

  PRINT_BUCKET("getSession")

  auto node = bucket_data.find(id);
  if (node == bucket_data.end()) {
    Debug("ssl.session_cache", "Session with id '%s' not found in bucket %p.", buf, this);
    return false;
  }
  const unsigned char *loc = reinterpret_cast<const unsigned char *>(node->second->asn1_data->data());
  *sess                    = d2i_SSL_SESSION(nullptr, &loc, node->second->len_asn1_data);
  if (data != nullptr) {
    ssl_session_cache_exdata *exdata = reinterpret_cast<ssl_session_cache_exdata *>(node->second->extra_data->data());
    *data                            = exdata;
  }
  return true;
}

void inline SSLSessionBucket::print(const char *ref_str) const
{
  /* NOTE: This method assumes you're already holding the bucket lock */
  if (!is_debug_tag_set("ssl.session_cache.bucket")) {
    return;
  }

  fprintf(stderr, "-------------- BUCKET %p (%s) ----------------\n", this, ref_str);
  fprintf(stderr, "Current Size: %ld, Max Size: %zd\n", bucket_data.size(), SSLConfigParams::session_cache_max_bucket_size);
  fprintf(stderr, "Bucket: \n");

  for (auto &x : bucket_data) {
    char s_buf[2 * x.second->session_id.len + 1];
    x.second->session_id.toString(s_buf, sizeof(s_buf));
    fprintf(stderr, "  %s\n", s_buf);
  }
}

void inline SSLSessionBucket::removeOldestSession(const std::unique_lock<std::shared_mutex> &lock)
{
  // Caller must hold the bucket shared_mutex with unique_lock.
  ink_assert(lock.owns_lock());

  PRINT_BUCKET("removeOldestSession before")

  auto node = bucket_data_ts.begin();
  bucket_data.erase(node->second->session_id);
  delete node->second;
  bucket_data_ts.erase(node);

  PRINT_BUCKET("removeOldestSession after")
}

void
SSLSessionBucket::removeSession(const SSLSessionID &id)
{
  // We can't bail on contention here because this session MUST be removed.
  std::unique_lock lock(mutex);

  auto node = bucket_data.find(id);

  PRINT_BUCKET("removeSession before")

  if (node != bucket_data.end()) {
    bucket_data_ts.erase(node->second->time_stamp);
    delete node->second;
    bucket_data.erase(node);
  }

  PRINT_BUCKET("removeSession after")

  return;
}

/* Session Bucket */
SSLSessionBucket::SSLSessionBucket() {}

SSLSessionBucket::~SSLSessionBucket() {}
