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

#include "I_Tasks.h"

// Globals
EventType ET_TASK = ET_CALL;
TasksProcessor tasksProcessor;

// Note that if the number of task_threads is 0, all continuations scheduled for
// ET_TASK ends up running on ET_CALL (which is the net-threads).
int
TasksProcessor::start(int task_threads, size_t stacksize)
{
  if (task_threads > 0) {
    ET_TASK = eventProcessor.spawn_event_threads(task_threads, "ET_TASK", stacksize);
  }
  return 0;
}
