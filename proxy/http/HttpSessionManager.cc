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

#include "HttpSessionManager.h"
#include "../ProxySession.h"
#include "Http1ServerSession.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"

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
  m_ip_pool.apply([](Http1ServerSession *ssn) -> void { ssn->do_io_close(); });
  m_ip_pool.clear();
  m_fqdn_pool.clear();
}

bool
ServerSessionPool::match(Http1ServerSession *ss, sockaddr const *addr, CryptoHash const &hostname_hash,
                         TSServerSessionSharingMatchType match_style)
{
  return TS_SERVER_SESSION_SHARING_MATCH_NONE !=
           match_style && // if no matching allowed, fail immediately.
                          // The hostname matches if we're not checking it or it (and the port!) is a match.
         (TS_SERVER_SESSION_SHARING_MATCH_IP == match_style ||
          (ats_ip_port_cast(addr) == ats_ip_port_cast(ss->get_server_ip()) && ss->hostname_hash == hostname_hash)) &&
         // The IP address matches if we're not checking it or it is a match.
         (TS_SERVER_SESSION_SHARING_MATCH_HOST == match_style || ats_ip_addr_port_eq(ss->get_server_ip(), addr));
}

bool
ServerSessionPool::validate_sni(HttpSM *sm, NetVConnection *netvc)
{
  // TS-4468: If the connection matches, make sure the SNI server
  // name (if present) matches the request hostname
  int len              = 0;
  const char *req_host = sm->t_state.hdr_info.server_request.host_get(&len);
  // The sni_servername of the connection was set on HttpSM::do_http_server_open
  // by fetching the hostname from the server request.  So the connection should only
  // be reused if the hostname in the new request is the same as the host name in the
  // original request
  const char *session_sni = netvc->options.sni_servername;

  return ((sm->t_state.scheme != URL_WKSIDX_HTTPS) || !session_sni || strncasecmp(session_sni, req_host, len) == 0);
}

HSMresult_t
ServerSessionPool::acquireSession(sockaddr const *addr, CryptoHash const &hostname_hash,
                                  TSServerSessionSharingMatchType match_style, HttpSM *sm, Http1ServerSession *&to_return)
{
  HSMresult_t zret = HSM_NOT_FOUND;
  to_return        = nullptr;

  if (TS_SERVER_SESSION_SHARING_MATCH_HOST == match_style) {
    // This is broken out because only in this case do we check the host hash first. The range must be checked
    // to verify an upstream that matches port and SNI name is selected. Walk backwards to select oldest.
    in_port_t port = ats_ip_port_cast(addr);
    FQDNTable::iterator first, last;
    // FreeBSD/clang++ bug workaround: explicit cast to super type to make overload work. Not needed on Fedora27 nor gcc.
    // Not fixed on FreeBSD as of llvm 6.0.1.
    std::tie(first, last) = static_cast<const decltype(m_fqdn_pool)::range::super_type &>(m_fqdn_pool.equal_range(hostname_hash));
    while (last != first) {
      --last;
      if (port == ats_ip_port_cast(last->get_server_ip()) && validate_sni(sm, last->get_netvc())) {
        zret = HSM_DONE;
        break;
      }
    }
    if (zret == HSM_DONE) {
      to_return = last;
      m_fqdn_pool.erase(last);
      m_ip_pool.erase(to_return);
    }
  } else if (TS_SERVER_SESSION_SHARING_MATCH_NONE != match_style) { // matching is not disabled.
    IPTable::iterator first, last;
    // FreeBSD/clang++ bug workaround: explicit cast to super type to make overload work. Not needed on Fedora27 nor gcc.
    // Not fixed on FreeBSD as of llvm 6.0.1.
    std::tie(first, last) = static_cast<const decltype(m_ip_pool)::range::super_type &>(m_ip_pool.equal_range(addr));
    // The range is all that is needed in the match IP case, otherwise need to scan for matching fqdn.
    // Note the port is matched as part of the address key so it doesn't need to be checked again.
    if (TS_SERVER_SESSION_SHARING_MATCH_IP != match_style) {
      while (last != first) {
        --last;
        if (last->hostname_hash == hostname_hash && validate_sni(sm, last->get_netvc())) {
          zret = HSM_DONE;
          break;
        }
      }
    }
    if (zret == HSM_DONE) {
      to_return = last;
      m_ip_pool.erase(last);
      m_fqdn_pool.erase(to_return);
    }
  }
  return zret;
}

void
ServerSessionPool::releaseSession(Http1ServerSession *ss)
{
  ss->state = HSS_KA_SHARED;
  // Now we need to issue a read on the connection to detect
  //  if it closes on us.  We will get called back in the
  //  continuation for this bucket, ensuring we have the lock
  //  to remove the connection from our lists
  ss->do_io_read(this, INT64_MAX, ss->read_buffer);

  // Transfer control of the write side as well
  ss->do_io_write(this, 0, nullptr);

  // we probably don't need the active timeout set, but will leave it for now
  ss->get_netvc()->set_inactivity_timeout(ss->get_netvc()->get_inactivity_timeout());
  ss->get_netvc()->set_active_timeout(ss->get_netvc()->get_active_timeout());
  // put it in the pools.
  m_ip_pool.insert(ss);
  m_fqdn_pool.insert(ss);

  Debug("http_ss",
        "[%" PRId64 "] [release session] "
        "session placed into shared pool",
        ss->con_id);
}

//   Called from the NetProcessor to let us know that a
//    connection has closed down
//
int
ServerSessionPool::eventHandler(int event, void *data)
{
  NetVConnection *net_vc = nullptr;
  Http1ServerSession *s  = nullptr;

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

  sockaddr const *addr                 = net_vc->get_remote_addr();
  HttpConfigParams *http_config_params = HttpConfig::acquire();
  bool found                           = false;

  for (auto spot = m_ip_pool.find(addr); spot != m_ip_pool.end() && spot->_ip_link.equal(addr, spot); ++spot) {
    if ((s = spot)->get_netvc() == net_vc) {
      // if there was a timeout of some kind on a keep alive connection, and
      // keeping the connection alive will not keep us above the # of max connections
      // to the origin and we are below the min number of keep alive connections to this
      // origin, then reset the timeouts on our end and do not close the connection
      if ((event == VC_EVENT_INACTIVITY_TIMEOUT || event == VC_EVENT_ACTIVE_TIMEOUT) && s->state == HSS_KA_SHARED &&
          s->conn_track_group) {
        bool connection_count_below_min = s->conn_track_group->_count <= http_config_params->origin_min_keep_alive_connections;

        if (connection_count_below_min) {
          Debug("http_ss",
                "[%" PRId64 "] [session_bucket] session received io notice [%s], "
                "resetting timeout to maintain minimum number of connections",
                s->con_id, HttpDebugNames::get_event_name(event));
          s->get_netvc()->set_inactivity_timeout(s->get_netvc()->get_inactivity_timeout());
          s->get_netvc()->set_active_timeout(s->get_netvc()->get_active_timeout());
          found = true;
          break;
        }
      }

      // We've found our server session. Remove it from
      //   our lists and close it down
      Debug("http_ss", "[%" PRId64 "] [session_pool] session %p received io notice [%s]", s->con_id, s,
            HttpDebugNames::get_event_name(event));
      ink_assert(s->state == HSS_KA_SHARED);
      // Out of the pool! Now!
      m_ip_pool.erase(spot);
      m_fqdn_pool.erase(s);
      // Drop connection on this end.
      s->do_io_close();
      found = true;
      break;
    }
  }

  HttpConfig::release(http_config_params);
  if (!found) {
    // We failed to find our session.  This can only be the result of a programming flaw. Since we only ever keep
    // UnixNetVConnections and SSLNetVConnections in the session pool, the dynamic cast won't fail.
    UnixNetVConnection *unix_net_vc = dynamic_cast<UnixNetVConnection *>(net_vc);
    if (unix_net_vc) {
      char peer_ip[INET6_ADDRPORTSTRLEN];
      ats_ip_nptop(unix_net_vc->get_remote_addr(), peer_ip, sizeof(peer_ip));

      Warning("Connection leak from http keep-alive system fd=%d closed=%d peer_ip_port=%s", unix_net_vc->con.fd,
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
HttpSessionManager::acquire_session(Continuation * /* cont ATS_UNUSED */, sockaddr const *ip, const char *hostname,
                                    ProxyTransaction *ua_txn, HttpSM *sm)
{
  Http1ServerSession *to_return = nullptr;
  TSServerSessionSharingMatchType match_style =
    static_cast<TSServerSessionSharingMatchType>(sm->t_state.txn_conf->server_session_sharing_match);
  CryptoHash hostname_hash;
  HSMresult_t retval = HSM_NOT_FOUND;

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
        ServerSessionPool::validate_sni(sm, to_return->get_netvc())) {
      Debug("http_ss", "[%" PRId64 "] [acquire session] returning attached session ", to_return->con_id);
      to_return->state = HSS_ACTIVE;
      sm->attach_server_session(to_return);
      return HSM_DONE;
    }
    // Release this session back to the main session pool and
    //   then continue looking for one from the shared pool
    Debug("http_ss",
          "[%" PRId64 "] [acquire session] "
          "session not a match, returning to shared pool",
          to_return->con_id);
    to_return->release();
    to_return = nullptr;
  }

  // TS-3797 Adding another scope so the pool lock is dropped after it is removed from the pool and
  // potentially moved to the current thread.  At the end of this scope, either the original
  // pool selected VC is on the current thread or its content has been moved to a new VC on the
  // current thread and the original has been deleted. This should adequately cover TS-3266 so we
  // don't have to continue to hold the pool thread while we initialize the server session in the
  // client session
  {
    // Now check to see if we have a connection in our shared connection pool
    EThread *ethread       = this_ethread();
    ProxyMutex *pool_mutex = (TS_SERVER_SESSION_SHARING_POOL_THREAD == sm->t_state.http_config_param->server_session_sharing_pool) ?
                               ethread->server_session_pool->mutex.get() :
                               m_g_pool->mutex.get();
    MUTEX_TRY_LOCK(lock, pool_mutex, ethread);
    if (lock.is_locked()) {
      if (TS_SERVER_SESSION_SHARING_POOL_THREAD == sm->t_state.http_config_param->server_session_sharing_pool) {
        retval = ethread->server_session_pool->acquireSession(ip, hostname_hash, match_style, sm, to_return);
        Debug("http_ss", "[acquire session] thread pool search %s", to_return ? "successful" : "failed");
      } else {
        retval = m_g_pool->acquireSession(ip, hostname_hash, match_style, sm, to_return);
        Debug("http_ss", "[acquire session] global pool search %s", to_return ? "successful" : "failed");
        // At this point to_return has been removed from the pool. Do we need to move it
        // to the same thread?
        if (to_return) {
          UnixNetVConnection *server_vc = dynamic_cast<UnixNetVConnection *>(to_return->get_netvc());
          if (server_vc) {
            UnixNetVConnection *new_vc = server_vc->migrateToCurrentThread(sm, ethread);
            if (new_vc->thread != ethread) {
              // Failed to migrate, put it back to global session pool
              m_g_pool->releaseSession(to_return);
              to_return = nullptr;
              retval    = HSM_NOT_FOUND;
            } else if (new_vc != server_vc) {
              // The VC migrated, keep things from timing out on us
              new_vc->set_inactivity_timeout(new_vc->get_inactivity_timeout());
              to_return->set_netvc(new_vc);
            } else {
              // The VC moved, keep things from timing out on us
              server_vc->set_inactivity_timeout(server_vc->get_inactivity_timeout());
            }
          }
        }
      }
    } else { // Didn't get the lock.  to_return is still NULL
      retval = HSM_RETRY;
    }
  }

  if (to_return) {
    Debug("http_ss", "[%" PRId64 "] [acquire session] return session from shared pool", to_return->con_id);
    to_return->state = HSS_ACTIVE;
    // the attach_server_session will issue the do_io_read under the sm lock
    sm->attach_server_session(to_return);
    retval = HSM_DONE;
  }
  return retval;
}

HSMresult_t
HttpSessionManager::release_session(Http1ServerSession *to_release)
{
  EThread *ethread = this_ethread();
  ServerSessionPool *pool =
    TS_SERVER_SESSION_SHARING_POOL_THREAD == to_release->sharing_pool ? ethread->server_session_pool : m_g_pool;
  bool released_p = true;

  // The per thread lock looks like it should not be needed but if it's not locked the close checking I/O op will crash.
  MUTEX_TRY_LOCK(lock, pool->mutex, ethread);
  if (lock.is_locked()) {
    pool->releaseSession(to_release);
  } else {
    Debug("http_ss", "[%" PRId64 "] [release session] could not release session due to lock contention", to_release->con_id);
    released_p = false;
  }

  return released_p ? HSM_DONE : HSM_RETRY;
}
