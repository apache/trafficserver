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
#include <memory>

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
  std::shared_lock r_lock(mutex, std::try_to_lock);
  if (!r_lock.owns_lock()) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_lock_contention);
    }
    if (SSLConfigParams::session_cache_skip_on_lock_contention) {
      return;
    }
    r_lock.lock();
  }

  // Don't insert if it is already there
  if (bucket_map.find(id) != bucket_map.end()) {
    return;
  }

  r_lock.unlock();

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
  exdata->curve = (ssl == nullptr) ? 0 : SSLGetCurveNID(ssl);

  std::unique_ptr<SSLSession> ssl_session(new SSLSession(id, buf, len, buf_exdata));

  std::unique_lock w_lock(mutex, std::try_to_lock);
  if (!w_lock.owns_lock()) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_lock_contention);
    }
    if (SSLConfigParams::session_cache_skip_on_lock_contention) {
      return;
    }
    w_lock.lock();
  }

  PRINT_BUCKET("insertSession before")
  if (bucket_map.size() >= SSLConfigParams::session_cache_max_bucket_size) {
    if (ssl_rsb) {
      SSL_INCREMENT_DYN_STAT(ssl_session_cache_eviction);
    }
    removeOldestSession(w_lock);
  }

  /* do the actual insert */
  auto node = ssl_session.release();
  bucket_que.enqueue(node);
  bucket_map[id] = node;

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

  auto entry = bucket_map.find(id);
  if (buffer && entry != bucket_map.end()) {
    true_len                 = entry->second->len_asn1_data;
    const unsigned char *loc = reinterpret_cast<const unsigned char *>(entry->second->asn1_data->data());
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

  auto entry = bucket_map.find(id);
  if (entry == bucket_map.end()) {
    Debug("ssl.session_cache", "Session with id '%s' not found in bucket %p.", buf, this);
    return false;
  }
  const unsigned char *loc = reinterpret_cast<const unsigned char *>(entry->second->asn1_data->data());
  *sess                    = d2i_SSL_SESSION(nullptr, &loc, entry->second->len_asn1_data);
  if (data != nullptr) {
    ssl_session_cache_exdata *exdata = reinterpret_cast<ssl_session_cache_exdata *>(entry->second->extra_data->data());
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
  fprintf(stderr, "Current Size: %ld, Max Size: %zd\n", bucket_map.size(), SSLConfigParams::session_cache_max_bucket_size);
  fprintf(stderr, "Bucket: \n");

  for (auto &x : bucket_map) {
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

  while (bucket_que.head && bucket_que.size >= static_cast<int>(SSLConfigParams::session_cache_max_bucket_size)) {
    auto node = bucket_que.pop();
    bucket_map.erase(node->session_id);
    delete node;
  }

  PRINT_BUCKET("removeOldestSession after")
}

void
SSLSessionBucket::removeSession(const SSLSessionID &id)
{
  // We can't bail on contention here because this session MUST be removed.
  std::unique_lock lock(mutex);

  PRINT_BUCKET("removeSession before")

  auto entry = bucket_map.find(id);
  if (entry != bucket_map.end()) {
    auto node = entry->second;
    bucket_que.remove(node);
    bucket_map.erase(entry);
    delete node;
  }

  PRINT_BUCKET("removeSession after")

  return;
}

// Custom deleter for shared origin sessions
void
SSLSessDeleter(SSL_SESSION *_p)
{
  SSL_SESSION_free(_p);
}

/* Session Bucket */
SSLSessionBucket::SSLSessionBucket() {}

SSLSessionBucket::~SSLSessionBucket() {}

SSLOriginSessionCache::SSLOriginSessionCache() {}

SSLOriginSessionCache::~SSLOriginSessionCache() {}

void
SSLOriginSessionCache::insert_session(const std::string &lookup_key, SSL_SESSION *sess, SSL *ssl)
{
  size_t len = i2d_SSL_SESSION(sess, nullptr); // make sure we're not going to need more than SSL_MAX_ORIG_SESSION_SIZE bytes

  /* do not cache a session that's too big. */
  if (len > static_cast<size_t>(SSL_MAX_ORIG_SESSION_SIZE)) {
    Debug("ssl.origin_session_cache", "Unable to save SSL session because size of %zd exceeds the max of %d", len,
          SSL_MAX_ORIG_SESSION_SIZE);
    return;
  } else if (len == 0) {
    Debug("ssl.origin_session_cache", "Unable to save SSL session because size is 0");
    return;
  }

  // Duplicate the session from the connection, we'll be keeping track the ref-count with a shared pointer ourself
  SSL_SESSION *sess_ptr = SSLSessionDup(sess);

  if (is_debug_tag_set("ssl.origin_session_cache")) {
    Debug("ssl.origin_session_cache", "insert session: %s = %p", lookup_key.c_str(), sess_ptr);
  }

  // Create the shared pointer to the session, with the custom deleter
  std::shared_ptr<SSL_SESSION> shared_sess(sess_ptr, SSLSessDeleter);
  ssl_curve_id curve = (ssl == nullptr) ? 0 : SSLGetCurveNID(ssl);
  std::unique_ptr<SSLOriginSession> ssl_orig_session(new SSLOriginSession(lookup_key, curve, shared_sess));
  auto new_node = ssl_orig_session.release();

  std::unique_lock lock(mutex);
  auto entry = orig_sess_map.find(lookup_key);
  if (entry != orig_sess_map.end()) {
    auto node = entry->second;
    if (is_debug_tag_set("ssl.origin_session_cache")) {
      Debug("ssl.origin_session_cache", "found duplicate key: %s, replacing %p with %p", lookup_key.c_str(),
            node->shared_sess.get(), sess_ptr);
    }
    orig_sess_que.remove(node);
    orig_sess_map.erase(entry);
    delete node;
  } else if (orig_sess_map.size() >= SSLConfigParams::origin_session_cache_size) {
    if (is_debug_tag_set("ssl.origin_session_cache")) {
      Debug("ssl.origin_session_cache", "origin session cache full, removing oldest session");
    }
    remove_oldest_session(lock);
  }

  orig_sess_que.enqueue(new_node);
  orig_sess_map[lookup_key] = new_node;
}

std::shared_ptr<SSL_SESSION>
SSLOriginSessionCache::get_session(const std::string &lookup_key, ssl_curve_id *curve)
{
  if (is_debug_tag_set("ssl.origin_session_cache")) {
    Debug("ssl.origin_session_cache", "get session: %s", lookup_key.c_str());
  }

  std::shared_lock lock(mutex);
  auto entry = orig_sess_map.find(lookup_key);
  if (entry == orig_sess_map.end()) {
    return nullptr;
  }

  if (curve != nullptr) {
    *curve = entry->second->curve_id;
  }

  return entry->second->shared_sess;
}

void
SSLOriginSessionCache::remove_oldest_session(const std::unique_lock<std::shared_mutex> &lock)
{
  // Caller must hold the bucket shared_mutex with unique_lock.
  ink_release_assert(lock.owns_lock());

  while (orig_sess_que.head && orig_sess_que.size >= static_cast<int>(SSLConfigParams::origin_session_cache_size)) {
    auto node = orig_sess_que.pop();
    if (is_debug_tag_set("ssl.origin_session_cache")) {
      Debug("ssl.origin_session_cache", "remove oldest session: %s, session ptr: %p", node->key.c_str(), node->shared_sess.get());
    }
    orig_sess_map.erase(node->key);
    delete node;
  }
}

void
SSLOriginSessionCache::remove_session(const std::string &lookup_key)
{
  // We can't bail on contention here because this session MUST be removed.
  std::unique_lock lock(mutex);
  auto entry = orig_sess_map.find(lookup_key);
  if (entry != orig_sess_map.end()) {
    auto node = entry->second;
    if (is_debug_tag_set("ssl.origin_session_cache")) {
      Debug("ssl.origin_session_cache", "remove session: %s, session ptr: %p", lookup_key.c_str(), node->shared_sess.get());
    }
    orig_sess_que.remove(node);
    orig_sess_map.erase(entry);
    delete node;
  }

  return;
}
