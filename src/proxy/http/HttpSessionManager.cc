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

   HttpSessionManager.cc

   Description:


 ****************************************************************************/

#include "../../iocore/net/P_UnixNetVConnection.h"
#include "proxy/http/HttpSessionManager.h"
#include "proxy/ProxySession.h"
#include "proxy/http/HttpSM.h"
#include "proxy/http/HttpDebugNames.h"
#include "iocore/net/TLSSNISupport.h"
#include <iterator>

namespace
{
DbgCtl dbg_ctl_http_ss{"http_ss"};

} // end anonymous namespace

// Initialize a thread to handle HTTP session management
void
initialize_thread_for_http_sessions(EThread *thread)
{
  thread->server_session_pool = new ServerSessionPool;
}

HttpSessionManager httpSessionManager;

ServerSessionPool::ServerSessionPool() : Continuation(new_ProxyMutex()), m_ip_pool(1023), m_fqdn_pool(1023)
{
  SET_HANDLER(&ServerSessionPool::eventHandler);
  m_ip_pool.set_expansion_policy(IPTable::MANUAL);
  m_fqdn_pool.set_expansion_policy(FQDNTable::MANUAL);
}

void
ServerSessionPool::purge()
{
  // @c do_io_close can free the instance which clears the intrusive links and breaks the iterator.
  // Therefore @c do_io_close is called on a post-incremented iterator.
  m_ip_pool.apply([](PoolableSession *ssn) -> void { ssn->do_io_close(); });
  m_ip_pool.clear();
  m_fqdn_pool.clear();
}

bool
ServerSessionPool::match(PoolableSession *ss, sockaddr const *addr, CryptoHash const &hostname_hash,
                         TSServerSessionSharingMatchMask match_style)
{
  bool retval = match_style != 0;
  if (retval && (TS_SERVER_SESSION_SHARING_MATCH_MASK_IP & match_style)) {
    retval = ats_ip_addr_port_eq(ss->get_remote_addr(), addr);
  }
  if (retval && (TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY & match_style)) {
    retval = (ats_ip_port_cast(addr) == ats_ip_port_cast(ss->get_remote_addr()) && ss->hostname_hash == hostname_hash);
  }
  return retval;
}

bool
ServerSessionPool::validate_host_sni(HttpSM *sm, NetVConnection *netvc)
{
  bool retval = true;
  if (sm->t_state.scheme == URL_WKSIDX_HTTPS) {
    // The sni_servername of the connection was set on HttpSM::do_http_server_open
    // by fetching the hostname from the server request.  So the connection should only
    // be reused if the hostname in the new request is the same as the host name in the
    // original request
    if (auto snis = netvc->get_service<TLSSNISupport>(); snis) {
      const char *session_sni = snis->get_sni_server_name();
      if (session_sni && session_sni[0] != '\0') {
        // TS-4468: If the connection matches, make sure the SNI server
        // name (if present) matches the request hostname
        auto req_host{sm->t_state.hdr_info.server_request.host_get()};
        retval = strncasecmp(session_sni, req_host.data(), req_host.length()) == 0;
        Dbg(dbg_ctl_http_ss, "validate_host_sni host=%*.s, sni=%s", static_cast<int>(req_host.length()), req_host.data(),
            session_sni);
      }
    } else {
      retval = false;
    }
  }
  return retval;
}

bool
ServerSessionPool::validate_sni(HttpSM *sm, NetVConnection *netvc)
{
  bool retval = true;
  // Verify that the sni name on this connection would match the sni we would have use to create
  // a new connection.
  //
  if (sm->t_state.scheme == URL_WKSIDX_HTTPS) {
    if (auto snis = netvc->get_service<TLSSNISupport>(); snis) {
      const char      *session_sni  = snis->get_sni_server_name();
      std::string_view proposed_sni = sm->get_outbound_sni();
      Dbg(dbg_ctl_http_ss, "validate_sni proposed_sni=%.*s, sni=%s", static_cast<int>(proposed_sni.length()), proposed_sni.data(),
          session_sni);
      if (!session_sni || session_sni[0] == '\0' || proposed_sni.length() == 0) {
        retval = session_sni == nullptr && proposed_sni.length() == 0;
      } else {
        retval = proposed_sni.compare(session_sni) == 0;
      }
    } else {
      retval = false;
    }
  }
  return retval;
}

bool
ServerSessionPool::validate_cert(HttpSM *sm, NetVConnection *netvc)
{
  bool retval = true;
  // Verify that the cert file associated this connection would match the cert file we would have use to create
  // a new connection.
  //
  if (sm->t_state.scheme == URL_WKSIDX_HTTPS) {
    const char      *session_cert  = netvc->options.ssl_client_cert_name;
    std::string_view proposed_cert = sm->get_outbound_cert();
    Dbg(dbg_ctl_http_ss, "validate_cert proposed_cert=%.*s, cert=%s", static_cast<int>(proposed_cert.size()), proposed_cert.data(),
        session_cert);
    if (!session_cert || proposed_cert.length() == 0) {
      retval = session_cert == nullptr && proposed_cert.length() == 0;
    } else {
      retval = proposed_cert.compare(session_cert) == 0;
    }
  }
  return retval;
}

HSMresult_t
ServerSessionPool::acquireSession(sockaddr const *addr, CryptoHash const &hostname_hash,
                                  TSServerSessionSharingMatchMask match_style, HttpSM *sm, PoolableSession *&to_return)
{
  HSMresult_t zret = HSMresult_t::NOT_FOUND;
  to_return        = nullptr;

  if ((TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY & match_style) && !(TS_SERVER_SESSION_SHARING_MATCH_MASK_IP & match_style)) {
    Dbg(dbg_ctl_http_ss, "Search for host name only not IP.  Pool size %zu", m_fqdn_pool.count());
    // This is broken out because only in this case do we check the host hash first. The range must be checked
    // to verify an upstream that matches port and SNI name is selected. Walk backwards to select oldest.
    in_port_t  port  = ats_ip_port_cast(addr);
    auto       range = m_fqdn_pool.equal_range(hostname_hash);
    auto       iter  = std::make_reverse_iterator(range.end());
    auto const end   = std::make_reverse_iterator(range.begin());
    while (iter != end) {
      Dbg(dbg_ctl_http_ss, "Compare port 0x%x against 0x%x", port, ats_ip_port_cast(iter->get_remote_addr()));
      if (port == ats_ip_port_cast(iter->get_remote_addr()) &&
          (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_SNI) || validate_sni(sm, iter->get_netvc())) &&
          (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTSNISYNC) || validate_host_sni(sm, iter->get_netvc())) &&
          (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_CERT) || validate_cert(sm, iter->get_netvc()))) {
        zret = HSMresult_t::DONE;
        break;
      }
      ++iter;
    }
    if (zret == HSMresult_t::DONE) {
      to_return = &*iter;
      if (!to_return->is_multiplexing()) {
        this->removeSession(to_return);
      }
    } else if (iter != end) {
      Dbg(dbg_ctl_http_ss, "Failed find entry due to name mismatch %s", sm->t_state.current.server->name);
    }
  } else if (TS_SERVER_SESSION_SHARING_MATCH_MASK_IP & match_style) { // matching is not disabled.
    auto range = m_ip_pool.equal_range(addr);
    // We want to access the sessions in LIFO order, so start from the back of the list.
    auto       iter = std::make_reverse_iterator(range.end());
    auto const end  = std::make_reverse_iterator(range.begin());
    // The range is all that is needed in the match IP case, otherwise need to scan for matching fqdn
    // And matches the other constraints as well
    // Note the port is matched as part of the address key so it doesn't need to be checked again.
    if (match_style & (~TS_SERVER_SESSION_SHARING_MATCH_MASK_IP)) {
      while (iter != end) {
        if ((!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY) || iter->hostname_hash == hostname_hash) &&
            (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_SNI) || validate_sni(sm, iter->get_netvc())) &&
            (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTSNISYNC) || validate_host_sni(sm, iter->get_netvc())) &&
            (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_CERT) || validate_cert(sm, iter->get_netvc()))) {
          zret = HSMresult_t::DONE;
          break;
        }
        ++iter;
      }
    } else if (iter != end) {
      zret = HSMresult_t::DONE;
    }
    if (zret == HSMresult_t::DONE) {
      to_return = &*iter;
      if (!to_return->is_multiplexing()) {
        this->removeSession(to_return);
      }
    }
  }
  return zret;
}

void
ServerSessionPool::releaseSession(PoolableSession *ss)
{
  ss->state = PoolableSession::PooledState::KA_POOLED;
  // Now we need to issue a read on the connection to detect
  //  if it closes on us.  We will get called back in the
  //  continuation for this bucket, ensuring we have the lock
  //  to remove the connection from our lists
  //  Actually need to have a buffer here, otherwise the vc is
  //  disabled
  ss->do_io_read(this, INT64_MAX, ss->get_remote_reader()->mbuf);

  // Transfer control of the write side as well
  ss->do_io_write(this, 0, nullptr);

  ss->set_inactivity_timeout(ss->get_netvc()->get_inactivity_timeout());
  ss->cancel_active_timeout();

  // put it in the pools.
  this->addSession(ss);

  Dbg(dbg_ctl_http_ss,
      "[%" PRId64 "] [release session] "
      "session placed into shared pool",
      ss->connection_id());
}

//   Called from the NetProcessor to let us know that a
//    connection has closed down
//
int
ServerSessionPool::eventHandler(int event, void *data)
{
  NetVConnection  *net_vc = nullptr;
  PoolableSession *s      = nullptr;

  switch (event) {
  case VC_EVENT_READ_READY:
  // The server sent us data.  This is unexpected so
  //   close the connection
  /* Fall through */
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    net_vc = static_cast<NetVConnection *>((static_cast<VIO *>(data))->vc_server);
    break;

  default:
    ink_release_assert(0);
    return 0;
  }

  sockaddr const *addr  = net_vc->get_remote_addr();
  bool            found = false;

  for (auto spot = m_ip_pool.find(addr); spot != m_ip_pool.end() && spot->_ip_link.equal(addr, spot); ++spot) {
    if ((s = spot)->get_netvc() == net_vc) {
      // if there was a timeout of some kind on a keep alive connection, and
      // keeping the connection alive will not keep us above the # of max connections
      // to the origin and we are below the min number of keep alive connections to this
      // origin, then reset the timeouts on our end and do not close the connection
      if ((event == VC_EVENT_INACTIVITY_TIMEOUT || event == VC_EVENT_ACTIVE_TIMEOUT) &&
          s->state == PoolableSession::PooledState::KA_POOLED && s->conn_track_group) {
        Dbg(dbg_ctl_http_ss, "s->conn_track_group->min_keep_alive_conns : %d", s->conn_track_group->_min_keep_alive_conns);
        bool connection_count_below_min = s->conn_track_group->_count <= s->conn_track_group->_min_keep_alive_conns;

        if (connection_count_below_min) {
          Dbg(dbg_ctl_http_ss,
              "[%" PRId64 "] [session_bucket] session received io notice [%s], "
              "resetting timeout to maintain minimum number of connections",
              s->connection_id(), HttpDebugNames::get_event_name(event));
          s->get_netvc()->set_inactivity_timeout(s->get_netvc()->get_inactivity_timeout());
          found = true;
          break;
        }
      }

      // We've found our server session. Remove it from
      //   our lists and close it down
      Dbg(dbg_ctl_http_ss, "[%" PRId64 "] [session_pool] session %p received io notice [%s]", s->connection_id(), s,
          HttpDebugNames::get_event_name(event));
      ink_assert(s->state == PoolableSession::PooledState::KA_POOLED);
      // Out of the pool! Now!
      this->removeSession(s);
      // Drop connection on this end.
      s->do_io_close();
      found = true;
      break;
    }
  }

  if (!found) {
    // We failed to find our session.  This can only be the result of a programming flaw. Since we only ever keep
    // UnixNetVConnections and SSLNetVConnections in the session pool, the dynamic cast won't fail.
    UnixNetVConnection *unix_net_vc = dynamic_cast<UnixNetVConnection *>(net_vc);
    if (unix_net_vc) {
      char peer_ip[INET6_ADDRPORTSTRLEN];
      ats_ip_nptop(unix_net_vc->get_remote_addr(), peer_ip, sizeof(peer_ip));

      Warning("Connection leak from http keep-alive system fd=%d closed=%d peer_ip_port=%s", unix_net_vc->get_fd(),
              unix_net_vc->closed, peer_ip);
    }
    ink_assert(0);
  }
  return 0;
}

void
HttpSessionManager::init()
{
  m_g_pool = new ServerSessionPool;
  eventProcessor.schedule_spawn(&initialize_thread_for_http_sessions, ET_NET);
}

// TODO: Should this really purge all keep-alive sessions?
// Does this make any sense, since we always do the global pool and not the per thread?
void
HttpSessionManager::purge_keepalives()
{
  EThread *ethread = this_ethread();

  MUTEX_TRY_LOCK(lock, m_g_pool->mutex, ethread);
  if (lock.is_locked()) {
    m_g_pool->purge();
  } // should we do something clever if we don't get the lock?
}

HSMresult_t
HttpSessionManager::acquire_session(HttpSM *sm, sockaddr const *ip, const char *hostname, ProxyTransaction *ua_txn)
{
  PoolableSession                *to_return = nullptr;
  TSServerSessionSharingMatchMask match_style =
    static_cast<TSServerSessionSharingMatchMask>(sm->t_state.txn_conf->server_session_sharing_match);
  CryptoHash  hostname_hash;
  HSMresult_t retval = HSMresult_t::NOT_FOUND;

  CryptoContext().hash_immediate(hostname_hash, (unsigned char *)hostname, strlen(hostname));

  // First check to see if there is a server session bound
  //   to the user agent session
  to_return = ua_txn->get_server_session();
  if (to_return != nullptr) {
    ua_txn->attach_server_session(nullptr);

    // Since the client session is reusing the same server session, it seems that the SNI should match
    // Will the client make requests to different hosts over the same SSL session? Though checking
    // the IP/hostname here seems a bit redundant too
    //
    if (ServerSessionPool::match(to_return, ip, hostname_hash, match_style) &&
        (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_SNI) ||
         ServerSessionPool::validate_sni(sm, to_return->get_netvc())) &&
        (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTSNISYNC) ||
         ServerSessionPool::validate_host_sni(sm, to_return->get_netvc())) &&
        (!(match_style & TS_SERVER_SESSION_SHARING_MATCH_MASK_CERT) ||
         ServerSessionPool::validate_cert(sm, to_return->get_netvc()))) {
      Dbg(dbg_ctl_http_ss, "[%" PRId64 "] [acquire session] returning attached session ", to_return->connection_id());
      to_return->state = PoolableSession::PooledState::SSN_IN_USE;
      sm->create_server_txn(to_return);
      return HSMresult_t::DONE;
    }
    // Release this session back to the main session pool and
    //   then continue looking for one from the shared pool
    Dbg(dbg_ctl_http_ss,
        "[%" PRId64 "] [acquire session] "
        "session not a match, returning to shared pool",
        to_return->connection_id());
    to_return->release(nullptr);
    to_return = nullptr;
  }

  // Otherwise, check the thread pool first
  if (this->get_pool_type() == TS_SERVER_SESSION_SHARING_POOL_THREAD ||
      this->get_pool_type() == TS_SERVER_SESSION_SHARING_POOL_HYBRID) {
    retval = _acquire_session(ip, hostname_hash, sm, match_style, TS_SERVER_SESSION_SHARING_POOL_THREAD);
  }

  //  If you didn't get a match, and the global pool is an option go there.
  if (retval != HSMresult_t::DONE) {
    if (TS_SERVER_SESSION_SHARING_POOL_GLOBAL == this->get_pool_type() ||
        TS_SERVER_SESSION_SHARING_POOL_HYBRID == this->get_pool_type()) {
      retval = _acquire_session(ip, hostname_hash, sm, match_style, TS_SERVER_SESSION_SHARING_POOL_GLOBAL);
    } else if (TS_SERVER_SESSION_SHARING_POOL_GLOBAL_LOCKED == this->get_pool_type())
      retval = _acquire_session(ip, hostname_hash, sm, match_style, TS_SERVER_SESSION_SHARING_POOL_GLOBAL_LOCKED);
  }

  return retval;
}

namespace
{

// Scoped lock of the session pool based on pool type
// (global_locked vs everything else)
inline bool
lockSessionPool(Ptr<ProxyMutex> &mutex, EThread *const ethread, TSServerSessionSharingPoolType const pool_type,
                MutexLock *const mlock, MutexTryLock *const tlock)
{
  bool locked = false;
  if (TS_SERVER_SESSION_SHARING_POOL_GLOBAL_LOCKED == pool_type) {
    SCOPED_MUTEX_LOCK(lock, mutex, ethread);
    *mlock = std::move(lock);
    locked = true;
  } else {
    MUTEX_TRY_LOCK(lock, mutex, ethread);
    *tlock = std::move(lock);
    locked = tlock->is_locked();
  }
  return locked;
}

} // namespace

HSMresult_t
HttpSessionManager::_acquire_session(sockaddr const *ip, CryptoHash const &hostname_hash, HttpSM *sm,
                                     TSServerSessionSharingMatchMask match_style, TSServerSessionSharingPoolType pool_type)
{
  PoolableSession *to_return = nullptr;
  HSMresult_t      retval    = HSMresult_t::NOT_FOUND;

  // Extend the mutex window until the acquired Server session is attached
  // to the SM. Releasing the mutex before that results in race conditions
  // due to a potential parallel network read on the VC with no mutex guarding
  {
    // Now check to see if we have a connection in our shared connection pool
    EThread        *ethread = this_ethread();
    Ptr<ProxyMutex> pool_mutex =
      (TS_SERVER_SESSION_SHARING_POOL_THREAD == pool_type) ? ethread->server_session_pool->mutex : m_g_pool->mutex;

    MutexLock    mlock;
    MutexTryLock tlock;
    bool const   locked = lockSessionPool(pool_mutex, ethread, pool_type, &mlock, &tlock);

    if (locked) {
      if (TS_SERVER_SESSION_SHARING_POOL_THREAD == pool_type) {
        retval = ethread->server_session_pool->acquireSession(ip, hostname_hash, match_style, sm, to_return);
        Dbg(dbg_ctl_http_ss, "[acquire session] thread pool search %s", to_return ? "successful" : "failed");
      } else {
        retval = m_g_pool->acquireSession(ip, hostname_hash, match_style, sm, to_return);
        Dbg(dbg_ctl_http_ss, "[acquire session] global pool search %s", to_return ? "successful" : "failed");
        // At this point to_return has been removed from the pool. Do we need to move it
        // to the same thread?
        if (to_return) {
          UnixNetVConnection *server_vc = dynamic_cast<UnixNetVConnection *>(to_return->get_netvc());
          if (server_vc) {
            // Disable i/o on this vc now, but, hold onto the g_pool cont
            // and the mutex to stop any stray events from getting in
            server_vc->do_io_read(m_g_pool, 0, nullptr);
            server_vc->do_io_write(m_g_pool, 0, nullptr);
            UnixNetVConnection *new_vc = server_vc->migrateToCurrentThread(sm, ethread);
            // The VC moved, free up the original one
            if (new_vc != server_vc) {
              ink_assert(new_vc == nullptr || new_vc->nh != nullptr);
              if (!new_vc) {
                // Close out to_return, we were't able to get a connection
                Metrics::Counter::increment(http_rsb.origin_shutdown_migration_failure);
                to_return->do_io_close();
                to_return = nullptr;
                retval    = HSMresult_t::NOT_FOUND;
              } else {
                // Keep things from timing out on us
                new_vc->set_inactivity_timeout(new_vc->get_inactivity_timeout());
                to_return->set_netvc(new_vc);
              }
            } else {
              // Keep things from timing out on us
              server_vc->set_inactivity_timeout(server_vc->get_inactivity_timeout());
            }
          }
        }
      }
    } else { // Didn't get the lock.  to_return is still NULL
      retval = HSMresult_t::RETRY;
    }

    if (to_return) {
      if (sm->create_server_txn(to_return)) {
        Dbg(dbg_ctl_http_ss, "[%" PRId64 "] [acquire session] return session from shared pool", to_return->connection_id());
        to_return->state = PoolableSession::PooledState::SSN_IN_USE;
        retval           = HSMresult_t::DONE;
      } else {
        Dbg(dbg_ctl_http_ss, "[%" PRId64 "] [acquire session] failed to get transaction on session from shared pool",
            to_return->connection_id());
        // Don't close the H2 origin.  Otherwise you get use-after free with the activity timeout cop
        if (!to_return->is_multiplexing()) {
          to_return->do_io_close();
        }
        retval = HSMresult_t::RETRY;
      }
    }
  }

  return retval;
}

HSMresult_t
HttpSessionManager::release_session(PoolableSession *to_release)
{
  EThread           *ethread = this_ethread();
  ServerSessionPool *pool =
    TS_SERVER_SESSION_SHARING_POOL_THREAD == to_release->sharing_pool ? ethread->server_session_pool : m_g_pool;
  bool released_p = true;

  // The per thread lock looks like it should not be needed but if it's not locked the close checking I/O op will crash.

  {
    MutexLock    mlock;
    MutexTryLock tlock;
    bool const   locked = lockSessionPool(pool->mutex, ethread, this->get_pool_type(), &mlock, &tlock);

    if (locked) {
      pool->releaseSession(to_release);
    } else if (this->get_pool_type() == TS_SERVER_SESSION_SHARING_POOL_HYBRID) {
      // Try again with the thread pool
      to_release->sharing_pool = TS_SERVER_SESSION_SHARING_POOL_THREAD;
      return release_session(to_release);
    } else {
      Dbg(dbg_ctl_http_ss, "[%" PRId64 "] [release session] could not release session due to lock contention",
          to_release->connection_id());
      released_p = false;
    }
  }

  return released_p ? HSMresult_t::DONE : HSMresult_t::RETRY;
}

void
ServerSessionPool::removeSession(PoolableSession *to_remove)
{
  EThread *ethread = this_ethread();
  SCOPED_MUTEX_LOCK(lock, mutex, ethread);
  char peer_ip[INET6_ADDRPORTSTRLEN];
  if (dbg_ctl_http_ss.on()) {
    ats_ip_nptop(to_remove->get_remote_addr(), peer_ip, sizeof(peer_ip));
    Dbg(dbg_ctl_http_ss, "Remove session %p %s m_fqdn_pool size=%zu m_ip_pool_size=%zu", to_remove, peer_ip, m_fqdn_pool.count(),
        m_ip_pool.count());
  }
  m_fqdn_pool.erase(to_remove);
  m_ip_pool.erase(to_remove);
  if (dbg_ctl_http_ss.on()) {
    Dbg(dbg_ctl_http_ss, "After Remove session %p m_fqdn_pool size=%zu m_ip_pool_size=%zu", to_remove, m_fqdn_pool.count(),
        m_ip_pool.count());
  }

  Metrics::Gauge::decrement(http_rsb.pooled_server_connections);
}

void
ServerSessionPool::addSession(PoolableSession *ss)
{
  EThread *ethread = this_ethread();
  SCOPED_MUTEX_LOCK(lock, mutex, ethread);
  // put it in the pools.
  m_ip_pool.insert(ss);
  m_fqdn_pool.insert(ss);

  if (dbg_ctl_http_ss.on()) {
    char peer_ip[INET6_ADDRPORTSTRLEN];
    ats_ip_nptop(ss->get_remote_addr(), peer_ip, sizeof(peer_ip));
    Dbg(dbg_ctl_http_ss, "[%" PRId64 "] [add session] session placed into shared pool under ip %s", ss->connection_id(), peer_ip);
  }

  Metrics::Gauge::increment(http_rsb.pooled_server_connections);
}
