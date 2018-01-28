/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "P_Net.h"
#include "I_EventSystem.h"
#include "I_NetVConnection.h"
#include "P_QUICNetProcessor.h"

class QUICClient : public Continuation
{
public:
  QUICClient(const char *addr, const char *port) : Continuation(new_ProxyMutex()), _remote_addr(addr), _remote_port(port)
  {
    SET_HANDLER(&QUICClient::start);
  };
  ~QUICClient();

  void start();
  int state_http_server_open(int event, void *data);

private:
  const char *_remote_addr;
  const char *_remote_port;
  struct addrinfo *_remote_addr_info = nullptr;
};
