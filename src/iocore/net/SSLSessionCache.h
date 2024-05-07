/** @file

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

#pragma once

#include "tscore/List.h"
#include "tscore/ink_mutex.h"
#include "../../../src/iocore/eventsystem/P_EventSystem.h"
#include "records/RecProcess.h"
#include "tscore/ink_platform.h"
#include "../../../src/iocore/net/P_SSLUtils.h"
#include "ts/apidefs.h"
#include <openssl/ssl.h>
#include <mutex>
#include <tsutil/TsSharedMutex.h>

#define SSL_MAX_SESSION_SIZE      256
#define SSL_MAX_ORIG_SESSION_SIZE 4096

struct ssl_session_cache_exdata {
  ssl_curve_id curve = 0;
};

class SSLOriginSession
{
public:
  std::string                  key;
  ssl_curve_id                 curve_id;
  std::shared_ptr<SSL_SESSION> shared_sess = nullptr;

  SSLOriginSession(const std::string &lookup_key, ssl_curve_id curve, std::shared_ptr<SSL_SESSION> session)
    : key(lookup_key), curve_id(curve), shared_sess(session)
  {
  }

  LINK(SSLOriginSession, link);
};

class SSLOriginSessionCache
{
public:
  SSLOriginSessionCache();
  ~SSLOriginSessionCache();

  void                         insert_session(const std::string &lookup_key, SSL_SESSION *sess, SSL *ssl);
  std::shared_ptr<SSL_SESSION> get_session(const std::string &lookup_key, ssl_curve_id *curve);
  void                         remove_session(const std::string &lookup_key);

private:
  void remove_oldest_session(const std::unique_lock<ts::shared_mutex> &lock);

  mutable ts::shared_mutex                  mutex;
  CountQueue<SSLOriginSession>              orig_sess_que;
  std::map<std::string, SSLOriginSession *> orig_sess_map;
};
