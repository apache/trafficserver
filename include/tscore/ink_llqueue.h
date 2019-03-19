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

#pragma once

/****************************************************************************


A simple linked list queue.
****************************************************************************/
#include "tscore/ink_mutex.h"
#include "tscore/ink_thread.h"

typedef struct llqrec_s {
  struct llqrec_s *next;
  void *data;
} LLQrec;

typedef struct llq_s {
  LLQrec *head, *tail, *free;
  uint64_t len, highwater;
  ink_mutex mux;
  ink_semaphore sema;
} LLQ;

LLQ *create_queue();
int enqueue(LLQ *q, void *data);
void *dequeue(LLQ *q);
bool queue_is_empty(LLQ *q);
uint64_t queue_len(LLQ *Q);
uint64_t queue_highwater(LLQ *Q);
void delete_queue(LLQ *Q); /* only deletes an empty queue but
                              provides symmetry. */
