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

#include "QUICApplication.h"

// TODO: add quic version option
struct QUICClientConfig {
  char addr[1024]       = "127.0.0.1";
  char output[1024]     = {0};
  char port[16]         = "4433";
  char path[1018]       = "/";
  char debug_tags[1024] = "quic";
  int close             = false;
};

class QUICClient : public Continuation
{
public:
  QUICClient(const QUICClientConfig *config);
  ~QUICClient();

  int start(int, void *);
  int state_http_server_open(int event, void *data);

private:
  const QUICClientConfig *_config    = nullptr;
  struct addrinfo *_remote_addr_info = nullptr;
};

class QUICClientApp : public QUICApplication
{
public:
  QUICClientApp(QUICNetVConnection *qvc, const QUICClientConfig *config);

  void start(const char *path);
  int main_event_handler(int event, Event *data);

private:
  const QUICClientConfig *_config = nullptr;
  const char *_filename           = nullptr;
};
