/** @file

  subscriber.cc - Redis subscriber

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

#include <unistd.h>
#include <ts/ts.h>
#include <openssl/ssl.h>
#include <hiredis/hiredis.h>
#include "common.h"

#include "subscriber.h"
#include "Config.h"
#include "redis_auth.h"
#include "session_process.h"
#include "stek.h"
#include "ssl_utils.h"

void *
setup_subscriber(void *arg)
{
  RedisSubscriber *me = static_cast<RedisSubscriber *>(arg);
  me->run();
  return (void *)1;
}

RedisSubscriber::RedisSubscriber(const std::string &conf)
  : m_channel(cDefaultSubColoChannel),
    m_redisConnectTimeout(cDefaultRedisConnectTimeout),
    m_redisRetryDelay(cDefaultRedisRetryDelay),
    err(false)
{
  std::string redisEndpointsStr;

  if (Config::getSingleton().loadConfig(conf)) {
    Config::getSingleton().getValue("redis", "RedisConnectTimeout", m_redisConnectTimeout);
    Config::getSingleton().getValue("redis", "RedisRetryDelay", m_redisRetryDelay);
    Config::getSingleton().getValue("subconfig", "SubColoChannel", m_channel);
    Config::getSingleton().getValue("redis", "RedisEndpoints", redisEndpointsStr);
  }

  // get our psk to access session_reuse redis network.
  char redis_auth_key[MAX_REDIS_KEYSIZE];
  if (!(get_redis_auth_key(redis_auth_key, MAX_REDIS_KEYSIZE))) {
    err = true;
    TSError("RedisPublisher::RedisPublisher.Cannot get redis AUTH password.");
    redis_passwd.clear();
  } else {
    redis_passwd = redis_auth_key;
    memset(redis_auth_key, 0, MAX_REDIS_KEYSIZE); // tidy up our stack
  }

  m_channel_prefix = m_channel.substr(0, m_channel.find('*'));

  TSDebug(PLUGIN, "RedisSubscriber::RedisSubscriber.SubscriberChannel= %s SubscriberChannelPrefix= %s", m_channel.c_str(),
          m_channel_prefix.c_str());

  addto_endpoint_vector(m_redisEndpoints, redisEndpointsStr);
  m_redisEndpointsIndex = 0;

  for (unsigned int i = 0; i < m_redisEndpoints.size(); i++) {
    TSThreadCreate(setup_subscriber, static_cast<void *>(this));
  }
}

bool
RedisSubscriber::is_good()
{
  return !err;
}

int
RedisSubscriber::get_endpoint_index()
{
  int retval = m_redisEndpointsIndex++;
  return retval;
}

::redisContext *
RedisSubscriber::setup_connection(int index)
{
  TSDebug(PLUGIN, "RedisSubscriber::setup_connection.called for host= %s and port= %d", m_redisEndpoints[index].m_hostname.c_str(),
          m_redisEndpoints[index].m_port);

  ::redisContext *my_context(nullptr);
  struct ::timeval timeout_connect;
  timeout_connect.tv_sec  = m_redisConnectTimeout / 1000;
  timeout_connect.tv_usec = (m_redisConnectTimeout % 1000) * 1000;

  while (true) {
    my_context =
      ::redisConnectWithTimeout(m_redisEndpoints[index].m_hostname.c_str(), m_redisEndpoints[index].m_port, timeout_connect);
    if (!my_context) {
      TSError("RedisSubscriber::setup_connection.connect to host: %s and port: %d failed",
              m_redisEndpoints[index].m_hostname.c_str(), m_redisEndpoints[index].m_port);
    } else if (my_context->err) {
      TSError("RedisSubscriber::setup_connection.connect to host: %s and port: %d  failed.",
              m_redisEndpoints[index].m_hostname.c_str(), m_redisEndpoints[index].m_port);
    } else {
      TSDebug(PLUGIN, "RedisSubscriber::setup_connection: successfully connected to the redis host: %s on port: %d",
              m_redisEndpoints[index].m_hostname.c_str(), m_redisEndpoints[index].m_port);

      redisReply *reply = static_cast<redisReply *>(redisCommand(my_context, "AUTH %s", redis_passwd.c_str()));

      if (reply == nullptr) {
        TSError("RedisSubscriber::setup_connection Cannot AUTH redis server, no reply.");
      } else if (reply->type == REDIS_REPLY_ERROR) {
        TSError("RedisSubscriber::setup_connection Cannot AUTH redis server, error reply.");
        freeReplyObject(reply);
      } else {
        TSDebug(PLUGIN, "RedisSubscriber::setup_connection. Successfully AUTH redis server.");
        freeReplyObject(reply);
      }

      break;
    }

    TSError("RedisSubscriber::setup_connection.will wait for: %d microseconds and try again.", m_redisRetryDelay);

    ::usleep(m_redisRetryDelay);
  }

  return my_context;
}

void
RedisSubscriber::run()
{
  TSDebug(PLUGIN, "RedisSubscriber::runMaster.called");
  int my_endpoint_index = get_endpoint_index();
  ::redisContext *my_context(setup_connection(my_endpoint_index));
  ::redisReply *current_reply(nullptr);

  while (true) {
    while ((!my_context) || (my_context->err)) {
      ::usleep(m_redisRetryDelay);
      my_context = setup_connection(my_endpoint_index);
    }

    TSDebug(PLUGIN, "RedisSubscriber::runMaster.Issuing command: \"PSUBSCRIBE %s\"", m_channel.c_str());
    current_reply = static_cast<redisReply *>(::redisCommand(my_context, "PSUBSCRIBE %s", m_channel.c_str()));

    if (!current_reply || (REDIS_REPLY_ERROR == current_reply->type)) {
      TSError("RedisSubscriber::runMaster.subscribe to redis server on channel: %s failed.", m_channel.c_str());
      ::usleep(1000 * 1000);
      continue;
    } else {
      TSDebug(PLUGIN, "RedisSubscriber::runMaster.Successfully subscribed to channel: \"%s\"", m_channel.c_str());
      TSDebug(PLUGIN, "RedisSubscriber::runMaster.Waiting for messages to appear on the channel!");
      ::freeReplyObject(current_reply);
    }

    // Blocking read
    while (REDIS_OK == ::redisGetReply(my_context, reinterpret_cast<void **>(&current_reply))) {
      // Process Message
      std::string channel(current_reply->element[2]->str, current_reply->element[2]->len);
      std::string data(current_reply->element[3]->str, current_reply->element[3]->len);
      TSDebug(PLUGIN, "Redis request channel=%s message=%s", channel.c_str(), data.c_str());

      std::string key = "";
      // Strip the channel name to get the key
      if (channel.compare(0, m_channel_prefix.length(), m_channel_prefix) == 0) {
        key = channel.substr(m_channel_prefix.length());
      }

      // If this is new keys, go do the ticket key updates
      if (strncmp(key.c_str(), STEK_ID_NAME, strlen(STEK_ID_NAME)) == 0) {
        STEK_update(data);
        // Requesting last ticket to be resent
      } else if (strncmp(key.c_str(), STEK_ID_RESEND, strlen(STEK_ID_RESEND)) == 0) {
        if (isSTEKMaster()) {
          TSDebug(PLUGIN, "RedisSubscriber Resend ticket");
          STEK_Send_To_Network(ssl_param.ticket_keys);
        }
      } else { // Otherwise this is a new session.  Let ATS core know about it
        char session_id[SSL_MAX_SSL_SESSION_ID_LENGTH * 2];
        int session_id_len = sizeof(session_id);
        if (decode_id(key, session_id, session_id_len) == 0) { // Decrypt the data
          TSDebug(PLUGIN, "Add session encoded_id=%s decoded_id=%.*s %d", key.c_str(), session_id_len, session_id, session_id_len);
          add_session(session_id, session_id_len, data);
        } else {
          TSDebug(PLUGIN, "failed to decode key=%s", key.c_str());
        }
      }

      ::freeReplyObject(current_reply);

      TSDebug(PLUGIN, "RedisSubscriber::runMaster.Got message: %s on channel: %s", data.c_str(), channel.c_str());
    }
  }
}

RedisSubscriber::~RedisSubscriber()
{
  TSDebug(PLUGIN, "RedisSubscriber::~RedisSubscriber.called for endpoint");
}
