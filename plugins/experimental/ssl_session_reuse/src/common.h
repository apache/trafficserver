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

#define PLUGIN "ssl_session_reuse"

// Base 64 encoding takes 4*(ceil(n/3)) bytes
#define ENCODED_LEN(len) (((int)ceil(1.34 * (len) + 5)) + 1)
#define DECODED_LEN(len) (((int)ceil(0.75 * (len))) + 1)
// 3DES encryption will take at most 8 extra bytes.  Plus we base 64 encode the result
#define ENCRYPT_LEN(len) ((int)ceil(1.34 * ((len) + 8) + 5) + 1)
#define DECRYPT_LEN(len) ((int)ceil(1.34 * ((len) + 8) + 5) + 1)

class PluginThreads
{
public:
  bool shutdown = false;

  void
  store(const pthread_t &th)
  {
    std::lock_guard<std::mutex> lock(threads_mutex);
    threads_queue.push_back(th);
  }

  void
  terminate()
  {
    shutdown = true;

    std::lock_guard<std::mutex> lock(threads_mutex);
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

std::string hex_str(std::string const &str);

int encrypt_encode64(const unsigned char *key, int key_length, const unsigned char *in_data, int in_data_len, char *out_data,
                     size_t out_data_size, size_t *out_data_len);

int decrypt_decode64(const unsigned char *key, int key_length, const char *in_data, int in_data_len, unsigned char *out_data,
                     size_t out_data_size, size_t *out_data_len);

extern const unsigned char salt[];

extern PluginThreads plugin_threads;
