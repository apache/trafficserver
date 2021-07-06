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

#pragma once

typedef void (*signal_handler_t)(int signo, siginfo_t *info, void *ctx);

// Default crash signal handler that dumps a stack trace and exits.
void signal_crash_handler(int, siginfo_t *, void *);

// Attach a signal handler to fatal crash signals.
void signal_register_crash_handler(signal_handler_t);
// Attach a signal handler to the default et of signals we care about.
void signal_register_default_handler(signal_handler_t);

// Format a siginfo_t into an informative(?) message on stderr.
void signal_format_siginfo(int signo, siginfo_t *, const char *);

// Test whether a signal indicates a process crashing.
bool signal_is_crash(int signo);

// Test whether the signal is masked.
bool signal_is_masked(int signo);

// Test whether the signal is being handled by the given handler.
bool signal_check_handler(int signo, signal_handler_t handler);

// Start a thread to test whether signals have the expected handler. Apparently useful for
// finding pthread bugs in some version of DEC Unix.
void signal_start_check_thread(signal_handler_t handler);
