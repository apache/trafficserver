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
#include <condition_variable>
#include <chrono>
#include <deque>
#include <cmath>
#include <atomic>

#include <ts/ts.h>

#define PLUGIN_NAME "stek_share"

class PluginThreads
{
public:
  void
  store(TSThread th)
  {
    std::lock_guard<std::mutex> lock(threads_mutex);
    threads_queue.push_back(th);
  }

  // TSThreadCreate starts its threads detached, so wait on each handle with
  // TSThreadWait rather than pthread_join. The handles are not destroyed: the
  // process is exiting, and the thread can still touch its handle briefly
  // after TSThreadWait returns.
  void
  terminate()
  {
    {
      std::lock_guard<std::mutex> lock(sleep_mutex);
      shut_down = true;
    }
    sleep_cv.notify_all();

    std::lock_guard<std::mutex> lock(threads_mutex);
    for (TSThread th : threads_queue) {
      TSThreadWait(th);
    }
  }

  bool
  is_shut_down()
  {
    return shut_down;
  }

  // Like std::this_thread::sleep_for, but terminate() wakes it early so
  // shutdown does not wait out a long sleep.
  void
  sleep_for(std::chrono::milliseconds duration)
  {
    std::unique_lock<std::mutex> lock(sleep_mutex);
    sleep_cv.wait_for(lock, duration, [this] { return shut_down.load(); });
  }

private:
  std::atomic<bool>       shut_down = false;
  std::deque<TSThread>    threads_queue;
  std::mutex              threads_mutex;
  std::mutex              sleep_mutex;
  std::condition_variable sleep_cv;
};

std::string hex_str(std::string const &str);
