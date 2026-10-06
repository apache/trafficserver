/** @file

  A brief file description

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

/****************************************************************************

   HttpSessionManager.h

   Description:


 ****************************************************************************/

#pragma once

#include "iocore/eventsystem/EventSystem.h"
#include "proxy/PoolableSession.h"
#include "swoc/IntrusiveHashMap.h"

#include <atomic>
#include <vector>

class ProxyTransaction;
class HttpSM;

void initialize_thread_for_http_sessions(EThread *thread, int thread_index);

enum class HSMresult_t {
  DONE,
  RETRY,
  NOT_FOUND,
};

/** A pool of server sessions.

    This is a continuation so that it can get callbacks from the server sessions.
    This is used to track remote closes on the sessions so they can be cleaned up.

    @internal Cleanup is the real reason we will always need an IP address mapping for the
    sessions. The I/O callback will have only the NetVC and thence the remote IP address for the
    closed session and we need to be able find it based on that.
*/
class ServerSessionPool : public Continuation
{
public:
  /// Default constructor.
  /// Constructs an empty pool.
  ServerSessionPool();
  /// Handle events from server sessions.
  int         eventHandler(int event, void *data);
  static bool validate_host_sni(HttpSM *sm, NetVConnection *netvc);
  static bool validate_sni(HttpSM *sm, NetVConnection *netvc);
  static bool validate_cert(HttpSM *sm, NetVConnection *netvc);
  void        removeSession(PoolableSession *ssn);
  void        addSession(PoolableSession *ssn);
  int
  count() const
  {
    return m_ip_pool.count();
  }

private:
  using IPTable   = swoc::IntrusiveHashMap<PoolableSession::IPLinkage>;
  using FQDNTable = swoc::IntrusiveHashMap<PoolableSession::FQDNLinkage>;

public:
  /** Check if a session matches address and host name.
   */
  static bool match(PoolableSession *ss, sockaddr const *addr, CryptoHash const &host_hash,
                    TSServerSessionSharingMatchMask match_style);

  /** Get a session from the pool.

      The session is selected based on @a match_style equivalently to @a match. If found the session
      is removed from the pool.

      @return A pointer to the session or @c NULL if not matching session was found.
  */
  HSMresult_t acquireSession(sockaddr const *addr, CryptoHash const &host_hash, TSServerSessionSharingMatchMask match_style,
                             HttpSM *sm, PoolableSession *&server_session);
  /** Release a session to the pool.

      @return @c true if the session was pooled; @c false if the session could not be pooled
              (the caller is responsible for closing it in that case).
   */
  bool releaseSession(PoolableSession *ss);

  /** Ask the thread that owns this pool to close its sessions in the shared pool.

      This can be called from any thread.

      @param thread The thread that owns this pool.
   */
  void requestPurge(EThread *thread);

  /** Remove the sessions whose connections belong to @a thread.

      The caller must hold the pool lock and is responsible for closing the sessions.
   */
  void removeSessionsOf(EThread *thread, std::vector<PoolableSession *> &sessions);

  /// Close sessions whose connections belong to the current thread.
  static void closeSessions(std::vector<PoolableSession *> const &sessions);

  /** Close a session from an event on the current thread.

      This is for closing a session while a shared pool is locked. The session must already be out
      of the pool and this must be the pool of the thread that owns the session.
   */
  void deferClose(PoolableSession *ssn);

  // Pools of server sessions.
  // Note that each server session is stored in both pools.
  IPTable   m_ip_pool;
  FQDNTable m_fqdn_pool;

private:
  void closeDeferred();

  /// Sessions waiting to be closed by this pool's thread.
  std::vector<PoolableSession *> m_deferred_close;
  /// Set while this pool's thread has been asked to purge and has not yet done so.
  std::atomic<bool> m_purge_requested{false};
};

class HttpSessionManager
{
public:
  HttpSessionManager() {}
  ~HttpSessionManager() {}
  HSMresult_t acquire_session(HttpSM *sm, sockaddr const *addr, const char *hostname, ProxyTransaction *ua_txn);
  HSMresult_t release_session(PoolableSession *to_release);
  /// Close the keep-alive sessions in the shared pool. Each thread closes its own, so this returns before they are closed.
  void purge_keepalives();
  /** Close the shared pool sessions whose connections belong to the current thread.

      @return @c false if the shared pool was locked by another thread and nothing was done.
   */
  bool purge_thread_keepalives();
  void init();
  int  main_handler(int event, void *data);
  void
  set_pool_type(int pool_type)
  {
    m_pool_type = static_cast<TSServerSessionSharingPoolType>(pool_type);
  }
  TSServerSessionSharingPoolType
  get_pool_type() const
  {
    return m_pool_type;
  }

private:
  /// Global pool, used if not per thread pools.
  /// @internal We delay creating this because the session manager is created during global statistics init.
  ServerSessionPool             *m_g_pool = nullptr;
  HSMresult_t                    _acquire_session(sockaddr const *ip, CryptoHash const &hostname_hash, HttpSM *sm,
                                                  TSServerSessionSharingMatchMask match_style, TSServerSessionSharingPoolType pool_type);
  TSServerSessionSharingPoolType m_pool_type = TS_SERVER_SESSION_SHARING_POOL_THREAD;
};

extern HttpSessionManager httpSessionManager;
