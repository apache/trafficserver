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
#include "../ProxyClientSession.h"
#include "HttpServerSession.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"
#include "tscore/Allocator.h"

ClassAllocator<ServerSessionPoolCont> serverSessionPoolContAllocator("ServerSessionPoolContAllocator");

void
ServerSessionPoolCont::init(Continuation *cont, HttpServerSession *s)
{
  m_action  = cont;
  mutex     = cont->mutex;
  m_session = s;

  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  ink_assert(lock.is_locked());
}

void
ServerSessionPoolCont::init(Continuation *cont, uint32 ip_key, NetVConnection *net_vc, int event)
{
  m_action = cont;
  mutex    = cont->mutex;
  m_ip_key = ip_key;
  m_net_vc = net_vc;
  m_event  = event;

  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  ink_assert(lock.is_locked());
}

int
ServerSessionPoolCont::removeFQDNEntry(int /* event ATS_UNUSED */, Event *e)
{
  EThread *t = e ? e->ethread : this_ethread();
  MUTEX_TRY_LOCK(lock, m_action.mutex, t);

  if (!lock.is_locked()) {
    mutex->thread_holding->schedule_in(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  }
  if (m_action.cancelled) {
    destroy();
    return EVENT_DONE;
  }

  Ptr<ProxyMutex> fqdn_lock = httpSessionManager.get_pool()->m_fqdn_pool.lock_for_key(m_session->hostname_hash.fold());
  MUTEX_TRY_LOCK(host_lock, fqdn_lock, this_ethread());
  if (host_lock.is_locked()) {
    httpSessionManager.get_pool()->m_fqdn_pool.remove_entry(m_session->hostname_hash.fold(), m_session);
    // Drop connection on this end.
    m_session->do_io_close();
    destroy();
    return EVENT_DONE;
  } else {
    mutex->thread_holding->schedule_in(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  }
}

int
ServerSessionPoolCont::removeIPAndFQDNEntry(int /* event ATS_UNUSED */, Event *e)
{
  EThread *t = e ? e->ethread : this_ethread();
  MUTEX_TRY_LOCK(lock, m_action.mutex, t);
  if (!lock.is_locked()) {
    mutex->thread_holding->schedule_in(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  }
  if (m_action.cancelled) {
    destroy();
    return EVENT_DONE;
  }

  if (!httpSessionManager.get_pool()->removeSession(m_ip_key, m_net_vc, m_event)) {
    mutex->thread_holding->schedule_in(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  } else {
    destroy();
    return EVENT_DONE;
  }
}

void
ServerSessionPoolCont::destroy()
{
  m_action  = nullptr;
  mutex     = nullptr;
  m_session = nullptr;
  m_ip_key  = 0;
  m_net_vc  = nullptr;
  m_event   = 0;

  serverSessionPoolContAllocator.free(this);
}

// Initialize a thread to handle HTTP session management
void
initialize_thread_for_http_sessions(EThread *thread)
{
  thread->server_session_pool = new ServerSessionPool;
}

HttpSessionManager httpSessionManager;

ServerSessionPool::ServerSessionPool()
  : Continuation(new_ProxyMutex()), m_ip_pool(1023, false, NULL, NULL), m_fqdn_pool(1023, false, NULL, NULL)
{
  SET_HANDLER(&ServerSessionPool::eventHandler);
}

void
ServerSessionPool::purge()
{
  // @c do_io_close can free the instance which clears the intrusive links and breaks the iterator.
  // Therefore @c do_io_close is called on a post-incremented iterator.
  HttpServerSession *ssn;
  IPTable_iter ip_iter;
  for (int part = 0; part < MT_HASHTABLE_PARTITIONS; ++part) {
    Ptr<ProxyMutex> iptable_lock = m_ip_pool.lock_for_partitions(part);
    MUTEX_TRY_LOCK(ip_lock, iptable_lock, this_ethread());
    if (ip_lock.is_locked()) {
      ssn = m_ip_pool.first_entry(part, &ip_iter);
      while (ssn) {
        m_ip_pool.remove_entry_by_iter(part, &ip_iter);
        uint64_t hostname_key     = ssn->hostname_hash.fold();
        Ptr<ProxyMutex> fqdn_lock = m_fqdn_pool.lock_for_key(hostname_key);
        SCOPED_MUTEX_LOCK(lock, fqdn_lock, this_ethread());
        m_fqdn_pool.remove_entry(hostname_key, ssn);
        ssn->do_io_close();
      }
    }
  }
}

bool
ServerSessionPool::match(HttpServerSession *ss, sockaddr const *addr, CryptoHash const &hostname_hash,
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

uint32_t
ServerSessionPool::ip_hash_of(sockaddr const *addr)
{
  return ats_ip_hash(addr);
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
                                  TSServerSessionSharingMatchType match_style, HttpSM *sm, HttpServerSession *&to_return)
{
  HSMresult_t zret = HSM_NOT_FOUND;
  to_return        = nullptr;

  if (TS_SERVER_SESSION_SHARING_MATCH_HOST == match_style) {
    // This is broken out because only in this case do we check the host hash first. The range must be checked
    // to verify an upstream that matches port and SNI name is selected. Walk backwards to select oldest.
    in_port_t port = ats_ip_port_cast(addr);
    FqdnTable_iter fqdn_iter;
    HttpServerSession *p_fqdn_Entry;
    p_fqdn_Entry = m_fqdn_pool.first_entry_in_bucket(hostname_hash.fold(), &fqdn_iter);
    while (p_fqdn_Entry) {
      if (p_fqdn_Entry->hostname_hash == hostname_hash) {
        if (p_fqdn_Entry->get_netvc() && port == ats_ip_port_cast(p_fqdn_Entry->get_server_ip()) &&
            validate_sni(sm, p_fqdn_Entry->get_netvc())) {
          zret = HSM_DONE;
          break;
        }
      }
      p_fqdn_Entry = m_fqdn_pool.next_entry_in_bucket(hostname_hash.fold(), &fqdn_iter);
    }

    if (zret == HSM_DONE) {
      to_return = p_fqdn_Entry;
      m_fqdn_pool.remove_entry(hostname_hash.fold(), &fqdn_iter);
      m_ip_pool.remove_entry(ip_hash_of(addr), to_return);
    }
  } else if (TS_SERVER_SESSION_SHARING_MATCH_NONE != match_style) { // matching is not disabled.
    IPTable_iter ip_iter;
    HttpServerSession *p_Entry;
    auto ip_key = ip_hash_of(addr);
    p_Entry     = m_ip_pool.first_entry_in_bucket(ip_key, &ip_iter);
    if (TS_SERVER_SESSION_SHARING_MATCH_IP != match_style) {
      while (p_Entry) {
        if (p_Entry->hostname_hash == hostname_hash && validate_sni(sm, p_Entry->get_netvc())) {
          zret = HSM_DONE;
          break;
        }
        p_Entry = m_ip_pool.next_entry_in_bucket(ip_key, &ip_iter);
      }
    }

    if (zret == HSM_DONE) {
      to_return = p_Entry;
      m_ip_pool.remove_entry(ip_key, &ip_iter);
      m_fqdn_pool.remove_entry(hostname_hash.fold(), to_return);
    }
  }
  return zret;
}

void
ServerSessionPool::releaseSession(HttpServerSession *ss)
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
  m_ip_pool.insert_entry(ip_hash_of(&(ss->get_server_ip().sa)), ss);
  m_fqdn_pool.insert_entry(ss->hostname_hash.fold(), ss);

  Debug("http_ss",
        "[%" PRId64 "] [release session] "
        "session placed into shared pool",
        ss->con_id);
}

bool
ServerSessionPool::removeSession(uint32 ip_key, NetVConnection *net_vc, int event)
{
  bool done                            = false;
  bool found                           = false;
  HttpConfigParams *http_config_params = HttpConfig::acquire();
  Ptr<ProxyMutex> iptable_lock         = m_ip_pool.lock_for_key(ip_key);
  MUTEX_TRY_LOCK(ip_lock, iptable_lock, this_ethread());
  if (ip_lock.is_locked()) {
    IPTable_iter ip_iter;
    HttpServerSession *s = m_ip_pool.first_entry_in_bucket(ip_key, &ip_iter);
    while (s) {
      if (s->get_netvc() == net_vc) {
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
        m_ip_pool.remove_entry(ip_key, &ip_iter);

        Ptr<ProxyMutex> fqdn_lock = m_fqdn_pool.lock_for_key(s->hostname_hash.fold());
        MUTEX_TRY_LOCK(host_lock, fqdn_lock, this_ethread());
        if (host_lock.is_locked()) {
          m_fqdn_pool.remove_entry(s->hostname_hash.fold(), s);
          // Drop connection on this end.
          s->do_io_close();
        } else {
          ServerSessionPoolCont *cont = serverSessionPoolContAllocator.alloc();
          cont->init(this, s);
          SET_CONTINUATION_HANDLER(cont, (ServerSessionPoolContHandler)&ServerSessionPoolCont::removeFQDNEntry);
          this_ethread()->schedule_in(cont, MUTEX_RETRY_DELAY);
        }
        found = true;
        break;
      }
      s = m_ip_pool.next_entry_in_bucket(ip_key, &ip_iter);
    }
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
    done = true;
  } else {
    done = false;
  }

  HttpConfig::release(http_config_params);
  return done;
}

//   Called from the NetProcessor to let us know that a
//    connection has closed down
//
int
ServerSessionPool::eventHandler(int event, void *data)
{
  NetVConnection *net_vc = nullptr;

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

  sockaddr const *addr = net_vc->get_remote_addr();
  uint32 ip_key        = ip_hash_of(addr);
  if (!removeSession(ip_key, net_vc, event)) {
    ServerSessionPoolCont *cont = serverSessionPoolContAllocator.alloc();
    cont->init(this, ip_key, net_vc, event);
    SET_CONTINUATION_HANDLER(cont, (ServerSessionPoolContHandler)&ServerSessionPoolCont::removeIPAndFQDNEntry);
    this_ethread()->schedule_in(cont, MUTEX_RETRY_DELAY);
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
                                    ProxyClientTransaction *ua_txn, HttpSM *sm)
{
  HttpServerSession *to_return = nullptr;
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
  //{
  // Now check to see if we have a connection in our shared connection pool
  EThread *ethread = this_ethread();
  if (TS_SERVER_SESSION_SHARING_POOL_THREAD == sm->t_state.http_config_param->server_session_sharing_pool) {
    MUTEX_TRY_LOCK(lock, ethread->server_session_pool->mutex, ethread);
    if (lock.is_locked()) {
      retval = ethread->server_session_pool->acquireSession(ip, hostname_hash, match_style, sm, to_return);
      Debug("http_ss", "[acquire session] thread pool search %s", to_return ? "successful" : "failed");
    } else {
      retval = HSM_RETRY;
    }
  } else {
    Ptr<ProxyMutex> iptable_lock = m_g_pool->m_ip_pool.lock_for_key(m_g_pool->ip_hash_of(ip));
    Ptr<ProxyMutex> fqdn_lock    = m_g_pool->m_fqdn_pool.lock_for_key(hostname_hash.fold());
    MUTEX_TRY_LOCK(ip_lock, iptable_lock, this_ethread());
    MUTEX_TRY_LOCK(host_lock, fqdn_lock, this_ethread());
    if (ip_lock.is_locked() && host_lock.is_locked()) {
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
    } else {
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
HttpSessionManager::release_session(HttpServerSession *to_release)
{
  EThread *ethread = this_ethread();
  ServerSessionPool *pool =
    TS_SERVER_SESSION_SHARING_POOL_THREAD == to_release->sharing_pool ? ethread->server_session_pool : m_g_pool;
  bool released_p = true;
  if (TS_SERVER_SESSION_SHARING_POOL_THREAD == to_release->sharing_pool) {
    // The per thread lock looks like it should not be needed but if it's not locked the close checking I/O op will crash.
    MUTEX_TRY_LOCK(lock, pool->mutex, ethread);
    if (lock.is_locked()) {
      pool->releaseSession(to_release);
    } else {
      Debug("http_ss", "[%" PRId64 "] [release session] could not release session due to lock contention", to_release->con_id);
      released_p = false;
    }
  } else {
    Ptr<ProxyMutex> iptable_lock = m_g_pool->m_ip_pool.lock_for_key(m_g_pool->ip_hash_of(&(to_release->get_server_ip().sa)));
    Ptr<ProxyMutex> fqdn_lock    = m_g_pool->m_fqdn_pool.lock_for_key(to_release->hostname_hash.fold());
    MUTEX_TRY_LOCK(ip_lock, iptable_lock, ethread);
    MUTEX_TRY_LOCK(host_lock, fqdn_lock, ethread);
    if (ip_lock.is_locked() && host_lock.is_locked()) {
      pool->releaseSession(to_release);
    } else {
      Debug("http_ss", "[%" PRId64 "] [release session] could not release session due to lock contention", to_release->con_id);
      released_p = false;
    }
  }

  return released_p ? HSM_DONE : HSM_RETRY;
}
