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

#define FIRST_LEVEL_HASH(x)   x % HSM_LEVEL1_BUCKETS
#define SECOND_LEVEL_HASH(x)  x % HSM_LEVEL2_BUCKETS

HttpSessionManager httpSessionManager;

SessionBucket::SessionBucket():Continuation(NULL)
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

  // Search the 2nd level bucket for apprpropriate netvc
  int l2_index = SECOND_LEVEL_HASH(net_vc->get_remote_ip());
  ink_assert(l2_index < HSM_LEVEL2_BUCKETS);
  s = l2_hash[l2_index].head;
  while (s != NULL) {
    if (s->get_netvc() == net_vc) {

      // if there was a timeout of some kind on a keep alive connection, and 
      // keeping the connection alive will not keep us above the # of max connections
      // to the origin and we are below the min number of keep alive connections to this 
      // origin, then reset the timeouts on our end and do not close the connection
      if( (event == VC_EVENT_INACTIVITY_TIMEOUT || event == VC_EVENT_ACTIVE_TIMEOUT) &&
           s->state == HSS_KA_SHARED &&
           s->enable_origin_connection_limiting ) {

        HttpConfigParams *http_config_params = HttpConfig::acquire();
        bool connection_count_below_min = s->connection_count->getCount(s->server_ip) <= http_config_params->origin_min_keep_alive_connections;
        HttpConfig::release(http_config_params);

        if( connection_count_below_min ) {
          Debug("http_ss", "[%lld] [session_bucket] session received io notice [%s], "
                "reseting timeout to maintain minimum number of connections", s->con_id,
                HttpDebugNames::get_event_name(event));
          s->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(
            HttpConfig::m_master.keep_alive_no_activity_timeout_out));
          s->get_netvc()->set_active_timeout(HRTIME_SECONDS(
            HttpConfig::m_master.keep_alive_no_activity_timeout_out));
          return 0;
        }
      }


      // We've found our server session. Remove it from
      //   our lists and close it down
      Debug("http_ss", "[%lld] [session_bucket] session received "
            "io notice [%s]", s->con_id, HttpDebugNames::get_event_name(event));
      ink_assert(s->state == HSS_KA_SHARED);
      lru_list.remove(s);
      l2_hash[l2_index].remove(s);
      s->do_io_close();
      return 0;
    } else {
      s = s->hash_link.next;
    }
  }

  // We failed to find our session.  This can only be the result
  //  of a programming flaw
  Warning("Connection leak from http keep-alive system");
  ink_assert(0);
  return 0;
}

HttpSessionManager::HttpSessionManager()
{
}

HttpSessionManager::~HttpSessionManager()
{
}

void
HttpSessionManager::init()
{
  // Initialize our internal (global) hash table
  for (int i = 0; i < HSM_LEVEL1_BUCKETS; i++) {
    g_l1_hash[i].mutex = new_ProxyMutex();
  }
}
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
        int l2_index = SECOND_LEVEL_HASH(sess->server_ip);
        b->l2_hash[l2_index].remove(sess);
        sess->do_io_close();
      }
    } else {
      // Fix me, should retry 
    }
  }
}
HSMresult_t
HttpSessionManager::acquire_session(Continuation * cont, unsigned int ip, int port,
                                    const char *hostname, HttpClientSession * ua_session, HttpSM * sm)
{

  HttpServerSession *to_return = NULL;

  // We compute the mmh for matching the hostname as the last
  //  check for a match between the session the HttpSM is
  //  looking for and the sessions we have.  The reason it's
  //  the last check is to save the cycles of needless 
  //  computing extra mmhs.  We have to use the hostname 
  //  as part of the match because some stupid servers can't 
  //  handle getting request for different virtual hosts over
  //  the same keep-alive session (INKqa05429).  
  // Also, note the ip is required as well to maintain client 
  //  to server affinity so that we don't break certain types 
  //  of authentication.
  bool hash_computed = false;
  INK_MD5 hostname_hash;

  // First check to see if there is a server session bound
  //   to the user agent session
  to_return = ua_session->get_server_session();
  if (to_return != NULL) {
    ua_session->attach_server_session(NULL);

    if (to_return->server_ip == ip && to_return->server_port == port) {

      ink_code_MMH((unsigned char *) hostname, strlen(hostname), (unsigned char *) &hostname_hash);
      hash_computed = true;

      if (hostname_hash == to_return->hostname_hash) {
        Debug("http_ss", "[%lld] [acquire session] returning attached session ", to_return->con_id);
        to_return->state = HSS_ACTIVE;
        sm->attach_server_session(to_return);
        return HSM_DONE;
      }
    }
    // Release this session back to the main session pool and
    //   then continue looking for one from the shared pool
    Debug("http_ss", "[%lld] [acquire session] " "session not a match, returning to shared pool", to_return->con_id);
    to_return->release();
    to_return = NULL;
  }
  // Now check to see if we have a connection is our
  //  shared connection pool
  int l1_index = FIRST_LEVEL_HASH(ip);

  ink_assert(l1_index < HSM_LEVEL1_BUCKETS);

  SessionBucket *bucket;
  ProxyMutex *bucket_mutex;
  EThread *ethread = this_ethread();

  bucket = g_l1_hash + l1_index;
  bucket_mutex = bucket->mutex;

  MUTEX_TRY_LOCK(lock, bucket_mutex, ethread);
  if (lock) {

    int l2_index = SECOND_LEVEL_HASH(ip);
    ink_assert(l2_index < HSM_LEVEL2_BUCKETS);

    // Check to see if an appropriate connection is in
    //  the 2nd level bucket
    HttpServerSession *b;
    b = bucket->l2_hash[l2_index].head;
    while (b != NULL) {
      if (b->server_ip == ip && b->server_port == port) {

        if (hash_computed == false) {
          ink_code_MMH((unsigned char *) hostname, strlen(hostname), (unsigned char *) &hostname_hash);
          hash_computed = true;
        }

        if (hostname_hash == b->hostname_hash) {

          // We found a match.  Since the lock for the 1st level
          //  bucket is the same one that we use for the read 
          //  on the keep alive connection, we are safe since 
          //  we can not get called back from the netProcessor 
          //  here.  The SM will do a do_io when it gets the session,
          //  effectively canceling the keep-alive read
          bucket->lru_list.remove(b);
          bucket->l2_hash[l2_index].remove(b);
          b->state = HSS_ACTIVE;
          to_return = b;
          Debug("http_ss", "[%lld] [acquire session] " "return session from shared pool", to_return->con_id);
          sm->attach_server_session(to_return);
          return HSM_DONE;
        }
      }

      b = b->hash_link.next;
    }

    return HSM_NOT_FOUND;
  } else {
    return HSM_RETRY;
  }
}


HSMresult_t
HttpSessionManager::release_session(HttpServerSession * to_release)
{
  int l1_index = FIRST_LEVEL_HASH(to_release->server_ip);

  ink_assert(l1_index < HSM_LEVEL1_BUCKETS);

  EThread *ethread = this_ethread();

  ProxyMutex *bucket_mutex;
  SessionBucket *bucket;

#ifdef TRANSACTION_ON_A_THREAD
  bucket = to_release->mutex->thread_holding->l1_hash + l1_index;
  bucket_mutex = to_release->mutex;
#else
  bucket = g_l1_hash + l1_index;
  bucket_mutex = bucket->mutex;
#endif

  MUTEX_TRY_LOCK(lock, bucket_mutex, ethread);
  if (lock) {

    int l2_index = SECOND_LEVEL_HASH(to_release->server_ip);
    ink_assert(l2_index < HSM_LEVEL2_BUCKETS);

    // First insert the session on to our lists
    bucket->lru_list.enqueue(to_release);
    bucket->l2_hash[l2_index].push(to_release);
    to_release->state = HSS_KA_SHARED;

    // Now we need to issue a read on the connection to detect
    //  if it closes on us.  We will get called back in the
    //  continuation for this bucket, ensuring we have the lock
    //  to remove the connection from our lists
    to_release->do_io_read(bucket, INT_MAX, to_release->read_buffer);

    // Transfer control of the write side as well
    to_release->do_io_write(bucket, 0, NULL);

    // we probably don't need the active timeout set, but will leave it for now
    to_release->get_netvc()->
      set_inactivity_timeout(HRTIME_SECONDS(HttpConfig::m_master.keep_alive_no_activity_timeout_out));
    to_release->get_netvc()->
      set_active_timeout(HRTIME_SECONDS(HttpConfig::m_master.keep_alive_no_activity_timeout_out));

    Debug("http_ss", "[%lld] [release session] " "session placed into shared pool", to_release->con_id);
    return HSM_DONE;
  } else {
    Debug("http_ss", "[%lld] [release session] "
          "could not release session due to lock contention", to_release->con_id);
    return HSM_RETRY;
  }
}
