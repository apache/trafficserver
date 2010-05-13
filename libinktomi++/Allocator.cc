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

#include "ink_unused.h"    /* MAGIC_EDITING_TAG */
/****************************************************************************

  Allocator.h


*****************************************************************************/

#include "inktomi++.h"
#include "Allocator.h"
#include "ink_align.h"

Allocator::Allocator(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment)
{
#ifdef USE_DALLOC
  da.init(name, element_size, alignment);
#else
  ink_freelist_init(&fl, name, element_size, chunk_size, 0, alignment);
#endif
}

#ifdef USE_PARTITION_MEMORY     /* VxWorks */

PART_ID uts_part_id = NULL;

void
partfree(void *ptr)
{
  memPartFree(uts_part_id, (char *) ptr);
}

void *
partmalloc(unsigned int size)
{
  char *mem;
  if (NULL == uts_part_id) {
    if (NULL == (uts_part_id = memPartCreate("uTSmem", 10000000))) {
      fprintf(stderr, "[TS]: main: Unable to allocate Memory pool(%d bytes)\n", 10000000);
      return NULL;
    }
    memPartOptionsSet(uts_part_id, MEM_ALLOC_ERROR_LOG_FLAG
                      | MEM_ALLOC_ERROR_SUSPEND_FLAG |
                      MEM_BLOCK_ERROR_LOG_FLAG | MEM_BLOCK_ERROR_SUSPEND_FLAG | MEM_BLOCK_CHECK);

    /*fprintf(stdout,"[TS]: main: create memory pool, armMemPartitionId=%x\n",  uts_part_id); */
  }

  mem = (char *) memPartAlloc(uts_part_id, size);
  return mem;
}

void *
partrealloc(void *ptr, unsigned int size)
{
  char *mem;

  mem = (char *) memPartRealloc(uts_part_id, (char *) ptr, size);
  return mem;
}

void *
partstrdup(const char *str, int length)
{
  char *mem;

  if (length == -1) {
    mem = (char *) memPartAlloc(uts_part_id, strlen(str) + 1);
    strcpy(mem, str);
  } else {
    mem = (char *) memPartAlloc(uts_part_id, length);
    strncpy(mem, str, length);
  }
  return mem;
}
#endif /* PARTITION_MEMORY */
