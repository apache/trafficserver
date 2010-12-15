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
     detection and integration with the mem-tracker


  ****************************************************************************/

#include "DAllocator.h"
#include "Resource.h"

#define DALLOC_START_ELEMENTS  32
#define DALLOC_DESCRIPTOR_MAGIC  0x343bbbff

#define DALLOC_RED_ZONE_BYTES   16
#define DALLOC_MAKE_RED_ZONE(x)   (uint32_t) x | 0x189dda3f

void
write_red_zone(void *el, int el_size)
{

  if (el_size < DALLOC_RED_ZONE_BYTES * 2) {
    return;
  }

  uint32_t red = DALLOC_MAKE_RED_ZONE(el);


  int i;
  uint32_t *write_ptr = (uint32_t *) el;

  // Redzone the front the object
  for (i = 0; i < DALLOC_RED_ZONE_BYTES / sizeof(uint32_t); i++) {
    *write_ptr = red;
    write_ptr++;
  }

  // Redzone the back of the object
  write_ptr = (uint32_t *) (((char *) el) + el_size - DALLOC_RED_ZONE_BYTES);
  for (i = 0; i < DALLOC_RED_ZONE_BYTES / sizeof(uint32_t); i++) {
    *write_ptr = red;
    write_ptr++;
  }
}

int
check_red_zone(void *el, int el_size)
{

  if (el_size < DALLOC_RED_ZONE_BYTES * 2) {
    return 1;
  }

  uint32_t red = DALLOC_MAKE_RED_ZONE(el);

  int i;
  uint32_t *read_ptr = (uint32_t *) el;

  // Check the front the object
  for (i = 0; i < DALLOC_RED_ZONE_BYTES / sizeof(uint32_t); i++) {
    if (*read_ptr != red) {
      return 0;
    }
    read_ptr++;
  }

  // Check the back of the object
  read_ptr = (uint32_t *) (((char *) el) + el_size - DALLOC_RED_ZONE_BYTES);
  for (i = 0; i < DALLOC_RED_ZONE_BYTES / sizeof(uint32_t); i++) {
    if (*read_ptr != red) {
      return 0;
    }
    read_ptr++;
  }

  return 1;
}

AllocPoolDescriptor::AllocPoolDescriptor():region_start(NULL), region_end(NULL), num_el(0), el_size(0), descriptors(NULL)
{
}

void
AllocPoolDescriptor::add_elements(int num, DAllocator * da)
{
  num_el = num;
  el_size = da->el_size;

  int size = el_size * num;
  region_start = ink_memalign(da->alignment, size);
  region_end = ((char *) region_start) + size;

  if (unlikely((descriptors = (AllocDescriptor *) xmalloc(sizeof(AllocDescriptor) * num_el)) == 0)) {
    ink_fatal(1, "AllocPoolDescriptor::add_elements: couldn't allocate %d bytes",
              (int) (sizeof(AllocDescriptor) * num_el));
  }

  for (int i = 0; i < num_el; i++) {
    descriptors[i].magic = DALLOC_DESCRIPTOR_MAGIC;
    descriptors[i].link.next = NULL;
    descriptors[i].link.prev = NULL;
    descriptors[i].state = DALLOC_FREE;
    descriptors[i].el = ((char *) region_start) + (el_size * i);
    ink_assert(descriptors[i].el < region_end);

    write_red_zone(descriptors[i].el, da->el_size);
    da->free_list.push(descriptors + i);
  }
}

void
DAllocator::init(const char *name_arg, unsigned type_size, unsigned alignment_arg)
{

  // We can change sizes if we haven't allocated anything yet
  bool re_init_ok = (pools.head == NULL && free_list.head == NULL);

  if (name == NULL) {
    ink_mutex_init(&mutex, name_arg);
    name = (name_arg == 0) ? name_arg : "unknown";
  } else {
    name = name_arg;
  }

  if (alignment == 0 || re_init_ok) {
    alignment = alignment_arg;
  } else {
    ink_release_assert(alignment);
  }

  if (el_size == 0 || re_init_ok) {
    el_size = type_size;
  } else {
    ink_release_assert(el_size == type_size);
  }

}

void
DAllocator::add_pool(int num_el)
{

  AllocPoolDescriptor *p;
  p = NEW(new AllocPoolDescriptor);
  p->add_elements(num_el, this);
  pools.push(p);
}

void *
DAllocator::alloc()
{

  AllocDescriptor *descriptor;

  ink_mutex_acquire(&mutex);

  if (!free_list.head) {
    // Nothing on freelist
    int new_elements;
    if (pools.head) {
      new_elements = pools.head->num_el * 2;
    } else {
      new_elements = DALLOC_START_ELEMENTS;
    }
    add_pool(new_elements);
  }


  descriptor = free_list.pop();

  ink_mutex_release(&mutex);

  ink_assert(descriptor);

  ink_assert(descriptor->state == DALLOC_FREE);
  descriptor->state = DALLOC_IN_USE;

  ink_release_assert(check_red_zone(descriptor->el, el_size) == 1);

  return descriptor->el;
}

void
DAllocator::free(void *to_free)
{

  ink_mutex_acquire(&mutex);

  // First thing to do is find the pool descriptor for this element
  AllocPoolDescriptor *p = pools.head;

  while (p) {

    if (to_free < p->region_end && to_free >= p->region_start)
      break;

    p = p->link.next;
  }

  // If there is no matching pool, this a bogus free
  ink_release_assert(p);

  // Now find the element descriptor for this element
  int region_offset = (int) (((char *) to_free) - ((char *) p->region_start));
  ink_assert(region_offset >= 0);
  ink_release_assert(region_offset % el_size == 0);
  int index = region_offset / el_size;

  AllocDescriptor *d = p->descriptors + index;
  ink_release_assert(d->magic == DALLOC_DESCRIPTOR_MAGIC);
  ink_release_assert(d->state == DALLOC_IN_USE);
  ink_release_assert(d->el == to_free);

  d->state = DALLOC_FREE;
  write_red_zone(d->el, el_size);

  free_list.enqueue(d);

  ink_mutex_release(&mutex);
}

DAllocator::DAllocator():name(NULL), alignment(0), el_size(0)
{
}

DAllocator::~DAllocator()
{
}
