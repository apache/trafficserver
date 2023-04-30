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

/**************************************************************************
  Signal functions and handlers.

**************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/signals.h"
#include "tscore/ink_stack_trace.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_thread.h"
#include "tscore/Diags.h"

static void
set_signal(int signo, signal_handler_t handler)
{
  struct sigaction act;

  act.sa_handler   = nullptr;
  act.sa_sigaction = handler;
  act.sa_flags     = SA_SIGINFO;
  sigemptyset(&(act.sa_mask));

  ink_release_assert(sigaction(signo, &act, nullptr) == 0);
}

// Reset a signal handler to the default handler.
static void
signal_reset_default(int signo)
{
  struct sigaction act;

  act.sa_handler = SIG_DFL;
  act.sa_flags   = SA_NODEFER | SA_ONSTACK | SA_RESETHAND;
  sigemptyset(&(act.sa_mask));

  ink_release_assert(sigaction(signo, &act, nullptr) == 0);
}

bool
signal_is_masked(int signo)
{
  sigset_t current;

  sigemptyset(&current);
  if (ink_thread_sigsetmask(SIG_SETMASK, nullptr /* oldset */, &current) == 0) {
    return sigismember(&current, signo) == 1;
  }

  return false;
}

bool
signal_is_crash(int signo)
{
  switch (signo) {
  case SIGILL:
  case SIGTRAP:
#if defined(SIGEMT)
  case SIGEMT:
#endif
#if defined(SIGSYS)
  case SIGSYS:
#endif
  case SIGFPE:
  case SIGBUS:
  case SIGXCPU:
  case SIGXFSZ:
  case SIGSEGV:
  case SIGABRT:
    return true;
  default:
    return false;
  }
}

void
signal_format_siginfo(int signo, siginfo_t *info, const char *msg)
{
  (void)info;
  (void)signo;

  char buf[64];

#if HAVE_STRSIGNAL
  snprintf(buf, sizeof(buf), "%s: received signal %d (%s)\n", msg, signo, strsignal(signo));
#else
  snprintf(buf, sizeof(buf), "%s: received signal %d\n", msg, signo);
#endif

  ssize_t ignored = write(STDERR_FILENO, buf, strlen(buf));
  (void)ignored; // because gcc and glibc are stupid, "(void)write(...)" doesn't suffice.
}

void
signal_crash_handler(int signo, siginfo_t *, void *)
{
  ink_stack_trace_dump();

  // Make sure to drop a core for signals that normally would do so.
  signal_reset_default(signo);
  raise(signo);
}

void
signal_register_crash_handler(signal_handler_t handler)
{
  set_signal(SIGBUS, handler);
  set_signal(SIGSEGV, handler);
  set_signal(SIGILL, handler);
  set_signal(SIGTRAP, handler);
  set_signal(SIGFPE, handler);
  set_signal(SIGABRT, handler);
}

void
signal_register_default_handler(signal_handler_t handler)
{
  sigset_t sigsToBlock;

  sigemptyset(&sigsToBlock);
  ink_thread_sigsetmask(SIG_SETMASK, &sigsToBlock, nullptr);

  // SIGPIPE is just annoying to handle,we never care about it
  signal(SIGPIPE, SIG_IGN);

  // SIGHUP ought to reconfigure, but it's surprisingly complex to figure out
  // how to do that. so leave that to libmgmt.
  signal(SIGHUP, SIG_IGN);

  set_signal(SIGQUIT, handler);
  set_signal(SIGTERM, handler);
  set_signal(SIGINT, handler);
  set_signal(SIGUSR1, handler);
  set_signal(SIGUSR2, handler);
}
