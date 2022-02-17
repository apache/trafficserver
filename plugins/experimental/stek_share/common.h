/** @file

  common.h - Things that need to be everywhere

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

#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <mutex>
#include <deque>
#include <cmath>

#define PLUGIN "stek_share"

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
    shut_down = true;

    std::lock_guard<std::mutex> lock(threads_mutex);
    while (!threads_queue.empty()) {
      pthread_t th = threads_queue.front();
      ::pthread_join(th, nullptr);
      threads_queue.pop_front();
    }
  }

  bool
  is_shut_down()
  {
    return shut_down;
  }

private:
  bool shut_down = false;
  std::deque<pthread_t> threads_queue;
  std::mutex threads_mutex;
};

std::string hex_str(std::string const &str);
