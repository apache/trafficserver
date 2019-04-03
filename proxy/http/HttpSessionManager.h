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
#include "HttpServerSession.h"
#include "tscore/IntrusiveHashMap.h"
#include "tscore/MT_hashtable.h"

class ProxyClientTransaction;
class HttpSM;

void initialize_thread_for_http_sessions(EThread *thread, int thread_index);
typedef HashTableIteratorState<uint32_t, HttpServerSession *> IPTable_iter;
typedef HashTableIteratorState<uint64_t, HttpServerSession *> FqdnTable_iter;
enum HSMresult_t {
  HSM_DONE,
  HSM_RETRY,
  HSM_NOT_FOUND,
};

// class ServerSessionPoolCont;

class ServerSessionPoolCont : public Continuation
{
public:
  ServerSessionPoolCont() : m_session(nullptr), m_ip_key(0), m_net_vc(nullptr), m_event(0){};
  Action m_action;
  void init(Continuation *cont, HttpServerSession *s);
  void init(Continuation *cont, uint32 ip_key, NetVConnection *net_vc, int event);
  int removeFQDNEntry(int /* event ATS_UNUSED */, Event *e);
  int removeIPAndFQDNEntry(int /* event ATS_UNUSED */, Event *e);
  void destroy();

private:
  HttpServerSession *m_session;
  uint32 m_ip_key;
  NetVConnection *m_net_vc;
  int m_event;
};
using ServerSessionPoolContHandler = int (ServerSessionPoolCont::*)(int, void *);

extern ClassAllocator<ServerSessionPoolCont> serverSessionPoolContAllocator;

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
  static uint32_t ip_hash_of(sockaddr const *addr);

  /// Handle events from server sessions.
  int eventHandler(int event, void *data);
  static bool validate_sni(HttpSM *sm, NetVConnection *netvc);
  bool removeSession(uint32 ip_key, NetVConnection *net_vc, int event);

protected:
  using IPTable   = MTHashTable<uint32_t, HttpServerSession *>;
  using FQDNTable = MTHashTable<uint64_t, HttpServerSession *>;

public:
  /** Check if a session matches address and host name.
   */
  static bool match(HttpServerSession *ss, sockaddr const *addr, CryptoHash const &host_hash,
                    TSServerSessionSharingMatchType match_style);

  /** Get a session from the pool.

      The session is selected based on @a match_style equivalently to @a match. If found the session
      is removed from the pool.

      @return A pointer to the session or @c NULL if not matching session was found.
  */
  HSMresult_t acquireSession(sockaddr const *addr, CryptoHash const &host_hash, TSServerSessionSharingMatchType match_style,
                             HttpSM *sm, HttpServerSession *&server_session);
  /** Release a session to to pool.
   */
  void releaseSession(HttpServerSession *ss);

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
  HSMresult_t acquire_session(Continuation *cont, sockaddr const *addr, const char *hostname, ProxyClientTransaction *ua_txn,
                              HttpSM *sm);
  HSMresult_t release_session(HttpServerSession *to_release);
  void purge_keepalives();
  void init();
  int main_handler(int event, void *data);
  ServerSessionPool *
  get_pool() const
  {
    return m_g_pool;
  }

private:
  /// Global pool, used if not per thread pools.
  /// @internal We delay creating this because the session manager is created during global statics init.
  ServerSessionPool *m_g_pool = nullptr;
};

extern HttpSessionManager httpSessionManager;
