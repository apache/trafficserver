/** @file

  openssl_utils.cc - Interaction with openssl constructs

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

#include <cstring>
#include <cerrno>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <openssl/rand.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ts/ts.h>

#include "ssl_utils.h"
#include "Config.h"
#include "session_process.h"
#include "common.h"

static int
ssl_new_session(TSSslSessionID &sid)
{
  // Encode ID
  std::string encoded_id;
  int ret = encode_id(sid.bytes, sid.len, encoded_id);
  if (ret < 0) {
    TSError("Encoded id failed.");
    return 0;
  }
  std::string redis_channel = ssl_param.cluster_name + "." + encoded_id;

  int session_ret_len = SSL_SESSION_MAX_DER;
  char session_data[SSL_SESSION_MAX_DER];
  const auto buffer_length = TSSslSessionGetBuffer(&sid, session_data, &session_ret_len);
  if (buffer_length == 0) {
    TSDebug(PLUGIN, "Failed to find a session buffer.");
    return 0;
  } else if (buffer_length > session_ret_len) {
    TSError("Session data is too large. Its size is: %d but our max buffer size is: %d.", buffer_length, SSL_SESSION_MAX_DER);
    return 0;
  }

  std::string encrypted_data;
  ret = encrypt_session(session_data, session_ret_len, (unsigned char *)get_key_ptr(), get_key_length(), encrypted_data);
  if (ret < 0) {
    TSError("Encrypt_session failed.");
    return 0;
  }

  ssl_param.pub->publish(redis_channel, encrypted_data);

  TSDebug(PLUGIN, "Create new session id: %s encoded: %s channel: %s", encoded_id.c_str(), encrypted_data.c_str(),
          redis_channel.c_str());

  return 0;
}

static int
ssl_access_session(TSSslSessionID &sid)
{
  return 0;
}

static int
ssl_del_session(TSSslSessionID &sid)
{
  std::string encoded_id;

  int ret = encode_id(sid.bytes, sid.len, encoded_id);
  if (!ret) {
    TSDebug(PLUGIN, "Session is deleted. id: %s", encoded_id.c_str());
  }

  return 0;
}

int
SSL_session_callback(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN, "SSL_session_callback event: %d", event);
  TSSslSessionID *sessionid = reinterpret_cast<TSSslSessionID *>(edata);

  switch (event) {
  case TS_EVENT_SSL_SESSION_NEW:
    ssl_new_session(*sessionid);
    break;
  case TS_EVENT_SSL_SESSION_REMOVE:
    ssl_del_session(*sessionid);
    break;
  case TS_EVENT_SSL_SESSION_GET:
    ssl_access_session(*sessionid);
    break;
  default:
    break;
  }
  return 0;
}
