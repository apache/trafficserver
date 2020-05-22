/** @file

  Main function to start the HTTP Proxy Server

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

#include <atomic>
#include <mutex>
#include <condition_variable>

struct HttpProxyPort;

/// Perform any pre-thread start initialization.
void prep_HttpProxyServer();

/** Initialize all HTTP proxy port data structures needed to accept connections.
 */
void init_accept_HttpProxyServer(int n_accept_threads = 0);

/** Checkes whether we can call start_HttpProxyServer().
 */
void init_HttpProxyServer();

/** Start the proxy server.
    The port data should have been created by @c prep_HttpProxyServer().
*/
void start_HttpProxyServer();

void stop_HttpProxyServer();

NetProcessor::AcceptOptions make_net_accept_options(const HttpProxyPort *port, unsigned nthreads);

extern std::mutex proxyServerMutex;
extern std::condition_variable proxyServerCheck;
extern bool et_net_threads_ready;
