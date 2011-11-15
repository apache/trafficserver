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

#ifndef _TIMERS_H_
#define _TIMERS_H_

#include <sys/time.h>

#ifndef INFTIM
#define INFTIM -1
#endif /* INFTIM */

/* ClientData is a random value that tags along with a timer.  The client
** can use it for whatever, and it gets passed to the callback when the
** timer triggers.
*/
typedef union
{
  void *p;
  int i;
  long l;
} ClientData;

extern ClientData JunkClientData;       /* for use when you don't care */

/* The TimerProc gets called when the timer expires.  It gets passed
** the ClientData associated with the timer, and a timeval in case
** it wants to schedule another timer.
*/
typedef void TimerProc(ClientData client_data, struct timeval *nowP);

/* The Timer struct. */
typedef struct TimerStruct
{
  TimerProc *timer_proc;
  ClientData client_data;
  long msecs;
  int periodic;
  struct timeval time;
  struct TimerStruct *prev;
  struct TimerStruct *next;
  int hash;
} Timer;

/* Initialize the timer package. */
extern void tmr_init(void);

/* Set up a timer, either periodic or one-shot. Returns (Timer*) 0 on errors. */
extern Timer *tmr_create(struct timeval *nowP, TimerProc * timer_proc, ClientData client_data,
                         long msecs, int periodic);

/* Returns a timeout indicating how long until the next timer triggers.  You
** can just put the call to this routine right in your select().  Returns
** (struct timeval*) 0 if no timers are pending.
*/
extern struct timeval *tmr_timeout(struct timeval *nowP);

/* Returns a timeout in milliseconds indicating how long until the next timer
** triggers.  You can just put the call to this routine right in your poll().
** Returns INFTIM (-1) if no timers are pending.
*/
extern long tmr_mstimeout(struct timeval *nowP);

/* Run the list of timers. Your main program needs to call this every so often,
** or as indicated by tmr_timeout().
*/
extern void tmr_run(struct timeval *nowP);

/* Reset the clock on a timer, to current time plus the original timeout. */
extern void tmr_reset(struct timeval *nowP, Timer * timer);

/* Deschedule a timer.  Note that non-periodic timers are automatically
** descheduled when they run, so you don't have to call this on them.
*/
extern void tmr_cancel(Timer * timer);

/* Clean up the timers package, freeing any unused storage. */
extern void tmr_cleanup(void);

/* Cancel all timers and free storage, usually in preparation for exitting. */
extern void tmr_destroy(void);

#endif /* _TIMERS_H_ */
