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

#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>

#include "timers.h"


#define HASH_SIZE 67
static Timer *timers[HASH_SIZE];
static Timer *free_timers = (Timer *) 0;
static long mstimeout_cache = -1;

ClientData JunkClientData;



static unsigned int
hash(Timer * t)
{
  /* We can hash on the trigger time, even though it can change over
   ** the life of a timer via either the periodic bit or the tmr_reset()
   ** call.  This is because both of those guys call l_resort(), which
   ** recomputes the hash and moves the timer to the appropriate list.
   */
  return ((unsigned int) t->time.tv_sec ^ (unsigned int) t->time.tv_usec) % HASH_SIZE;
}


static void
l_add(Timer * t)
{
  int h = t->hash;
  register Timer *t2;
  register Timer *t2prev;

  t2 = timers[h];
  if (t2 == (Timer *) 0) {
    /* The list is empty. */
    timers[h] = t;
    t->prev = t->next = (Timer *) 0;
  } else {
    if (t->time.tv_sec < t2->time.tv_sec || (t->time.tv_sec == t2->time.tv_sec && t->time.tv_usec <= t2->time.tv_usec)) {
      /* The new timer goes at the head of the list. */
      timers[h] = t;
      t->prev = (Timer *) 0;
      t->next = t2;
      t2->prev = t;
    } else {
      /* Walk the list to find the insertion point. */
      for (t2prev = t2, t2 = t2->next; t2 != (Timer *) 0; t2prev = t2, t2 = t2->next) {
        if (t->time.tv_sec < t2->time.tv_sec ||
            (t->time.tv_sec == t2->time.tv_sec && t->time.tv_usec <= t2->time.tv_usec)) {
          /* Found it. */
          t2prev->next = t;
          t->prev = t2prev;
          t->next = t2;
          t2->prev = t;
          return;
        }
      }
      /* Oops, got to the end of the list.  Add to tail. */
      t2prev->next = t;
      t->prev = t2prev;
      t->next = (Timer *) 0;
    }
  }
}


static void
l_remove(Timer * t)
{
  int h = t->hash;

  if (t->prev == (Timer *) 0)
    timers[h] = t->next;
  else
    t->prev->next = t->next;
  if (t->next != (Timer *) 0)
    t->next->prev = t->prev;
}


static void
l_resort(Timer * t)
{
  /* Remove the timer from its old list. */
  l_remove(t);
  /* Recompute the hash. */
  t->hash = hash(t);
  /* And add it back in to its new list, sorted correctly. */
  l_add(t);
}


void
tmr_init(void)
{
  int h;

  mstimeout_cache = -1;
  for (h = 0; h < HASH_SIZE; ++h)
    timers[h] = (Timer *) 0;
}


Timer *
tmr_create(struct timeval *nowP, TimerProc * timer_proc, ClientData client_data, long msecs, int periodic)
{
  Timer *t;

  if (free_timers != (Timer *) 0) {
    t = free_timers;
    free_timers = t->next;
  } else {
    t = (Timer *) malloc(sizeof(Timer));
    if (t == (Timer *) 0)
      return (Timer *) 0;
  }

  mstimeout_cache = -1;
  t->timer_proc = timer_proc;
  t->client_data = client_data;
  t->msecs = msecs;
  t->periodic = periodic;
  if (nowP != (struct timeval *) 0)
    t->time = *nowP;
  else
    (void) gettimeofday(&t->time, (struct timezone *) 0);
  t->time.tv_sec += msecs / 1000L;
  t->time.tv_usec += (msecs % 1000L) * 1000L;
  if (t->time.tv_usec >= 1000000L) {
    t->time.tv_sec += t->time.tv_usec / 1000000L;
    t->time.tv_usec %= 1000000L;
  }
  t->hash = hash(t);
  /* Add the new timer to the proper active list. */
  l_add(t);

  return t;
}


struct timeval *
tmr_timeout(struct timeval *nowP)
{
  long msecs;
  static struct timeval timeout;

  msecs = tmr_mstimeout(nowP);
  if (msecs == INFTIM)
    return (struct timeval *) 0;
  timeout.tv_sec = msecs / 1000L;
  timeout.tv_usec = (msecs % 1000L) * 1000L;
  return &timeout;
}


long
tmr_mstimeout(struct timeval *nowP)
{
  if (mstimeout_cache > -1) {
    return mstimeout_cache;
  } else {
    int h;
    int gotone;
    long msecs, m;
    register Timer *t;

    gotone = 0;
    msecs = 0;                  /* make lint happy */
    /* Since the lists are sorted, we only need to look at the
     ** first timer on each one.
     */
    for (h = 0; h < HASH_SIZE; ++h) {
      t = timers[h];
      if (t != (Timer *) 0) {
        m = (t->time.tv_sec - nowP->tv_sec) * 1000L + (t->time.tv_usec - nowP->tv_usec) / 1000L;
        if (!gotone) {
          msecs = m;
          gotone = 1;
        } else if (m < msecs)
          msecs = m;
      }
    }
    if (!gotone)
      return INFTIM;
    if (msecs <= 0)
      msecs = 0;
    mstimeout_cache = msecs;

    return msecs;
  }
}


void
tmr_run(struct timeval *nowP)
{
  int h;
  Timer *t;
  Timer *next;

  for (h = 0; h < HASH_SIZE; ++h)
    for (t = timers[h]; t != (Timer *) 0; t = next) {
      next = t->next;
      /* Since the lists are sorted, as soon as we find a timer
       ** that isn't ready yet, we can go on to the next list.
       */
      if (t->time.tv_sec > nowP->tv_sec || (t->time.tv_sec == nowP->tv_sec && t->time.tv_usec > nowP->tv_usec))
        break;

      /* Invalidate mstimeout cache, since we're modifying the queue */
      mstimeout_cache = -1;

      (t->timer_proc) (t->client_data, nowP);
      if (t->periodic) {
        /* Reschedule. */
        t->time.tv_sec += t->msecs / 1000L;
        t->time.tv_usec += (t->msecs % 1000L) * 1000L;
        if (t->time.tv_usec >= 1000000L) {
          t->time.tv_sec += t->time.tv_usec / 1000000L;
          t->time.tv_usec %= 1000000L;
        }
        l_resort(t);
      } else
        tmr_cancel(t);
    }
}


void
tmr_reset(struct timeval *nowP, Timer * t)
{
  mstimeout_cache = -1;
  t->time = *nowP;
  t->time.tv_sec += t->msecs / 1000L;
  t->time.tv_usec += (t->msecs % 1000L) * 1000L;
  if (t->time.tv_usec >= 1000000L) {
    t->time.tv_sec += t->time.tv_usec / 1000000L;
    t->time.tv_usec %= 1000000L;
  }
  l_resort(t);
}


void
tmr_cancel(Timer * t)
{
  mstimeout_cache = -1;
  /* Remove it from its active list. */
  l_remove(t);
  /* And put it on the free list. */
  t->next = free_timers;
  free_timers = t;
  t->prev = (Timer *) 0;
}


void
tmr_cleanup(void)
{
  Timer *t;

  mstimeout_cache = -1;
  while (free_timers != (Timer *) 0) {
    t = free_timers;
    free_timers = t->next;
    free((void *) t);
  }
}


void
tmr_destroy(void)
{
  int h;

  mstimeout_cache = -1;
  for (h = 0; h < HASH_SIZE; ++h)
    while (timers[h] != (Timer *) 0)
      tmr_cancel(timers[h]);
  tmr_cleanup();

}
