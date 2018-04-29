/** @file

    MemArena memory allocator. Chunks of memory are allocated, frozen into generations and
     thawed away when unused.

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

#include "MemArena.h"
#include <ts/ink_memory.h>
#include <ts/ink_assert.h>

using namespace ts;

inline MemArena::Block::Block(size_t n) : size(n), allocated(0), next(nullptr) {}

inline char *
MemArena::Block::data()
{
  return reinterpret_cast<char *>(this + 1);
}

/**
    Allocates a new internal block of memory. If there are no existing blocks, this becomes the head of the
     ll. If there are existing allocations, the new block is inserted in the current list.
     If @custom == true, the new block is pushed into the generation but @current doesn't change.
        @custom == false, the new block is pushed to the head and becomes the @current internal block.
  */
inline MemArena::Block *
MemArena::newInternalBlock(size_t n, bool custom)
{
  // Adjust to the nearest power of two. Works for 64 bit values. Allocate Block header and
  //  actual underlying memory together for locality. ALLOC_HEADER_SIZE to account for malloc/free headers.
  static constexpr size_t free_space_per_page = DEFAULT_PAGE_SIZE - sizeof(Block) - ALLOC_HEADER_SIZE;

  void *tmp;
  if (n <= free_space_per_page) { // will fit within one page, just allocate.
    tmp = ats_malloc(n + sizeof(Block));
  } else {
    size_t t = n;
    t--;
    t |= t >> 1;
    t |= t >> 2;
    t |= t >> 4;
    t |= t >> 8;
    t |= t >> 16;
    t |= t >> 32;
    t++;
    n   = t - sizeof(Block) - ALLOC_HEADER_SIZE; // n is the actual amount of memory the block can allocate out.
    tmp = ats_malloc(t - ALLOC_HEADER_SIZE);
  }

  std::shared_ptr<Block> block(new (tmp) Block(n)); // placement new

  if (current) {
    arena_size += n;
    generation_size += n;

    if (!custom) {
      block->next = current;
      current     = block;
      return current.get();
    } else {
      // Situation where we do not have enough space for a large block of memory. We don't want
      //  to update @current because it would be wasting memory. Create a new block for the entire
      //  allocation and just add it to the generation.
      block->next   = current->next; // here, current always exists.
      current->next = block;
    }
  } else { // empty
    generation_size = n;
    arena_size      = n;

    generation = current = block;
  }

  return block.get();
}

MemArena::MemArena()
{
  newInternalBlock(arena_size, true); // nDefault size
}

MemArena::MemArena(size_t n)
{
  newInternalBlock(n, true);
}

/**
    Returns a span of memory of @n bytes. If necessary, alloc will create a new internal block
     of memory in order to serve the required number of bytes.
 */
MemSpan
MemArena::alloc(size_t n)
{
  total_alloc += n;

  // Two cases when we want a new internal block:
  //   1. A new generation.
  //   2. Current internal block isn't large enough to alloc
  //       @n bytes.

  Block *block = nullptr;

  if (!generation) { // allocation after a freeze. new generation.
    generation_size = 0;

    next_block_size = (next_block_size < n) ? n : next_block_size;
    block           = newInternalBlock(next_block_size, false);

    // current is updated in newInternalBlock.
    generation = current;
  } else if (current->size - current->allocated /* remaining size */ < n) {
    if (n >= DEFAULT_PAGE_SIZE && n >= (current->size / 2)) {
      block = newInternalBlock(n, true);
    } else {
      block = newInternalBlock(current->size * 2, false);
    }
  } else {
    // All good. Simply allocate.
    block = current.get();
  }

  ink_assert(block->data() != nullptr);
  ink_assert(block->size >= n);

  uint64_t offset = block->allocated;
  block->allocated += n;

  // Allocate a span of memory within the block.
  MemSpan ret(block->data() + offset, n);
  return ret;
}

MemArena &
MemArena::freeze(size_t n)
{
  generation      = nullptr;
  next_block_size = n ? n : total_alloc;
  prev_alloc      = total_alloc;

  return *this;
}

/**
    Everything up the current generation is considered frozen and will be
     thawed away (deallocated).
 */
MemArena &
MemArena::thaw()
{
  // A call to thaw a frozen generation before any allocation. Empty the arena.
  if (!generation) {
    return empty();
  }

  arena_size = generation_size;
  total_alloc -= prev_alloc;
  prev_alloc = 0;

  generation->next = nullptr;
  return *this;
}

/**
    Check if a pointer is in the arena. Need to search through all the internal blocks.
 */
bool
MemArena::contains(void *ptr) const
{
  Block *tmp = current.get();
  while (tmp) {
    if (ptr >= tmp->data() && ptr < tmp->data() + tmp->size) {
      return true;
    }
    tmp = tmp->next.get();
  }
  return false;
}

MemArena &
MemArena::empty()
{
  generation = nullptr;
  current    = nullptr;

  arena_size = generation_size = 0;
  total_alloc = prev_alloc = 0;

  return *this;
}