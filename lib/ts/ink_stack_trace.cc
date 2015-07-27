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

#include "ts/ink_platform.h"
#include "ts/ink_stack_trace.h"
#include "ts/ink_args.h"

#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#if TS_HAS_BACKTRACE

#include <execinfo.h> /* for backtrace_symbols, etc. */
#include <signal.h>

void
ink_stack_trace_dump()
{
  int btl;

  // Recopy and re-terminate the app name in case it has been trashed.
  const char *msg = " - STACK TRACE: \n";
  if (write(STDERR_FILENO, program_name, strlen(program_name)) == -1)
    return;
  if (write(STDERR_FILENO, msg, strlen(msg)) == -1)
    return;

  // In certain situations you can get stuck in malloc waiting for a lock
  // that your program held when it segfaulted. We set an alarm so that
  // if this situation happens it will allow traffic_server to exit.
  alarm(10);

  void *stack[INK_STACK_TRACE_MAX_LEVELS + 1];
  memset(stack, 0, sizeof(stack));
  if ((btl = backtrace(stack, INK_STACK_TRACE_MAX_LEVELS)) > 2) {
    // dump the backtrace to stderr
    backtrace_symbols_fd(stack + 2, btl - 2, STDERR_FILENO);
  }
}

#else /* !TS_HAS_BACKTRACE */

void
ink_stack_trace_dump()
{
  const char msg[] = "ink_stack_trace_dump not implemented on this operating system\n";
  if (write(STDERR_FILENO, msg, sizeof(msg) - 1) == -1)
    return;
}

#endif /* TS_HAS_BACKTRACE */
