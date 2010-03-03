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

/*****************************************************************************

  DAllocator.h

   A pool allocator with some debugging feature including double free
     detection

   The basic idea behind this allocator is that you can store information
     about each allocated block separately from the block allowing easy
     double free detection.  It's also easy to integrate with the
     memory tracker but I found most fast allocated stuff doesn't lend
     it self well to this approach as it's typically allocated through
     wrapper functions

   
  ****************************************************************************/

#ifndef _D_ALLOCATOR_H_
#define _D_ALLOCATOR_H_

#include "List.h"
#include "ink_mutex.h"

enum DallocState
{
  DALLOC_UNKNOWN = 0,
  DALLOC_FREE,
  DALLOC_IN_USE
};

struct AllocDescriptor
{
  int magic;
  DallocState state;
  void *el;

  LINK(AllocDescriptor, link);      // list of free elements
};

struct DAllocator;
struct AllocPoolDescriptor
{
  AllocPoolDescriptor();
  void add_elements(int num, DAllocator * da);

  void *region_start;
  void *region_end;
  int num_el;
  int el_size;

  AllocDescriptor *descriptors;

  SLINK(AllocPoolDescriptor, link);
};

struct DAllocator
{
  ink_mutex mutex;
  const char *name;
  int alignment;
  int el_size;

  SList(AllocPoolDescriptor,link) pools;
  Que(AllocDescriptor,link) free_list;

  DAllocator();
  ~DAllocator();

  void init(const char *name, unsigned type_size, unsigned alignment);
  void *alloc();
  void free(void *to_free);

private:
  void add_pool(int num_el);
};



#endif
