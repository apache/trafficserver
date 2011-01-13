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

#ifndef _SIMPLE_QUEUE_H_
#define _SIMPLE_QUEUE_H_

/****************************************************************************
 *
 *  SimpleQueue.h - a thread safe queue
 *
 *
 ****************************************************************************/

#include <stdio.h>
#include "ink_mutex.h"
#include "ink_thread.h"
#include "ink_bool.h"

struct SimpleQueueEntry
{
  SimpleQueueEntry *next;
  void *data;
  SimpleQueueEntry *prev;
};

class SimpleQueue
{
public:
  SimpleQueue();
  ~SimpleQueue();
  void enqueue(void *);
  void *dequeue();

  void push(void *);
  void *pop();

  bool isEmpty();
  void Print();
private:
    ink_mutex accessLock;
#if defined(darwin)
  ink_sem *waitSema;
#else
  ink_sem waitSema;
#endif
  SimpleQueueEntry *head;
  SimpleQueueEntry *tail;
};


#endif
