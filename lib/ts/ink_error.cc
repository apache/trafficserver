/** @file

  Error reporting routines

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

#include "libts.h"
#include "ink_error.h"
#include "ink_stack_trace.h"

#include <syslog.h>
#include <signal.h>    /* MAGIC_EDITING_TAG */

static int ink_dprintf_level = 0;

/**
  This routine causes process death. Some signal handler problems got
  in the way of abort before, so this is an overzealous and somewhat
  amusing implementation.

*/
void
ink_die_die_die(int retval)
{
  abort();
  _exit(retval);
  exit(retval);
}

/**
  This routine prints/logs an error message given the printf format
  string in message_format, and the optional arguments.  The program is
  then terminated with return_code.

*/
void
ink_fatal_va(int return_code, const char * fmt, va_list ap)
{
  char msg[1024];
  const size_t len = sizeof("FATAL: ") - 1;

  strncpy(msg, "FATAL: ", sizeof(msg));
  vsnprintf(msg + len, sizeof(msg) - len, fmt, ap);
  msg[sizeof(msg) - 1] = 0;

  fprintf(stderr, "%s\n", msg);
  syslog(LOG_CRIT, "%s", msg);
  ink_stack_trace_dump();
  ink_die_die_die(return_code);
}

void
ink_fatal(int return_code, const char *message_format, ...)
{
  va_list ap;
  va_start(ap, message_format);
  ink_fatal_va(return_code, message_format, ap);
  va_end(ap);
}

void
ink_fatal_die(const char *message_format, ...)
{
  va_list ap;
  va_start(ap, message_format);
  ink_fatal_va(1, message_format, ap);
  va_end(ap);
}

/**
  This routine prints/logs an error message given the printf format
  string in message_format, and the optional arguments.  The current
  errno is also printed.  The program is then terminated with return_code.

*/
void
ink_pfatal(int return_code, const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  char *errno_string;

  va_start(ap, message_format);
  errno_string = strerror(errno);
  snprintf(extended_format, sizeof(extended_format) - 1, "FATAL: %s <last errno = %d (%s)>",
           message_format, errno, (errno_string == NULL ? "unknown" : errno_string));
  extended_format[sizeof(extended_format) - 1] = 0;
  vsnprintf(message, sizeof(message) - 1, extended_format, ap);
  message[sizeof(message) - 1] = 0;
  fprintf(stderr, "%s\n", message);
  syslog(LOG_CRIT, "%s", message);
  va_end(ap);
  ink_stack_trace_dump();
  ink_die_die_die(return_code);
}

/**
  This routine prints/logs a warning message given the printf format
  string in message_format, and the optional arguments.

*/
void
ink_warning(const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];
  va_start(ap, message_format);
  snprintf(extended_format, sizeof(extended_format) - 1, "WARNING: %s", message_format);
  extended_format[sizeof(extended_format) - 1] = 0;
  vsnprintf(message, sizeof(message) - 1, extended_format, ap);
  message[sizeof(message) - 1] = 0;
  fprintf(stderr, "%s\n", message);
  syslog(LOG_WARNING, "%s", message);
  va_end(ap);
}

/**
  This routine prints/logs a warning message given the printf format
  string in message_format, and the optional arguments.  The current
  errno is also printed.

*/
void
ink_pwarning(const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];
  char *errno_string;

  va_start(ap, message_format);
  errno_string = strerror(errno);
  snprintf(extended_format, sizeof(extended_format) - 1, "WARNING: %s <last errno = %d (%s)>",
           message_format, errno, (errno_string == NULL ? "unknown" : errno_string));
  extended_format[sizeof(extended_format) - 1] = 0;
  vsnprintf(message, sizeof(message) - 1, extended_format, ap);
  message[sizeof(message) - 1] = 0;
  fprintf(stderr, "%s\n", message);
  syslog(LOG_WARNING, "%s", message);
  va_end(ap);
}

/**
  This routine prints/logs a notice message given the printf format
  string in message_format, and the optional arguments.

*/
void
ink_notice(const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];
  va_start(ap, message_format);
  snprintf(extended_format, sizeof(extended_format) - 1, "NOTE: %s", message_format);
  extended_format[sizeof(extended_format) - 1] = 0;
  vsnprintf(message, sizeof(message) - 1, extended_format, ap);
  message[sizeof(message) - 1] = 0;
  fprintf(stderr, "%s\n", message);
  syslog(LOG_NOTICE, "%s", message);
  va_end(ap);
}

/**
  This routine prints/logs a message given the printf format string in
  message_format, and the optional arguments.

*/
void
ink_eprintf(const char *message_format, ...)
{
  va_list ap;
  char message[4096];
  va_start(ap, message_format);
  vsnprintf(message, sizeof(message) - 1, message_format, ap);
  message[sizeof(message) - 1] = 0;
  fprintf(stderr, "ERROR: %s\n", message);
  va_end(ap);
}

/**
  This routine prints/logs a warning message given the printf format
  string in message_format, and the optional arguments.

*/
void
ink_error(const char *message_format, ...)
{
  va_list ap;
  char extended_format[2048], message[4096];
  va_start(ap, message_format);
  snprintf(extended_format, sizeof(extended_format) - 1, "ERROR: %s", message_format);
  extended_format[sizeof(extended_format) - 1] = 0;
  vsnprintf(message, sizeof(message) - 1, extended_format, ap);
  message[sizeof(message) - 1] = 0;
  fprintf(stderr, "%s\n", message);
  syslog(LOG_ERR, "%s", message);
  va_end(ap);
}

/**
  This routine prints/logs a message given the printf format string in
  message_format, and the optional arguments.

*/
void
ink_dprintf(int debug_level, const char *message_format, ...)
{
  char message[4096];
  va_list ap;
  if (debug_level <= ink_dprintf_level) {
    va_start(ap, message_format);
    vsnprintf(message, sizeof(message) - 1, message_format, ap);
    message[sizeof(message) - 1] = 0;
    fprintf(stderr, "%s\n", message);
    va_end(ap);
  }
}

/**
  Set output level for ink_dprintf() function. For debugging purposes
  only!

*/
int
ink_set_dprintf_level(int debug_level)
{
  int old_ink_dprintf_level = ink_dprintf_level;
  if ((ink_dprintf_level = debug_level) < 0)
    ink_dprintf_level = 0;
  return old_ink_dprintf_level;
}
