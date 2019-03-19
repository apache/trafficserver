/** @file

  publisher.h - Redis publisher

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

#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <memory>
#include <semaphore.h>
#include <hiredis/hiredis.h>
#include "message.h"
#include "globals.h"
#include "redis_endpoint.h"
#include "simple_pool.h"
#include "tscore/HashFNV.h"

struct RedisContextDeleter {
  void
  operator()(::redisContext *ctx)
  {
    ::redisFree(ctx);
  }
};

typedef std::unique_ptr<::redisContext, RedisContextDeleter> RedisContextPtr;

class RedisPublisher
{
  std::string redis_passwd;
  std::deque<Message> m_messageQueue;
  std::mutex m_messageQueueMutex;
  ::sem_t m_workerSem;

  std::vector<RedisEndpoint> m_redisEndpoints;
  std::string m_redisEndpointsStr;
  int m_endpointIndex = 0;
  std::mutex m_endpointIndexMutex;

  std::vector<simple_pool *> pools;

  unsigned int m_numWorkers;
  unsigned int m_redisConnectTimeout; // milliseconds
  unsigned int m_redisConnectTries;
  unsigned int m_redisPublishTries;
  unsigned int m_redisRetryDelay; // milliseconds
  unsigned int m_maxQueuedMessages;
  unsigned int m_poolRedisConnectTimeout; // milliseconds

  bool err = false;

  void runWorker();

  ::redisContext *setup_connection(const RedisEndpoint &re);

  ::redisReply *send_publish(RedisContextPtr &ctx, const RedisEndpoint &re, const Message &msg);
  ::redisReply *set_session(const Message &msg);
  void clear_reply(::redisReply *reply);

  uint32_t
  get_hash_index(const std::string &str) const
  {
    ATSHash32FNV1a hashFNV;
    hashFNV.update(str.c_str(), str.length());
    return hashFNV.get();
  }

  uint32_t
  get_next_index(uint32_t index) const
  {
    return (index + 1) % m_redisEndpoints.size();
  }

  int signal_cleanup();
  static void *start_worker_thread(void *arg);

public:
  RedisPublisher(const std::string &conf = cDefaultConfig);
  virtual ~RedisPublisher();
  int publish(const std::string &channel, const std::string &message);
  std::string get_session(const std::string &channel);
  bool is_good();
};
