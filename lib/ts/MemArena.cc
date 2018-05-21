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

#include <algorithm>

#include "MemArena.h"
#include <ts/ink_memory.h>
#include <ts/ink_assert.h>

using namespace ts;

/**
    Allocates a new internal block of memory. If there are no existing blocks, this becomes the head of the
     ll. If there are existing allocations, the new block is inserted in the current list.
     If @custom == true, the new block is pushed into the generation but @current doesn't change.
        @custom == false, the new block is pushed to the head and becomes the @current internal block.
  */
inline MemArena::Block *
MemArena::newInternalBlock(size_t n, bool custom)
{
  // Allocate Block header and actual underlying memory together for locality and fewer calls.
  // ALLOC_HEADER_SIZE to account for malloc/free headers to try to minimize pages required.
  static constexpr size_t FREE_SPACE_PER_PAGE = DEFAULT_PAGE_SIZE - sizeof(Block) - ALLOC_HEADER_SIZE;
  static_assert(ALLOC_MIN_SIZE > ALLOC_HEADER_SIZE,
                "ALLOC_MIN_SIZE must be larger than ALLOC_HEADER_SIZE to ensure positive allocation request size.");

  // If post-freeze or reserved, bump up block size then clear.
  n               = std::max({n, next_block_size, ALLOC_MIN_SIZE});
  next_block_size = 0;

  if (n <= FREE_SPACE_PER_PAGE) { // will fit within one page, just allocate.
    n += sizeof(Block);           // can just allocate that much with the Block.
  } else {
    // Round up to next power of 2 and allocate that.
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    ++n;                    // power of 2 now.
    n -= ALLOC_HEADER_SIZE; // clip presumed malloc header size.
  }

  // Allocate space for the Block instance and the request memory.
  std::shared_ptr<Block> block(new (ats_malloc(n)) Block(n - sizeof(Block)));

  if (current) {
    if (!custom) {
      block->next = current;
      current     = block;
    } else {
      // Situation where we do not have enough space for a large block of memory. We don't want
      //  to update @current because it would be wasting memory. Create a new block for the entire
      //  allocation and just add it to the generation.
      block->next   = current->next; // here, current always exists.
      current->next = block;
    }
  } else { // empty
    current = block;
  }

  return block.get();
}

MemArena::MemArena(size_t n)
{
  next_block_size = 0; // don't force larger size.
  this->newInternalBlock(n, true);
}

/**
    Returns a span of memory of @n bytes. If necessary, alloc will create a new internal block
     of memory in order to serve the required number of bytes.
 */
MemSpan
MemArena::alloc(size_t n)
{
  Block *block = nullptr;

  current_alloc += n;

  if (!current) {
    block = this->newInternalBlock(n, false);
  } else {
    if (current->size - current->allocated /* remaining size */ < n) {
      if (n >= DEFAULT_PAGE_SIZE && n >= (current->size / 2)) {
        block = this->newInternalBlock(n, true);
      } else {
        block = this->newInternalBlock(current->size * 2, false);
      }
    } else {
      block = current.get();
    }
  }

  ink_assert(block->data() != nullptr);
  ink_assert(block->size >= n);

  auto zret = block->remnant().prefix(n);
  block->allocated += n;

  return zret;
}

MemArena &
MemArena::freeze(size_t n)
{
  prev            = current;
  prev_alloc      = current_alloc;
  current         = nullptr;
  next_block_size = n ? n : current_alloc;
  current_alloc   = 0;

  return *this;
}

MemArena &
MemArena::thaw()
{
  prev_alloc = 0;
  prev       = nullptr;
  return *this;
}

bool
MemArena::contains(const void *ptr) const
{
  for (Block *b = current.get(); b; b = b->next.get()) {
    if (b->contains(ptr)) {
      return true;
    }
  }
  for (Block *b = prev.get(); b; b = b->next.get()) {
    if (b->contains(ptr)) {
      return true;
    }
  }

  return false;
}

MemArena &
MemArena::clear()
{
  prev          = nullptr;
  prev_alloc    = 0;
  current       = nullptr;
  current_alloc = 0;

  return *this;
}

size_t
MemArena::extent() const
{
  size_t zret{0};
  Block *b;
  for (b = current.get(); b; b = b->next.get()) {
    zret += b->size;
  }
  for (b = prev.get(); b; b = b->next.get()) {
    zret += b->size;
  }
  return zret;
};
