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
#include "P_SSLUtils.h"
#include "SSLStats.h"
#include "../eventsystem/P_IOBuffer.h"

#include <cstring>
#include <memory>
#include <shared_mutex>

namespace
{
DbgCtl dbg_ctl_ssl_origin_session_cache{"ssl.origin_session_cache"};
} // end anonymous namespace

// Custom deleter for shared origin sessions
void
SSLSessDeleter(SSL_SESSION *_p)
{
  SSL_SESSION_free(_p);
}

SSLOriginSessionCache::SSLOriginSessionCache() {}

SSLOriginSessionCache::~SSLOriginSessionCache() {}

void
SSLOriginSessionCache::insert_session(const std::string &lookup_key, SSL_SESSION *sess, SSL *ssl)
{
  size_t len = i2d_SSL_SESSION(sess, nullptr); // make sure we're not going to need more than SSL_MAX_ORIG_SESSION_SIZE bytes

  /* do not cache a session that's too big. */
  if (len > static_cast<size_t>(SSL_MAX_ORIG_SESSION_SIZE)) {
    Dbg(dbg_ctl_ssl_origin_session_cache, "Unable to save SSL session because size of %zd exceeds the max of %d", len,
        SSL_MAX_ORIG_SESSION_SIZE);
    return;
  } else if (len == 0) {
    Dbg(dbg_ctl_ssl_origin_session_cache, "Unable to save SSL session because size is 0");
    return;
  }

  // Duplicate the session from the connection, we'll be keeping track the ref-count with a shared pointer ourself
  SSL_SESSION *sess_ptr = SSLSessionDup(sess);

  Dbg(dbg_ctl_ssl_origin_session_cache, "insert session: %s = %p", lookup_key.c_str(), sess_ptr);

  ssl_curve_id                      curve = (ssl == nullptr) ? 0 : SSLGetCurveNID(ssl);
  std::unique_ptr<SSLOriginSession> ssl_orig_session(
    new SSLOriginSession(lookup_key, curve, std::shared_ptr<SSL_SESSION>{sess_ptr, SSLSessDeleter}));
  auto new_node = ssl_orig_session.release();

  std::unique_lock lock(mutex);
  auto             entry = orig_sess_map.find(lookup_key);
  if (entry != orig_sess_map.end()) {
    auto node = entry->second;
    Dbg(dbg_ctl_ssl_origin_session_cache, "found duplicate key: %s, replacing %p with %p", lookup_key.c_str(),
        node->shared_sess.get(), sess_ptr);
    orig_sess_que.remove(node);
    orig_sess_map.erase(entry);
    delete node;
  } else if (orig_sess_map.size() >= SSLConfigParams::origin_session_cache_size) {
    Dbg(dbg_ctl_ssl_origin_session_cache, "origin session cache full, removing oldest session");
    remove_oldest_session(lock);
  }

  orig_sess_que.enqueue(new_node);
  orig_sess_map[lookup_key] = new_node;
}

std::shared_ptr<SSL_SESSION>
SSLOriginSessionCache::get_session(const std::string &lookup_key, ssl_curve_id *curve)
{
  Dbg(dbg_ctl_ssl_origin_session_cache, "get session: %s", lookup_key.c_str());

  std::shared_lock lock(mutex);
  auto             entry = orig_sess_map.find(lookup_key);
  if (entry == orig_sess_map.end()) {
    return nullptr;
  }

  if (curve != nullptr) {
    *curve = entry->second->curve_id;
  }

  return entry->second->shared_sess;
}

void
SSLOriginSessionCache::remove_oldest_session(const std::unique_lock<ts::shared_mutex> &lock)
{
  // Caller must hold the bucket shared_mutex with unique_lock.
  ink_release_assert(lock.owns_lock());

  while (orig_sess_que.head && orig_sess_que.size >= static_cast<int>(SSLConfigParams::origin_session_cache_size)) {
    auto node = orig_sess_que.pop();
    Dbg(dbg_ctl_ssl_origin_session_cache, "remove oldest session: %s, session ptr: %p", node->key.c_str(), node->shared_sess.get());
    orig_sess_map.erase(node->key);
    delete node;
  }
}

void
SSLOriginSessionCache::remove_session(const std::string &lookup_key)
{
  // We can't bail on contention here because this session MUST be removed.
  std::unique_lock lock(mutex);
  auto             entry = orig_sess_map.find(lookup_key);
  if (entry != orig_sess_map.end()) {
    auto node = entry->second;
    Dbg(dbg_ctl_ssl_origin_session_cache, "remove session: %s, session ptr: %p", lookup_key.c_str(), node->shared_sess.get());
    orig_sess_que.remove(node);
    orig_sess_map.erase(entry);
    delete node;
  }

  return;
}
