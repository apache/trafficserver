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


#ifndef _HTTP_SESSION_MANAGER_H_
#define _HTTP_SESSION_MANAGER_H_

#include "P_EventSystem.h"
#include "HttpServerSession.h"

class HttpClientSession;
class HttpSM;

void
initialize_thread_for_http_sessions(EThread *thread, int thread_index);

#define  HSM_LEVEL1_BUCKETS   127
#define  HSM_LEVEL2_BUCKETS   63

class SessionBucket: public Continuation
{
public:
  SessionBucket();
  int session_handler(int event, void *data);
  Que(HttpServerSession, lru_link) lru_list;
  DList(HttpServerSession, hash_link) l2_hash[HSM_LEVEL2_BUCKETS];
};

enum HSMresult_t
{
  HSM_DONE,
  HSM_RETRY,
  HSM_NOT_FOUND
};

class HttpSessionManager
{
public:
  HttpSessionManager()
  { }

  ~HttpSessionManager()
  { }

  HSMresult_t acquire_session(Continuation *cont, sockaddr const* addr, const char *hostname,
                              HttpClientSession *ua_session, HttpSM *sm);
  HSMresult_t release_session(HttpServerSession *to_release);
  void purge_keepalives();
  void init();
  int main_handler(int event, void *data);

  /// Check if a session is a valid match.
  static bool match(
		    HttpServerSession* s, ///< Session to check for match.
		    sockaddr const* addr, ///< IP address.
		    INK_MD5 const& hostname_hash, ///< Hash of hostname of origin server.
		    HttpSM* sm ///< State machine (for configuration data).
		    );

  /// Check if a session is a valid match.
  static bool match(
		    HttpServerSession* s, ///< Session to check for match.
		    sockaddr const* addr, ///< IP address.
		    char const* hostname, ///< Hostname of origin server.
		    HttpSM* sm ///< State machine (for configuration data).
		    );


private:
  //    Global l1 hash, used when there is no per-thread buckets
  SessionBucket g_l1_hash[HSM_LEVEL1_BUCKETS];
};

extern HttpSessionManager httpSessionManager;

#endif
