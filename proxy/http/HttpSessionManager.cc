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
#include "HttpClientSession.h"
#include "HttpServerSession.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"

// Initialize a thread to handle HTTP session management
void
initialize_thread_for_http_sessions(EThread *thread, int /* thread_index ATS_UNUSED */)
{
  thread->server_session_pool = new ServerSessionPool;
}

HttpSessionManager httpSessionManager;

ServerSessionPool::ServerSessionPool()
  : Continuation(new_ProxyMutex()), m_ip_pool(1023), m_host_pool(1023)
{
  SET_HANDLER(&ServerSessionPool::eventHandler);
  m_ip_pool.setExpansionPolicy(IPHashTable::MANUAL);
  m_host_pool.setExpansionPolicy(HostHashTable::MANUAL);
}

void
ServerSessionPool::purge()
{ 
  for ( IPHashTable::iterator last = m_ip_pool.end(), spot = m_ip_pool.begin() ; spot != last ; ++spot ) {
    spot->do_io_close();
  }
  m_ip_pool.clear();
  m_host_pool.clear();
}

bool
ServerSessionPool::match(HttpServerSession* ss, sockaddr const* addr, INK_MD5 const& hostname_hash, TSServerSessionSharingMatchType match_style)
{
  return TS_SERVER_SESSION_SHARING_MATCH_NONE != match_style && // if no matching allowed, fail immediately.
    // The hostname matches if we're not checking it or it (and the port!) is a match.
    (TS_SERVER_SESSION_SHARING_MATCH_IP == match_style ||  (ats_ip_port_cast(addr) == ats_ip_port_cast(ss->server_ip) && ss->hostname_hash == hostname_hash)) &&
    // The IP address matches if we're not checking it or it is a match.
    (TS_SERVER_SESSION_SHARING_MATCH_HOST == match_style || ats_ip_addr_port_eq(ss->server_ip, addr))
    ;
}

HttpServerSession*
ServerSessionPool::acquireSession(sockaddr const* addr, INK_MD5 const& hostname_hash, TSServerSessionSharingMatchType match_style)
{
  HttpServerSession* zret = NULL;

  if (TS_SERVER_SESSION_SHARING_MATCH_HOST == match_style) {
    // This is broken out because only in this case do we check the host hash first.
    HostHashTable::Location loc = m_host_pool.find(hostname_hash);
    in_port_t port = ats_ip_port_cast(addr);
    while (loc && port != ats_ip_port_cast(loc->server_ip)) ++loc; // scan for matching port.
    if (loc) {
      zret = loc;
      m_host_pool.remove(loc);
      m_ip_pool.remove(m_ip_pool.find(zret));
    }
  } else if (TS_SERVER_SESSION_SHARING_MATCH_NONE != match_style) { // matching is not disabled.
    IPHashTable::Location loc = m_ip_pool.find(addr);
    // If we're matching on the IP address we're done, this one is good enough.
    // Otherwise we need to scan further matches to match the host name as well.
    // Note we don't have to check the port because it's checked as part of the IP address key.
    if (TS_SERVER_SESSION_SHARING_MATCH_IP != match_style) {
      while (loc && loc->hostname_hash != hostname_hash)
        ++loc;
    }
    if (loc) {
      zret = loc;
      m_ip_pool.remove(loc);
      m_host_pool.remove(m_host_pool.find(zret));
    }
  }
  return zret;
}

void
ServerSessionPool::releaseSession(HttpServerSession* ss)
{
  ss->state = HSS_KA_SHARED;
  // Now we need to issue a read on the connection to detect
  //  if it closes on us.  We will get called back in the
  //  continuation for this bucket, ensuring we have the lock
  //  to remove the connection from our lists
  ss->do_io_read(this, INT64_MAX, ss->read_buffer);

  // Transfer control of the write side as well
  ss->do_io_write(this, 0, NULL);

  // we probably don't need the active timeout set, but will leave it for now
  ss->get_netvc()->set_inactivity_timeout(ss->get_netvc()->get_inactivity_timeout());
  ss->get_netvc()->set_active_timeout(ss->get_netvc()->get_active_timeout());
  // put it in the pools.
  m_ip_pool.insert(ss);
  m_host_pool.insert(ss);

  Debug("http_ss", "[%" PRId64 "] [release session] " "session placed into shared pool", ss->con_id);
}

//   Called from the NetProcessor to let us know that a
//    connection has closed down
//
int
ServerSessionPool::eventHandler(int event, void *data)
{
  NetVConnection *net_vc = NULL;
  HttpServerSession *s = NULL;

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

  sockaddr const* addr = net_vc->get_remote_addr();
  HttpConfigParams *http_config_params = HttpConfig::acquire();
  bool found = false;

  for ( ServerSessionPool::IPHashTable::Location lh = m_ip_pool.find(addr) ; lh ; ++lh ) {
    if ((s = lh)->get_netvc() == net_vc) {
      // if there was a timeout of some kind on a keep alive connection, and
      // keeping the connection alive will not keep us above the # of max connections
      // to the origin and we are below the min number of keep alive connections to this
      // origin, then reset the timeouts on our end and do not close the connection
      if ((event == VC_EVENT_INACTIVITY_TIMEOUT || event == VC_EVENT_ACTIVE_TIMEOUT) &&
          s->state == HSS_KA_SHARED &&
          s->enable_origin_connection_limiting) {
        bool connection_count_below_min = s->connection_count->getCount(s->server_ip) <= http_config_params->origin_min_keep_alive_connections;

        if (connection_count_below_min) {
          Debug("http_ss", "[%" PRId64 "] [session_bucket] session received io notice [%s], "
                "reseting timeout to maintain minimum number of connections", s->con_id,
                HttpDebugNames::get_event_name(event));
          s->get_netvc()->set_inactivity_timeout(s->get_netvc()->get_inactivity_timeout());
          s->get_netvc()->set_active_timeout(s->get_netvc()->get_active_timeout());
          found = true;
          break;
        }
      }

      // We've found our server session. Remove it from
      //   our lists and close it down
      Debug("http_ss", "[%" PRId64 "] [session_pool] session %p received io notice [%s]",
            s->con_id, s, HttpDebugNames::get_event_name(event));
      ink_assert(s->state == HSS_KA_SHARED);
      // Out of the pool! Now!
      m_ip_pool.remove(lh);
      m_host_pool.remove(m_host_pool.find(s));
      // Drop connection on this end.
      s->do_io_close();
      found = true;
      break;
    }
  }

  HttpConfig::release(http_config_params);
  if (!found) {
    // We failed to find our session.  This can only be the result
    //  of a programming flaw
    Warning("Connection leak from http keep-alive system");
    ink_assert(0);
  }
  return 0;
}


void
HttpSessionManager::init()
{
  m_g_pool = new ServerSessionPool;
}

// TODO: Should this really purge all keep-alive sessions?
// Does this make any sense, since we always do the global pool and not the per thread?
void
HttpSessionManager::purge_keepalives()
{
  EThread *ethread = this_ethread();

  MUTEX_TRY_LOCK(lock, m_g_pool->mutex, ethread);
  if (lock) {
    m_g_pool->purge();
  } // should we do something clever if we don't get the lock?
}

HSMresult_t
HttpSessionManager::acquire_session(Continuation * /* cont ATS_UNUSED */, sockaddr const* ip,
                                    const char *hostname, HttpClientSession *ua_session, HttpSM *sm)
{
  HttpServerSession *to_return = NULL;
  TSServerSessionSharingMatchType match_style = static_cast<TSServerSessionSharingMatchType>(sm->t_state.txn_conf->server_session_sharing_match);
  INK_MD5 hostname_hash;

  ink_code_md5((unsigned char *) hostname, strlen(hostname), (unsigned char *) &hostname_hash);

  // First check to see if there is a server session bound
  //   to the user agent session
  to_return = ua_session->get_server_session();
  if (to_return != NULL) {
    ua_session->attach_server_session(NULL);

    if (ServerSessionPool::match(to_return, ip, hostname_hash, match_style)) {
      Debug("http_ss", "[%" PRId64 "] [acquire session] returning attached session ", to_return->con_id);
      to_return->state = HSS_ACTIVE;
      sm->attach_server_session(to_return);
      return HSM_DONE;
    }
    // Release this session back to the main session pool and
    //   then continue looking for one from the shared pool
    Debug("http_ss", "[%" PRId64 "] [acquire session] " "session not a match, returning to shared pool", to_return->con_id);
    to_return->release();
    to_return = NULL;
  }

  // Now check to see if we have a connection in our shared connection pool
  EThread *ethread = this_ethread();

  if (TS_SERVER_SESSION_SHARING_POOL_THREAD == sm->t_state.txn_conf->server_session_sharing_pool) {
    to_return = ethread->server_session_pool->acquireSession(ip, hostname_hash, match_style);
  } else {
    MUTEX_TRY_LOCK(lock, m_g_pool->mutex, ethread);
    if (lock) {
      to_return = m_g_pool->acquireSession(ip, hostname_hash, match_style);
      Debug("http_ss", "[acquire session] pool search %s", to_return ? "successful" : "failed");
    } else {
      Debug("http_ss", "[acquire session] could not acquire session due to lock contention");
      return HSM_RETRY;
    }
  }

  if (to_return) {
    Debug("http_ss", "[%" PRId64 "] [acquire session] " "return session from shared pool", to_return->con_id);
    to_return->state = HSS_ACTIVE;
    sm->attach_server_session(to_return);
    return HSM_DONE;
  }
  return HSM_NOT_FOUND;
}

HSMresult_t
HttpSessionManager::release_session(HttpServerSession *to_release)
{
  EThread *ethread = this_ethread();
  ServerSessionPool* pool = TS_SERVER_SESSION_SHARING_POOL_THREAD == to_release->sharing_pool ? ethread->server_session_pool : m_g_pool;
  bool released_p = true;
  
  // The per thread lock looks like it should not be needed but if it's not locked the close checking I/O op will crash.
  MUTEX_TRY_LOCK(lock, pool->mutex, ethread);
  if (lock) {
    pool->releaseSession(to_release);
  } else {
    Debug("http_ss", "[%" PRId64 "] [release session] could not release session due to lock contention", to_release->con_id);
    released_p = false;
  }

  return released_p ? HSM_DONE : HSM_RETRY;
}
