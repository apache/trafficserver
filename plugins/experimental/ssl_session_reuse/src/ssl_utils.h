/** @file

  ssl_utils.h - a containuer of connection objects

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

#include <openssl/ssl.h>
#include <string>
#include <ts/ts.h>
#include <mutex>
#include <deque>

#include "publisher.h"
#include "subscriber.h"

#include "stek.h"

struct ssl_session_param {
  std::string cluster_name;
  int key_update_interval;         // STEK master rotation period seconds
  int stek_master;                 // bool - Am I the STEK setter/rotator for POD?
  ssl_ticket_key_t ticket_keys[2]; // current and past STEK
  std::string redis_auth_key_file;
  RedisPublisher *pub = nullptr;
  RedisSubscriber *sub;

  ssl_session_param();
  ~ssl_session_param();
};

class PluginThreads
{
public:
  void
  store(const pthread_t &th)
  {
    std::lock_guard<std::mutex> lock(threads_mutex);
    threads_queue.push_back(th);
  }

  void
  terminate()
  {
    std::lock_guard<std::mutex> lock(threads_mutex);
    for (pthread_t th : threads_queue) {
      ::pthread_cancel(th);
    }
    while (!threads_queue.empty()) {
      pthread_t th = threads_queue.front();
      ::pthread_join(th, nullptr);
      threads_queue.pop_front();
    }
  }

private:
  std::deque<pthread_t> threads_queue;
  std::mutex threads_mutex;
};

int STEK_init_keys();

const char *get_key_ptr();
int get_key_length();

/* Initialize ssl parameters */
/**
   Return the result of initialization. If 0 is returned, it means
   the initializtion is success, -1 means it is failure.

   @param conf_file the configuration file

   @return @c 0 if it is success.

*/
int init_ssl_params(const std::string &conf_file);
int init_subscriber();

int SSL_session_callback(TSCont contp, TSEvent event, void *edata);

extern ssl_session_param ssl_param; // almost everything one needs is stored in here

extern PluginThreads plugin_threads;
