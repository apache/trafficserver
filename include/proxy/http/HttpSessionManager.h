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
#include <string_view>
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
  /// @param thread The thread that owns this pool, or @c nullptr for the shared pool.
  explicit ServerSessionPool(EThread *thread = nullptr);
  /// Handle events from server sessions.
  int         eventHandler(int event, void *data);
  static bool validate_host_sni(HttpSM *sm, NetVConnection *netvc);
  static bool validate_sni(HttpSM *sm, NetVConnection *netvc);
  /** @return true if a session established with @a session_sni would be established again for a
      request that sends @a proposed_sni.

      @a session_sni comes from TLSSNISupport and is "" rather than nullptr when the connection sent
      no name, so a session established without an SNI matches a request that would not send one.
      A nullptr @a session_sni is treated the same as "".
   */
  static bool sni_matches(std::string_view proposed_sni, const char *session_sni);
  /** @return true if a pooled raw public key session may carry @a sm's request.

      The pinned key set is the whole of what authenticated such a session, so this compares that set
      with the one @a sm's own connection would pin, rather than inferring it from a name.

      @param sm The transaction looking for a session.
      @param netvc A pooled origin connection for which origin_pinned_raw_public_key() is true.
   */
  static bool validate_pinned_origin(HttpSM *sm, NetVConnection *netvc);
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

  /** Search the pool for a server session compatible with the given address, hostname, and state machine.
   *
   * The selection criteria are controlled by @p match_style. At least one of
   * @c TS_SERVER_SESSION_SHARING_MATCH_MASK_IP or @c TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY
   * must be set for a match to be possible; if neither is set, @c HSMresult_t::NOT_FOUND is
   * returned unconditionally.
   *
   * Regardless of @p match_style, a session that supports multiplexed streams is selected only if
   * its origin certificate is valid for the host that @p sm would verify on a new connection.
   *
   * When a compatible session is found and does not support multiplexed streams, it is removed from
   * the pool and ownership transfers to the caller. A session that supports multiplexed streams is
   * not removed and remains available for subsequent acquisitions.
   *
   * @param[in]  addr           Remote address and port to match.
   * @param[in]  hostname_hash  Hash of the target hostname, computed the same way as
   *                            @c PoolableSession::hostname_hash.
   * @param[in]  match_style    Bitmask of @c TSServerSessionSharingMatchMask values specifying which
   *                            attributes must agree between the candidate session and the request.
   * @param[in]  sm             The requesting HTTP state machine. Must be non-null unless no SNI,
   *                            HOSTSNISYNC, or CERT bit is set in @p match_style and no session
   *                            supporting multiplexed streams has been added to this pool; when
   *                            non-null, @c t_state.txn_conf,
   *                            @c t_state.scheme, and @c t_state.hdr_info.server_request must be
   *                            initialized.
   * @param[out] server_session Set to the matched session when @c HSMresult_t::DONE is returned;
   *                            @c nullptr otherwise.
   *
   * @return @c HSMresult_t::DONE if a matching session was found;
   *         @c HSMresult_t::NOT_FOUND otherwise.
   *
   * @pre  Called on an @c EThread.
   *
   * @post If the returned session was removed from the pool, @c http_rsb.pooled_server_connections
   *       is decremented, and the caller must redirect the session's I/O away from this pool before
   *       this pool can receive another event for it.
   *
   * @par Thread Safety
   *   Not thread-safe.
   */
  HSMresult_t acquireSession(sockaddr const *addr, CryptoHash const &hostname_hash, TSServerSessionSharingMatchMask match_style,
                             HttpSM *sm, PoolableSession *&server_session);
  /** Release a session to the pool.

      @return @c true if the session was pooled; @c false if the session could not be pooled
              (the caller is responsible for closing it in that case).
   */
  bool releaseSession(PoolableSession *ss);

  /** Ask the thread that owns this pool to close its sessions in the shared pool.

      This can be called from any thread.
   */
  void requestPurge();

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

  /// The thread that owns this pool, @c nullptr for the shared pool.
  EThread *m_thread = nullptr;
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
  /** Close the keep-alive sessions in the shared pool.

      Each thread closes its own sessions. The current thread's are closed before this returns if
      the shared pool lock is free, the others when their thread next runs.
   */
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
