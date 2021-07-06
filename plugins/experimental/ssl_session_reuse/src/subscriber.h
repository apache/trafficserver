/** @file

  subscriber.h - a containuer of connection objects

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

#include <vector>
#include <string>
#include <atomic>

#include "message.h"
#include "globals.h"
#include "redis_endpoint.h"

class RedisSubscriber
{
private:
  std::string redis_passwd;

  std::vector<RedisEndpoint> m_redisEndpoints;
  std::atomic<int> m_redisEndpointsIndex;
  std::string m_channel;
  std::string m_channel_prefix;

  unsigned int m_redisConnectTimeout; // milliseconds
  unsigned int m_redisRetryDelay;     // milliseconds

  bool err;

  ::redisContext *setup_connection(int index);

  friend void *start(void *arg);

public:
  void run();
  RedisSubscriber(const std::string &conf = cDefaultConfig);
  virtual ~RedisSubscriber();
  bool is_good();
  int get_endpoint_index();
};
