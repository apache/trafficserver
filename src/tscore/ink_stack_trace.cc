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

#include "tscore/ink_platform.h"
#include "tscore/ink_stack_trace.h"
#include "tscore/ink_args.h"

#include <strings.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define HAS_CXXABI 1
#else
#define HAS_CXXABI 0
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#if TS_HAS_BACKTRACE

#include <execinfo.h> /* for backtrace_symbols, etc. */
#include <csignal>

void
ink_stack_trace_dump()
{
  int btl;

  // Recopy and re-terminate the app name in case it has been trashed.
  const char *msg = " - STACK TRACE: \n";
  if (write(STDERR_FILENO, program_name, strlen(program_name)) == -1) {
    return;
  }
  if (write(STDERR_FILENO, msg, strlen(msg)) == -1) {
    return;
  }

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

const void *
ink_backtrace(const int n)
{
  if (INK_STACK_TRACE_MAX_LEVELS < n + 1) {
    return nullptr;
  }

  const int m = n + 1;
  void     *callstack[m];
  int       frames = backtrace(callstack, m);

  const void *symbol = nullptr;
  if (frames == m && callstack[n] != nullptr) {
    symbol = callstack[n];
  }

  return symbol;
}

namespace
{
// Demangle a symbol name if possible. The caller must free the returned string
// if it is non-null and different from the input.
char *
demangle_symbol(const char *symbol)
{
#if HAS_CXXABI
  // Symbol format is typically: "binary(mangled_name+0x1234) [0xaddr]"
  // We need to extract the mangled name between '(' and '+' or ')'
  const char *start = strchr(symbol, '(');
  if (start == nullptr) {
    return nullptr;
  }
  start++;

  const char *end = strchr(start, '+');
  if (end == nullptr) {
    end = strchr(start, ')');
  }
  if (end == nullptr || end <= start) {
    return nullptr;
  }

  size_t len     = end - start;
  char  *mangled = static_cast<char *>(malloc(len + 1));
  if (mangled == nullptr) {
    return nullptr;
  }
  memcpy(mangled, start, len);
  mangled[len] = '\0';

  int   status    = 0;
  char *demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
  free(mangled);

  if (status == 0 && demangled != nullptr) {
    return demangled;
  }
  return nullptr;
#else
  (void)symbol;
  return nullptr;
#endif
}
} // anonymous namespace

ssize_t
ink_stack_trace_dump_to_fd(int fd)
{
  void *stack[INK_STACK_TRACE_MAX_LEVELS + 1];
  memset(stack, 0, sizeof(stack));
  int btl = backtrace(stack, INK_STACK_TRACE_MAX_LEVELS);
  if (btl <= 2) {
    return 0;
  }

  // Use backtrace_symbols_fd which is async-signal-safe (doesn't call malloc).
  // Skip the first 2 frames (this function and its caller).
  backtrace_symbols_fd(stack + 2, btl - 2, fd);

  // We can't easily know how many bytes were written by backtrace_symbols_fd,
  // so return a positive value to indicate success.
  return 1;
}

void
ink_stack_trace_get(std::string &bt)
{
  bt.clear();

  void *stack[INK_STACK_TRACE_MAX_LEVELS + 1];
  memset(stack, 0, sizeof(stack));
  int btl = backtrace(stack, INK_STACK_TRACE_MAX_LEVELS);
  if (btl <= 2) {
    return;
  }

  // Skip the first 2 frames (this function and its caller).
  char **symbols = backtrace_symbols(stack + 2, btl - 2);
  if (symbols == nullptr) {
    return;
  }

  char line[1024];
  for (int i = 0; i < btl - 2; ++i) {
    char *demangled = demangle_symbol(symbols[i]);

    if (demangled != nullptr) {
      // Extract the address part from the original symbol.
      const char *addr_start = strchr(symbols[i], '[');
      const char *addr       = addr_start ? addr_start : "";
      snprintf(line, sizeof(line), "%-4d %s %s\n", i, demangled, addr);
      free(demangled);
    } else {
      snprintf(line, sizeof(line), "%-4d %s\n", i, symbols[i]);
    }

    bt += line;
  }

  free(symbols);
}

#else /* !TS_HAS_BACKTRACE */

void
ink_stack_trace_dump()
{
  const char msg[] = "ink_stack_trace_dump not implemented on this operating system\n";
  if (write(STDERR_FILENO, msg, sizeof(msg) - 1) == -1) {
    return;
  }
}

const void *
ink_backtrace(const int /* n */)
{
  return nullptr;
}

ssize_t
ink_stack_trace_dump_to_fd(int /* fd */)
{
  return -1;
}

void
ink_stack_trace_get(std::string &bt)
{
  bt.clear();
}

#endif /* TS_HAS_BACKTRACE */
