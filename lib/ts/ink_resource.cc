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

#include "libts.h"

#include "ink_assert.h"
#include "ink_atomic.h"
#include "ink_port.h"
#include "ink_resource.h"
#include "ink_stack_trace.h"

volatile int res_track_memory = RES_TRACK_MEMORY_DEFAULT;

/*-------------------------------------------------------------------------
-------------------------------------------------------------------------*/
char *
_xstrdup(const char *str, int length, const char *path)
{
  NOWARN_UNUSED(path);
  char *newstr;

  if (likely(str)) {
    if (length < 0)
      length = strlen(str);

    newstr = (char *)ats_malloc(length + 1);
    ink_strlcpy(newstr, str, length + 1);
    return newstr;
  }
  return NULL;
}

typedef struct Resource Resource;

Resource *
res_lookup(const char *path)
{
  NOWARN_UNUSED(path);
  return NULL;
}

void
xdump(void)
{
  ink_stack_trace_dump();
}
