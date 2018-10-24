/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <queue>
#include "server_intercept.h"
#include "fcgi_config.h"
#include <ts/ts.h>
namespace ats_plugin
{
class ServerIntercept;
class RequestQueue
{
  TSMutex mutex;
  uint max_queue_size;
  std::queue<ServerIntercept *> pending_list;

public:
  RequestQueue();
  ~RequestQueue();

  uint isQueueFull();
  uint isQueueEmpty();
  uint getSize();
  uint addToQueue(ServerIntercept *);
  ServerIntercept *popFromQueue();
};
} // namespace ats_plugin
