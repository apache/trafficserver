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

#include "request_queue.h"
#include "ats_fastcgi.h"
using namespace ats_plugin;

RequestQueue::RequestQueue()
{
  mutex                     = TSMutexCreate();
  FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
  max_queue_size            = gConfig->getRequestQueueSize();
}

RequestQueue::~RequestQueue()
{
  max_queue_size = 0;
  TSMutexDestroy(mutex);
}

uint
RequestQueue::isQueueFull()
{
  if (pending_list.size() >= max_queue_size) {
    return 1;
  } else {
    return 0;
  }
}

uint
RequestQueue::getSize()
{
  return pending_list.size();
}

uint
RequestQueue::isQueueEmpty()
{
  if (pending_list.empty()) {
    return 1;
  } else {
    return 0;
  }
}

uint
RequestQueue::addToQueue(ServerIntercept *intercept)
{
  // TSMutexLock(mutex);
  if (!isQueueFull()) {
    pending_list.push(intercept);
  }
  // TODO: handle queue full use case
  // TSMutexUnlock(mutex);
  return 1;
}

ServerIntercept *
RequestQueue::popFromQueue()
{
  ServerIntercept *intercept = nullptr;
  // TSMutexLock(mutex);
  if (!pending_list.empty()) {
    intercept = pending_list.front();
    pending_list.pop();
  }
  // TSMutexUnlock(mutex);
  return intercept;
}
