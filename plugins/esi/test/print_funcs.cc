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

#include <cstdio>
#include <cstdarg>
#include <string>
#include <ts/ts.h>

#include "print_funcs.h"

char ts_new_debug_on_flag_{1};

static const int LINE_SIZE = 1024 * 1024;

static std::string *buffer_TSDbg{nullptr};

void
set_TSDbgBuffer(std::string *buf)
{
  buffer_TSDbg = buf;
}

TSDbgCtl const *
TSDbgCtlCreate(char const *tag)
{
  static TSDbgCtl dummy;
  dummy.on  = 1;
  dummy.tag = tag;
  return &dummy;
}

tsapi void
TSDbgCtlDestroy(TSDbgCtl const *)
{
}

void
_TSDbg(const char *tag, const char *fmt, ...)
{
  char buf[LINE_SIZE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, LINE_SIZE, fmt, ap);
  if (buffer_TSDbg) {
    *buffer_TSDbg = *buffer_TSDbg + "Debug (" + std::string{tag} + "): " + std::string{buf} + '\n';
  } else {
    printf("Debug (%s): %s\n", tag, buf);
  }
  va_end(ap);
}

void
Error(const char *fmt, ...)
{
  char buf[LINE_SIZE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, LINE_SIZE, fmt, ap);
  printf("Error: %s\n", buf);
  va_end(ap);
}
