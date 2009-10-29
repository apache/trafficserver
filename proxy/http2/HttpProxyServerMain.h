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

void init_HttpProxyServer(void);

/**
  fd is either a file descriptor which has already been opened for
  the purpose of accepting proxy connections, or NO_FD (-1) if a file
  descriptor should be opened.

*/
void start_HttpProxyServer(int fd, int port, int ssl_fd = NO_FD);

void start_HttpProxyServerBackDoor(int port);
