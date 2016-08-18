/** @file

  Implementation of a simple linked list queue

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

#include "ts/ink_config.h"
#include "ts/ink_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>

#include "ts/ink_llqueue.h"
#include "errno.h"

#define RECORD_CHUNK 1024

// These are obviously not used anywhere, I don't know if or how they
// were supposed to work, but #ifdef'ing them out of here for now.
#ifdef NOT_USED_HERE
static LLQrec *
newrec(LLQ *Q)
{
  LLQrec *new_val;
  int i;

  if (Q->free != NULL) {
    new_val = Q->free;
    Q->free = Q->free->next;
    return new_val;
  }

  Q->free = (LLQrec *)ats_malloc(RECORD_CHUNK * sizeof(LLQrec));
  for (i            = 0; i < RECORD_CHUNK; i++)
    Q->free[i].next = &Q->free[i + 1];

  Q->free[RECORD_CHUNK - 1].next = NULL;

  new_val = Q->free;
  Q->free = Q->free->next;

  return new_val;
}

// Not used either ...
static void
freerec(LLQ *Q, LLQrec *rec)
{
  rec->next = Q->free;
  Q->free   = rec;
}
#endif

LLQ *
create_queue()
{
  LLQ *new_val = (LLQ *)ats_malloc(sizeof(LLQ));

  ink_sem_init(&(new_val->sema), 0);
  ink_mutex_init(&(new_val->mux), "LLQ::create_queue");

  new_val->head = new_val->tail = new_val->free = NULL;
  new_val->len = new_val->highwater = 0;

  return new_val;
}

// matching delete function, only for empty queue!
void
delete_queue(LLQ *Q)
{
  // There seems to have been some ideas of making sure that this queue is
  // actually empty ...
  //
  //    LLQrec * qrec;
  ink_sem_destroy(&(Q->sema));
  ink_mutex_destroy(&(Q->mux));
  ats_free(Q);
  return;
}

int
enqueue(LLQ *Q, void *data)
{
  LLQrec *new_val;

  ink_mutex_acquire(&(Q->mux));
  new_val       = (LLQrec *)ats_malloc(sizeof(LLQrec));
  new_val->data = data;
  new_val->next = NULL;

  if (Q->tail)
    Q->tail->next = new_val;
  Q->tail         = new_val;

  if (Q->head == NULL)
    Q->head = Q->tail;

  Q->len++;
  if (Q->len > Q->highwater)
    Q->highwater = Q->len;
  ink_mutex_release(&(Q->mux));
  ink_sem_post(&(Q->sema));
  return 1;
}

uint64_t
queue_len(LLQ *Q)
{
  uint64_t len;

  /* Do I really need to grab the lock here? */
  /* ink_mutex_acquire(&(Q->mux)); */
  len = Q->len;
  /* ink_mutex_release(&(Q->mux)); */
  return len;
}

uint64_t
queue_highwater(LLQ *Q)
{
  uint64_t highwater;

  /* Do I really need to grab the lock here? */
  /* ink_mutex_acquire(&(Q->mux)); */
  highwater = Q->highwater;
  /* ink_mutex_release(&(Q->mux)); */
  return highwater;
}

/*
 *---------------------------------------------------------------------------
 *
 * queue_is_empty
 *
 *  Is the queue empty?
 *
 * Results:
 *  nonzero if empty, zero else.
 *
 * Side Effects:
 *  none.
 *
 * Reentrancy:     n/a.
 * Thread Safety:  safe.
 * Mem Management: n/a.
 *
 *---------------------------------------------------------------------------
 */
bool
queue_is_empty(LLQ *Q)
{
  uint64_t len;

  len = queue_len(Q);

  return len == 0;
}

void *
dequeue(LLQ *Q)
{
  LLQrec *rec;
  void *d;
  ink_sem_wait(&(Q->sema));
  ink_mutex_acquire(&(Q->mux));

  if (Q->head == NULL) {
    ink_mutex_release(&(Q->mux));

    return NULL;
  }

  rec = Q->head;

  Q->head = Q->head->next;
  if (Q->head == NULL)
    Q->tail = NULL;

  d = rec->data;
  // freerec(Q, rec);
  ats_free(rec);

  Q->len--;
  ink_mutex_release(&(Q->mux));

  return d;
}

#ifdef LLQUEUE_MAIN

void *
testfun(void *unused)
{
  int num;
  LLQ *Q;

  Q = create_queue();
  assert(Q);

  do {
    scanf("%d", &num);
    if (num == 0) {
      printf("DEQUEUE: %d\n", (int)dequeue(Q));
    } else if (num == -1) {
      printf("queue_is_empty: %d\n", queue_is_empty(Q));
    } else {
      printf("enqueue: %d\n", num);
      enqueue(Q, (void *)num);
    }
  } while (num >= -1);

  return NULL;
}

/*
 * test harness-- hit Ctrl-C if it blocks or you get tired.
 */
void
main()
{
  assert(thr_create(NULL, 0, testfun, (void *)NULL, THR_NEW_LWP, NULL) == 0);
  while (1) {
    ;
  }
}

#endif
