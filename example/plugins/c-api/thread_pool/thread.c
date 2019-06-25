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

#include <stdio.h>
#include <pthread.h>
#include "ts/ts.h"

#include "thread.h"
#include "tscore/ink_defs.h"

struct timespec tp1;
struct timespec tp2;

Queue job_queue;

static pthread_cond_t cond;
static pthread_mutex_t cond_mutex;

void
init_queue(Queue *q)
{
  q->head    = NULL; /* Pointer on head cell */
  q->tail    = NULL; /* Pointer on tail cell */
  q->nb_elem = 0;    /* Nb elem in the queue */
  q->mutex   = TSMutexCreate();
}

void
add_to_queue(Queue *q, void *data)
{
  if (data != NULL) {
    TSMutexLock(q->mutex);
    /* Init the new cell */
    Cell *new_cell     = TSmalloc(sizeof(Cell));
    new_cell->magic    = MAGIC_ALIVE;
    new_cell->ptr_data = data;
    new_cell->ptr_next = q->tail;
    new_cell->ptr_prev = NULL;

    /* Add this new cell to the queue */
    if (q->tail == NULL) {
      TSAssert(q->head == NULL);
      TSAssert(q->nb_elem == 0);
      q->tail = new_cell;
      q->head = new_cell;
    } else {
      TSAssert(q->tail->magic == MAGIC_ALIVE);
      q->tail->ptr_prev = new_cell;
      q->tail           = new_cell;
    }
    int n = q->nb_elem++;
    TSMutexUnlock(q->mutex);

    if (n > MAX_JOBS_ALARM) {
      TSError("[%s] Warning:Too many jobs in plugin thread pool queue (%d). Maximum allowed is %d", PLUGIN_NAME, n, MAX_JOBS_ALARM);
    }
  }
}

void *
remove_from_queue(Queue *q)
{
  void *data = NULL;
  Cell *remove_cell;

  TSMutexLock(q->mutex);
  if (q->nb_elem > 0) {
    remove_cell = q->head;
    TSAssert(remove_cell->magic == MAGIC_ALIVE);

    data    = remove_cell->ptr_data;
    q->head = remove_cell->ptr_prev;
    if (q->head == NULL) {
      TSAssert(q->nb_elem == 1);
      q->tail = NULL;
    } else {
      TSAssert(q->head->magic == MAGIC_ALIVE);
      q->head->ptr_next = NULL;
    }

    remove_cell->magic = MAGIC_DEAD;
    TSfree(remove_cell);
    q->nb_elem--;
  }
  TSMutexUnlock(q->mutex);
  return data;
}

int
get_nbelem_queue(Queue *q)
{
  int nb;
  TSMutexLock(q->mutex);
  nb = q->nb_elem;
  TSMutexUnlock(q->mutex);

  return nb;
}

Job *
job_create(TSCont contp, ExecFunc func, void *data)
{
  Job *new_job;

  new_job        = TSmalloc(sizeof(Job));
  new_job->magic = MAGIC_ALIVE;
  new_job->cont  = contp;
  new_job->func  = func;
  new_job->data  = data;
  return new_job;
}

void
job_delete(Job *job)
{
  job->magic = MAGIC_DEAD;
  TSfree(job);
}

void
thread_signal_job()
{
  pthread_mutex_lock(&cond_mutex);
  pthread_cond_broadcast(&cond);
  pthread_mutex_unlock(&cond_mutex);
}

void
thread_init()
{
  pthread_cond_init(&cond, NULL);
}

void *
thread_loop(void *arg ATS_UNUSED)
{
  /* Infinite loop */
  for (;;) {
    /* returns a job or NULL if no jobs to do */
    Job *job_todo = remove_from_queue(&job_queue);

    if (job_todo != NULL) {
      TSAssert(job_todo->magic == MAGIC_ALIVE);

      /* Simply execute the job function */
      job_todo->func(job_todo->cont, job_todo->data);

      /* Destroy this job */
      job_delete(job_todo);
    } else {
      /* Sleep until we get awake (probably some work to do) */
      pthread_mutex_lock(&cond_mutex);
      pthread_cond_wait(&cond, &cond_mutex);
      pthread_mutex_unlock(&cond_mutex);
    }
  }
  return NULL;
}
