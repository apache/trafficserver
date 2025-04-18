/** @file

  A brief file description

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

#include "iocore/eventsystem/Tasks.h"
#include "iocore/eventsystem/EventProcessor.h"

// Globals
EventType      ET_TASK = ET_CALL;
TasksProcessor tasksProcessor;

EventType
TasksProcessor::register_event_type()
{
  ET_TASK = eventProcessor.register_event_type("ET_TASK");
  return ET_TASK;
}

// Note that if the number of task_threads is 0, all continuations scheduled for
// ET_TASK ends up running on ET_CALL (which is the net-threads).
int
TasksProcessor::start(int task_threads, size_t stacksize)
{
  eventProcessor.spawn_event_threads(ET_TASK, std::max(1, task_threads), stacksize);
  return 0;
}
