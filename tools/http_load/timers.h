/* timers.h - header file for timers package
**
** Copyright � 1995,1998,1999,2000 by Jef Poskanzer <jef@mail.acme.com>.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

#pragma once

#include <sys/time.h>

#ifndef INFTIM
#define INFTIM -1
#endif /* INFTIM */

/* ClientData is a random value that tags along with a timer.  The client
** can use it for whatever, and it gets passed to the callback when the
** timer triggers.
*/
typedef union {
  void *p;
  int i;
  long l;
} ClientData;

extern ClientData JunkClientData; /* for use when you don't care */

/* The TimerProc gets called when the timer expires.  It gets passed
** the ClientData associated with the timer, and a timeval in case
** it wants to schedule another timer.
*/
typedef void TimerProc(ClientData client_data, struct timeval *nowP);

/* The Timer struct. */
typedef struct TimerStruct {
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
extern Timer *tmr_create(struct timeval *nowP, TimerProc *timer_proc, ClientData client_data, long msecs, int periodic);

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
extern void tmr_reset(struct timeval *nowP, Timer *timer);

/* Deschedule a timer.  Note that non-periodic timers are automatically
** descheduled when they run, so you don't have to call this on them.
*/
extern void tmr_cancel(Timer *timer);

/* Clean up the timers package, freeing any unused storage. */
extern void tmr_cleanup(void);

/* Cancel all timers and free storage, usually in preparation for exitting. */
extern void tmr_destroy(void);
