/** @file

  publish.cc - redis publisher class

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

#include <iostream>
#include <exception>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <sys/time.h>
#include <ts/ts.h>

#include "common.h"
#include "publisher.h"
#include "Config.h"
#include "redis_auth.h"
#include "ssl_utils.h"
#include <cinttypes>
#include <condition_variable>

std::mutex q_mutex;
std::condition_variable q_checker;
bool q_ready = false;

void *
RedisPublisher::start_worker_thread(void *arg)
{
  plugin_threads.store(pthread_self());
  ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
  ::pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);

  RedisPublisher *publisher = static_cast<RedisPublisher *>(arg);
  publisher->runWorker();
  return arg;
}

RedisPublisher::RedisPublisher(const std::string &conf)
  : m_redisEndpointsStr(cDefaultRedisEndpoint),
    m_numWorkers(cPubNumWorkerThreads),
    m_redisConnectTimeout(cDefaultRedisConnectTimeout),
    m_redisConnectTries(cDefaultRedisConnectTries),
    m_redisPublishTries(cDefaultRedisPublishTries),
    m_redisRetryDelay(cDefaultRedisRetryDelay),
    m_maxQueuedMessages(cDefaultMaxQueuedMessages)
{
  if (Config::getSingleton().loadConfig(conf)) {
    Config::getSingleton().getValue("pubconfig", "PubNumWorkers", m_numWorkers);
    Config::getSingleton().getValue("redis", "RedisEndpoints", m_redisEndpointsStr);
    Config::getSingleton().getValue("redis", "RedisConnectTimeout", m_redisConnectTimeout);
    Config::getSingleton().getValue("pubconfig", "PubRedisPublishTries", m_redisPublishTries);
    Config::getSingleton().getValue("pubconfig", "PubRedisConnectTries", m_redisConnectTries);
    Config::getSingleton().getValue("redis", "RedisRetryDelay", m_redisRetryDelay);
    Config::getSingleton().getValue("pubconfig", "PubMaxQueuedMessages", m_maxQueuedMessages);
    Config::getSingleton().getValue("redis", "RedisConnectTimeout", m_poolRedisConnectTimeout);
  }

  // get our psk to access session_reuse redis network.
  char redis_auth_key[MAX_REDIS_KEYSIZE];
  if (!(get_redis_auth_key(redis_auth_key, MAX_REDIS_KEYSIZE))) {
    err = true;
    TSError("RedisPublisher::RedisPublisher: Cannot get redis AUTH password.");
    redis_passwd.clear();
  } else {
    redis_passwd = redis_auth_key;
    memset(redis_auth_key, 0, MAX_REDIS_KEYSIZE); // tidy up our stack
  }

  addto_endpoint_vector(m_redisEndpoints, m_redisEndpointsStr);

  TSDebug(PLUGIN, "RedisPublisher::RedisPublisher: NumWorkers: %d RedisConnectTimeout: %d", m_numWorkers, m_redisConnectTimeout);
  TSDebug(PLUGIN,
          "RedisPublisher::RedisPublisher: RedisPublishTries: %d RedisConnectTries: %d RedisRetryDelay: %d MaxQueuedMessages: %d",
          m_redisPublishTries, m_redisConnectTries, m_redisRetryDelay, m_maxQueuedMessages);

  TSDebug(PLUGIN, "RedisPublisher::RedisPublisher: Redis Publish endpoints are as follows:");
  for (auto &m_redisEndpoint : m_redisEndpoints) {
    simple_pool *pool = simple_pool::create(m_redisEndpoint.m_hostname, m_redisEndpoint.m_port, m_poolRedisConnectTimeout);
    pools.push_back(pool);
  }

  TSDebug(PLUGIN, "RedisPublisher::RedisPublisher: PoolRedisConnectTimeout: %d", m_poolRedisConnectTimeout);

  ::sem_init(&m_workerSem, 0, 0);

  if (m_redisEndpoints.size() > m_numWorkers) {
    err = true;
    TSError("RedisPublisher::RedisPublisher: Number of threads in the thread pool less than the number of redis endpoints.");
  }

  if (!err) {
    for (unsigned int i = 0; i < m_redisEndpoints.size(); i++) {
      TSThreadCreate(RedisPublisher::start_worker_thread, static_cast<void *>(this));
    }
  }
}

bool
RedisPublisher::is_good()
{
  return !err;
}

::redisContext *
RedisPublisher::setup_connection(const RedisEndpoint &re)
{
  uint64_t my_id = 0;
  if (TSIsDebugTagSet(PLUGIN)) {
    my_id = static_cast<uint64_t>(pthread_self());
    TSDebug(PLUGIN, "RedisPublisher::setup_connection: Called by threadId: %" PRIx64, my_id);
  }

  RedisContextPtr my_context;
  struct ::timeval timeout;
  timeout.tv_sec  = m_redisConnectTimeout / 1000;
  timeout.tv_usec = (m_redisConnectTimeout % 1000) * 1000;

  for (int i = 0; i < static_cast<int>(m_redisConnectTries); ++i) {
    my_context.reset(::redisConnectWithTimeout(re.m_hostname.c_str(), re.m_port, timeout));
    if (!my_context) {
      TSError("RedisPublisher::setup_connection: Connect to host: %s port: %d fail count: %d threadId: %" PRIx64,
              re.m_hostname.c_str(), re.m_port, i + 1, my_id);
    } else if (my_context->err) {
      TSError("RedisPublisher::setup_connection: Connect to host: %s port: %d fail count: %d threadId: %" PRIx64,
              re.m_hostname.c_str(), re.m_port, i + 1, my_id);
      my_context.reset(nullptr);
    } else {
      TSDebug(PLUGIN, "RedisPublisher::setup_connection: threadId: %" PRIx64 " Successfully connected to the redis instance.",
              my_id);

      redisReply *reply = static_cast<redisReply *>(redisCommand(my_context.get(), "AUTH %s", redis_passwd.c_str()));

      if (reply == nullptr) {
        TSError("RedisPublisher::setup_connection: Cannot AUTH redis server, no reply.");
        my_context.reset(nullptr);
      } else if (reply->type == REDIS_REPLY_ERROR) {
        TSError("RedisPublisher::setup_connection: Cannot AUTH redis server, error reply.");
        freeReplyObject(reply);
        my_context.reset(nullptr);
      } else {
        TSDebug(PLUGIN, "RedisPublisher::setup_connection: Successfully AUTH redis server.");
        freeReplyObject(reply);
      }
      break;
    }

    TSError("RedisPublisher::setup_connection: Connect failed, will wait for: %d microseconds and try again.", m_redisRetryDelay);
    ::usleep(m_redisRetryDelay);
  }

  return my_context.release();
}

::redisReply *
RedisPublisher::send_publish(RedisContextPtr &ctx, const RedisEndpoint &re, const Message &msg)
{
  uint64_t my_id = 0;
  if (TSIsDebugTagSet(PLUGIN)) {
    my_id = static_cast<uint64_t>(pthread_self());
    TSDebug(PLUGIN, "RedisPublisher::send_publish: Called by threadId: %" PRIx64, my_id);
  }

  ::redisReply *current_reply(nullptr);

  for (int i = 0; i < static_cast<int>(m_redisPublishTries); ++i) {
    if (!ctx) {
      ctx.reset(setup_connection(re));

      if (!ctx) {
        TSError("RedisPublisher::send_publish: Unable to setup a connection to the redis server: %s:%d threadId: %" PRIx64
                " try: %d",
                re.m_hostname.c_str(), re.m_port, my_id, (i + 1));
        continue;
      }
    }

    current_reply = static_cast<redisReply *>(::redisCommand(ctx.get(), "PUBLISH %s %s", msg.channel.c_str(), msg.data.c_str()));
    if (!current_reply) {
      TSError("RedisPublisher::send_publish: Unable to get a reply from the server for publish. threadId: %" PRIx64 " try: %d",
              my_id, (i + 1));
      ctx.reset(nullptr); // Clean up previous attempt

    } else if (REDIS_REPLY_ERROR == current_reply->type) {
      TSError("RedisPublisher::send_publish: Server responded with error for publish. threadId: %" PRIx64 " try: %d", my_id, i + 1);
      clear_reply(current_reply);
      current_reply = nullptr;
      ctx.reset(nullptr); // Clean up previous attempt
    } else {
      break;
    }
  }

  return current_reply;
}

void
RedisPublisher::clear_reply(::redisReply *reply)
{
  if (reply) {
    ::freeReplyObject(reply);
  }
}

void
RedisPublisher::runWorker()
{
  m_endpointIndexMutex.lock();
  RedisEndpoint &my_endpoint(m_redisEndpoints[m_endpointIndex]);

  if (static_cast<int>(m_redisEndpoints.size() - 1) == m_endpointIndex) {
    m_endpointIndex = 0;
  } else {
    ++m_endpointIndex;
  }
  m_endpointIndexMutex.unlock();

  RedisContextPtr my_context;
  ::redisReply *current_reply(nullptr);

  while (!plugin_threads.shutdown) {
    try {
      ::sem_post(&m_workerSem);
      int readyWorkers = 0;
      ::sem_getvalue(&m_workerSem, &readyWorkers);

      // LOGDEBUG(PUB, "RedisPublisher::runWorker.Ready worker count: " << readyWorkers);

      m_messageQueueMutex.lock();

      if (m_messageQueue.empty()) {
        ::sem_wait(&m_workerSem);
        m_messageQueueMutex.unlock();
        std::unique_lock<std::mutex> lock(q_mutex);
        q_checker.wait(lock, [] { return q_ready; });
        q_ready = false;
        continue;
      }

      // Can't do reference here, since we pop it off the queue, the reference will be invalid.
      Message current_message(m_messageQueue.front());
      if (!current_message.cleanup) {
        m_messageQueue.pop_front();
      }

      m_messageQueueMutex.unlock();
      ::sem_wait(&m_workerSem);

      if (current_message.cleanup) {
        if (TSIsDebugTagSet(PLUGIN)) {
          auto my_id = static_cast<uint64_t>(pthread_self());
          TSDebug(PLUGIN, "RedisPublisher::runWorker: threadId: %" PRIx64 " received the cleanup message. Exiting!", my_id);
        }
        break;
      }
      current_reply = send_publish(my_context, my_endpoint, current_message);

      if (!current_reply) {
        current_message.hosts_tried.insert(my_endpoint);
        if (current_message.hosts_tried.size() < m_redisEndpoints.size()) {
          // all endpoints are not tried
          // someone else might be able to transmit
          m_messageQueueMutex.lock();
          if (!m_messageQueue.front().cleanup) {
            m_messageQueue.push_front(current_message);
          }
          m_messageQueueMutex.unlock();

          {
            std::lock_guard<std::mutex> lock(q_mutex);
            q_ready = true;
          }
          q_checker.notify_one();
        }
      }

      clear_reply(current_reply);
      current_reply = nullptr;
    } catch (...) {
      TSDebug(PLUGIN, "RedisPublisher::runWorker exception");
      break;
    }
  }
  my_context.reset(nullptr);
  clear_reply(current_reply);

  ::pthread_exit(nullptr);
}

int
RedisPublisher::publish(const std::string &channel, const std::string &data)
{
  TSDebug(PLUGIN, "RedisPublisher::publish: Publish request for channel: %s and message: \"%s\" received.", channel.c_str(),
          hex_str(data).c_str());

  m_messageQueueMutex.lock();

  m_messageQueue.emplace_back(channel, data);

  if (m_messageQueue.size() > m_maxQueuedMessages) {
    m_messageQueue.pop_front();
  }
  m_messageQueueMutex.unlock();

  {
    std::lock_guard<std::mutex> lock(q_mutex);
    q_ready = true;
  }
  q_checker.notify_one();

  return SUCCESS;
}

int
RedisPublisher::signal_cleanup()
{
  TSDebug(PLUGIN, "RedisPublisher::signal_cleanup: Called.");
  Message cleanup_message("", "", true);

  m_messageQueueMutex.lock();
  m_messageQueue.push_front(cleanup_message); // highest priority
  m_messageQueueMutex.unlock();

  {
    std::lock_guard<std::mutex> lock(q_mutex);
    q_ready = true;
  }
  q_checker.notify_one();

  return SUCCESS;
}

RedisPublisher::~RedisPublisher()
{
  TSDebug(PLUGIN, "RedisPublisher::~RedisPublisher: Called.");
  RedisPublisher::signal_cleanup();

  ::sem_destroy(&m_workerSem);
}

std::string
RedisPublisher::get_session(const std::string &channel)
{
  if (TSIsDebugTagSet(PLUGIN)) {
    auto my_id = static_cast<uint64_t>(pthread_self());
    TSDebug(PLUGIN, "RedisPublisher::get_session: Called by threadId: %" PRIx64, my_id);
  }

  std::string ret;
  uint32_t index    = get_hash_index(channel);
  redisReply *reply = nullptr;
  TSDebug(PLUGIN, "RedisPublisher::get_session: Start to try to get session.");
  for (uint32_t i = 0; i < m_redisEndpoints.size(); i++) {
    connection *conn = pools[index]->get();

    if (conn) {
      reply = static_cast<redisReply *>(redisCommand(conn->c_ptr(), "GET %s", channel.c_str()));
      if (reply && reply->type == REDIS_REPLY_STRING) {
        TSDebug(PLUGIN, "RedisPublisher::get_session: Success to GET a value from redis server index: %d", index);
        pools[index]->put(conn);
        ret = reply->str;
        clear_reply(reply);
        return ret;
      }
      pools[index]->put(conn);
      clear_reply(reply);
    }
    TSError("RedisPublisher::get_session: Fail to GET a value from this redis server index: %d", index);
    index = get_next_index(index);
    TSDebug(PLUGIN, "RedisPublisher::get_session: Will try the next redis server: %d", index);
  }

  TSError("RedisPublisher::get_session: Fail to GET a value from all redis servers!");
  return ret;
}

redisReply *
RedisPublisher::set_session(const Message &msg)
{
  if (TSIsDebugTagSet(PLUGIN)) {
    auto my_id = static_cast<uint64_t>(pthread_self());
    TSDebug(PLUGIN, "RedisPublisher::set_session: Called by threadId: %" PRIx64, my_id);
  }

  uint32_t index    = get_hash_index(msg.channel);
  redisReply *reply = nullptr;
  for (uint32_t i = 0; i < m_redisEndpoints.size(); i++) {
    connection *conn = pools[index]->get();

    if (conn) {
      reply = static_cast<redisReply *>(redisCommand(conn->c_ptr(), "SET %s %s", msg.channel.c_str(), msg.data.c_str()));
      if (reply && reply->type == REDIS_REPLY_STATUS && strcasecmp(reply->str, "OK") == 0) {
        TSDebug(PLUGIN, "RedisPublisher::set_session: Success to SET a value to redis server: %s:%d",
                m_redisEndpoints[index].m_hostname.c_str(), m_redisEndpoints[index].m_port);
        pools[index]->put(conn);
        return reply;
      }
      pools[index]->put(conn);
      clear_reply(reply);
    }
    TSError("RedisPublisher::set_session: Fail to SET a value to this redis server %s:%d",
            m_redisEndpoints[index].m_hostname.c_str(), m_redisEndpoints[index].m_port);

    index = get_next_index(index);
    TSDebug(PLUGIN, "RedisPublisher::set_session: Will try the next redis server: %s:%d",
            m_redisEndpoints[index].m_hostname.c_str(), m_redisEndpoints[index].m_port);
  }

  TSError("RedisPublisher::set_session: Fail to SET a value to all redis servers!");
  return nullptr;
}
