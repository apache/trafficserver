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

#define FIRST_LEVEL_HASH(x)   ats_ip_hash(x) % HSM_LEVEL1_BUCKETS
#define SECOND_LEVEL_HASH(x)  ats_ip_hash(x) % HSM_LEVEL2_BUCKETS

// Initialize a thread to handle HTTP session management
void
initialize_thread_for_http_sessions(EThread *thread, int /* thread_index ATS_UNUSED */)
{
  thread->l1_hash = NEW(new SessionBucket[HSM_LEVEL1_BUCKETS]);
  for (int i = 0; i < HSM_LEVEL1_BUCKETS; ++i)
    thread->l1_hash[i].mutex = new_ProxyMutex();
  //thread->l1_hash[i].mutex = thread->mutex;
}


HttpSessionManager httpSessionManager;

SessionBucket::SessionBucket()
  : Continuation(NULL)
{
  SET_HANDLER(&SessionBucket::session_handler);
}

// int SessionBucket::session_handler(int event, void* data)
//
//   Called from the NetProcessor to left us know that a
//    connection has closed down
//
int
SessionBucket::session_handler(int event, void *data)
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
    net_vc = (NetVConnection *) ((VIO *) data)->vc_server;
    break;

  default:
    ink_release_assert(0);
    return 0;
  }

  // Search the 2nd level bucket for appropriate netvc
  int l2_index = SECOND_LEVEL_HASH(net_vc->get_remote_addr());
  HttpConfigParams *http_config_params = HttpConfig::acquire();
  bool found = false;

  ink_assert(l2_index < HSM_LEVEL2_BUCKETS);
  s = l2_hash[l2_index].head;

  while (s != NULL) {
    if (s->get_netvc() == net_vc) {
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
      Debug("http_ss", "[%" PRId64 "] [session_bucket] session received io notice [%s]",
            s->con_id, HttpDebugNames::get_event_name(event));
      ink_assert(s->state == HSS_KA_SHARED);
      lru_list.remove(s);
      l2_hash[l2_index].remove(s);
      s->do_io_close();
      found = true;
      break;
    } else {
      s = s->hash_link.next;
    }
  }

  HttpConfig::release(http_config_params);
  if (found)
    return 0;

  // We failed to find our session.  This can only be the result
  //  of a programming flaw
  Warning("Connection leak from http keep-alive system");
  ink_assert(0);
  return 0;
}


void
HttpSessionManager::init()
{
  // Initialize our internal (global) hash table
  for (int i = 0; i < HSM_LEVEL1_BUCKETS; i++) {
    g_l1_hash[i].mutex = new_ProxyMutex();
  }
}

// TODO: Should this really purge all keep-alive sessions?
void
HttpSessionManager::purge_keepalives()
{
  EThread *ethread = this_ethread();

  for (int i = 0; i < HSM_LEVEL1_BUCKETS; i++) {
    SessionBucket *b = &g_l1_hash[i];
    MUTEX_TRY_LOCK(lock, b->mutex, ethread);
    if (lock) {
      while (b->lru_list.head) {
        HttpServerSession *sess = b->lru_list.head;
        b->lru_list.remove(sess);
        int l2_index = SECOND_LEVEL_HASH(&sess->server_ip.sa);
        b->l2_hash[l2_index].remove(sess);
        sess->do_io_close();
      }
    } else {
      // Fix me, should retry
    }
  }
}

HSMresult_t
_acquire_session(SessionBucket *bucket, sockaddr const* ip, INK_MD5 &hostname_hash, HttpSM *sm)
{
  HttpServerSession *b;
  HttpServerSession *to_return = NULL;
  int l2_index = SECOND_LEVEL_HASH(ip);

  ink_assert(l2_index < HSM_LEVEL2_BUCKETS);

  // Check to see if an appropriate connection is in
  //  the 2nd level bucket
  b = bucket->l2_hash[l2_index].head;
  while (b != NULL) {
    if (ats_ip_addr_eq(&b->server_ip.sa, ip) &&
      ats_ip_port_cast(ip) == ats_ip_port_cast(&b->server_ip)
    ) {
      if (hostname_hash == b->hostname_hash) {
        bucket->lru_list.remove(b);
        bucket->l2_hash[l2_index].remove(b);
        b->state = HSS_ACTIVE;
        to_return = b;
        Debug("http_ss", "[%" PRId64 "] [acquire session] " "return session from shared pool", to_return->con_id);
        sm->attach_server_session(to_return);
        return HSM_DONE;
      }
    }

    b = b->hash_link.next;
  }

  return HSM_NOT_FOUND;
}

HSMresult_t
HttpSessionManager::acquire_session(Continuation * /* cont ATS_UNUSED */, sockaddr const* ip,
                                    const char *hostname, HttpClientSession *ua_session, HttpSM *sm)
{
  HttpServerSession *to_return = NULL;

  //  We compute the hash for matching the hostname as the last
  //  check for a match between the session the HttpSM is looking
  //  for and the sessions we have. We have to use the hostname
  //  as part of the match because some stupid servers can't
  //  handle getting request for different virtual hosts over
  //  the same keep-alive session (INKqa05429).
  // 
  //  Also, note the ip is required as well to maintain client
  //  to server affinity so that we don't break certain types
  //  of authentication.
  INK_MD5 hostname_hash;

  ink_code_md5((unsigned char *) hostname, strlen(hostname), (unsigned char *) &hostname_hash);

  // First check to see if there is a server session bound
  //   to the user agent session
  to_return = ua_session->get_server_session();
  if (to_return != NULL) {
    ua_session->attach_server_session(NULL);

    if (ats_ip_addr_eq(&to_return->server_ip.sa, ip) &&
	ats_ip_port_cast(&to_return->server_ip) == ats_ip_port_cast(ip)) {
      if (hostname_hash == to_return->hostname_hash) {
        Debug("http_ss", "[%" PRId64 "] [acquire session] returning attached session ", to_return->con_id);
        to_return->state = HSS_ACTIVE;
        sm->attach_server_session(to_return);
        return HSM_DONE;
      }
    }
    // Release this session back to the main session pool and
    //   then continue looking for one from the shared pool
    Debug("http_ss", "[%" PRId64 "] [acquire session] " "session not a match, returning to shared pool", to_return->con_id);
    to_return->release();
    to_return = NULL;
  }

  // Now check to see if we have a connection is our
  //  shared connection pool
  int l1_index = FIRST_LEVEL_HASH(ip);
  EThread *ethread = this_ethread();

  ink_assert(l1_index < HSM_LEVEL1_BUCKETS);

  if (2 == sm->t_state.txn_conf->share_server_sessions) {
    ink_assert(ethread->l1_hash);
    return _acquire_session(ethread->l1_hash + l1_index, ip, hostname_hash, sm);
  } else {
    SessionBucket *bucket = g_l1_hash + l1_index;

    MUTEX_TRY_LOCK(lock, bucket->mutex, ethread);
    if (lock) {
      return _acquire_session(bucket, ip, hostname_hash, sm);
    } else {
      Debug("http_ss", "[acquire session] could not acquire session due to lock contention");
    }
  }

  return HSM_RETRY;
}

HSMresult_t
HttpSessionManager::release_session(HttpServerSession *to_release)
{
  EThread *ethread = this_ethread();
  int l1_index = FIRST_LEVEL_HASH(&to_release->server_ip.sa);
  SessionBucket *bucket;

  ink_assert(l1_index < HSM_LEVEL1_BUCKETS);

  if (2 == to_release->share_session) {
    bucket = ethread->l1_hash + l1_index;
  } else {
    bucket = g_l1_hash + l1_index;
  }

  MUTEX_TRY_LOCK(lock, bucket->mutex, ethread);
  if (lock) {
    int l2_index = SECOND_LEVEL_HASH(&to_release->server_ip.sa);

    ink_assert(l2_index < HSM_LEVEL2_BUCKETS);

    // First insert the session on to our lists
    bucket->lru_list.enqueue(to_release);
    bucket->l2_hash[l2_index].push(to_release);
    to_release->state = HSS_KA_SHARED;

    // Now we need to issue a read on the connection to detect
    //  if it closes on us.  We will get called back in the
    //  continuation for this bucket, ensuring we have the lock
    //  to remove the connection from our lists
    to_release->do_io_read(bucket, INT64_MAX, to_release->read_buffer);

    // Transfer control of the write side as well
    to_release->do_io_write(bucket, 0, NULL);

    // we probably don't need the active timeout set, but will leave it for now
    to_release->get_netvc()->set_inactivity_timeout(to_release->get_netvc()->get_inactivity_timeout());
    to_release->get_netvc()->set_active_timeout(to_release->get_netvc()->get_active_timeout());
    Debug("http_ss", "[%" PRId64 "] [release session] " "session placed into shared pool", to_release->con_id);

    return HSM_DONE;
  } else {
    Debug("http_ss", "[%" PRId64 "] [release session] could not release session due to lock contention", to_release->con_id);
  }

  return HSM_RETRY;
}
