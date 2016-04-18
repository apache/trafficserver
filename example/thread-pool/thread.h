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

#ifndef _THREAD_H_
#define _THREAD_H_

#define MAGIC_ALIVE 0xfeedbabe
#define MAGIC_DEAD 0xdeadbeef

/* If more than MAX_JOBS_ALARM are present in queue, the plugin
   will log error messages. This should be tuned based on your application */
#define MAX_JOBS_ALARM 1000

typedef int (*ExecFunc)(TSCont, void *);

/* Structure that contains all information for a job execution */
typedef struct {
  unsigned int magic;
  TSCont cont;   /* Continuation to call once job is done */
  ExecFunc func; /* Job function */
  void *data;    /* Any data to pass to the job function */
} Job;

/* Implementation of the queue for jobs */
struct cell_rec {
  unsigned int magic;
  void *ptr_data;
  struct cell_rec *ptr_next;
  struct cell_rec *ptr_prev;
};
typedef struct cell_rec Cell;

typedef struct {
  Cell *head;
  Cell *tail;
  int nb_elem;
  TSMutex mutex;
} Queue;

/* queue manipulation functions */
void init_queue(Queue *q);

void add_to_queue(Queue *q, void *data);

void *remove_from_queue(Queue *q);

int get_nbelem_queue(Queue *q);

/* Job functions */
Job *job_create(TSCont contp, ExecFunc func, void *data);

void job_delete(Job *job);

/* thread functions */
void thread_signal_job();

void thread_init();

void thread_loop(void *arg);

#endif
