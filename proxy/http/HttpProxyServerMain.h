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

struct HttpProxyPort;

void init_HttpProxyServer(void);

/** Start the proxy server. 
    The ports are contained in the HttpProxyPort global data.
*/
void start_HttpProxyServer(int accept_threads = 0);

void start_HttpProxyServerBackDoor(int port, int accept_threads = 0);

bool start_HttpProxyPort(const HttpProxyPort& port, unsigned nthreads);
