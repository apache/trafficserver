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

#include "P_EventSystem.h"
#include "PoolableSession.h"
#include "tscore/IntrusiveHashMap.h"

class ProxyTransaction;
class HttpSM;

void initialize_thread_for_http_sessions(EThread *thread, int thread_index);

enum HSMresult_t {
  HSM_DONE,
  HSM_RETRY,
  HSM_NOT_FOUND,
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
  int eventHandler(int event, void *data);
  static bool validate_host_sni(HttpSM *sm, NetVConnection *netvc);
  static bool validate_sni(HttpSM *sm, NetVConnection *netvc);
  static bool validate_cert(HttpSM *sm, NetVConnection *netvc);
  int
  count() const
  {
    return m_ip_pool.count();
  }

private:
  void removeSession(PoolableSession *ssn);
  void addSession(PoolableSession *ssn);

  using IPTable   = IntrusiveHashMap<PoolableSession::IPLinkage>;
  using FQDNTable = IntrusiveHashMap<PoolableSession::FQDNLinkage>;

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
   */
  void releaseSession(PoolableSession *ss);

  /// Close all sessions and then clear the table.
  void purge();

  // Pools of server sessions.
  // Note that each server session is stored in both pools.
  IPTable m_ip_pool;
  FQDNTable m_fqdn_pool;
};

class HttpSessionManager
{
public:
  HttpSessionManager() {}
  ~HttpSessionManager() {}
  HSMresult_t acquire_session(HttpSM *sm, sockaddr const *addr, const char *hostname, ProxyTransaction *ua_txn);
  HSMresult_t release_session(PoolableSession *to_release);
  void purge_keepalives();
  void init();
  int main_handler(int event, void *data);
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
  /// @internal We delay creating this because the session manager is created during global statics init.
  ServerSessionPool *m_g_pool = nullptr;
  HSMresult_t _acquire_session(sockaddr const *ip, CryptoHash const &hostname_hash, HttpSM *sm,
                               TSServerSessionSharingMatchMask match_style, TSServerSessionSharingPoolType pool_type);
  TSServerSessionSharingPoolType m_pool_type = TS_SERVER_SESSION_SHARING_POOL_THREAD;
};

extern HttpSessionManager httpSessionManager;
