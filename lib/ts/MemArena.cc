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

#include <ts/MemArena.h>
#include <ts/ink_memory.h>
#include <ts/ink_assert.h>

using namespace ts;

void
MemArena::Block::operator delete(void *ptr)
{
  ::free(ptr);
}

MemArena::BlockPtr
MemArena::make_block(size_t n)
{
  // If post-freeze or reserved, allocate at least that much.
  n               = std::max<size_t>(n, next_block_size);
  next_block_size = 0; // did this, clear for next time.
  // Add in overhead and round up to paragraph units.
  n = Paragraph{round_up(n + ALLOC_HEADER_SIZE + sizeof(Block))};
  // If a page or more, round up to page unit size and clip back to account for alloc header.
  if (n >= Page::SCALE) {
    n = Page{round_up(n)} - ALLOC_HEADER_SIZE;
  }

  // Allocate space for the Block instance and the request memory and construct a Block at the front.
  // In theory this could use ::operator new(n) but this causes a size mismatch during ::operator delete.
  // Easier to use malloc and not carry a memory block size value around.
  return BlockPtr(new (::malloc(n)) Block(n - sizeof(Block)));
}

MemArena::MemArena(size_t n)
{
  next_block_size = 0; // Don't use default size.
  current         = this->make_block(n);
}

MemSpan
MemArena::alloc(size_t n)
{
  MemSpan zret;
  current_alloc += n;

  if (!current) {
    current = this->make_block(n);
    zret    = current->alloc(n);
  } else if (n > current->remaining()) { // too big, need another block
    if (next_block_size < n) {
      next_block_size = 2 * current->size;
    }
    BlockPtr block = this->make_block(n);
    // For the new @a current, pick the block which will have the most free space after taking
    // the request space out of the new block.
    zret = block->alloc(n);
    if (block->remaining() > current->remaining()) {
      block->next = current;
      current     = block;
#if defined(__clang_analyzer__)
      // Defeat another clang analyzer false positive. Unit tests validate the code is correct.
      ink_assert(current.use_count() > 1);
#endif
    } else {
      block->next   = current->next;
      current->next = block;
#if defined(__clang_analyzer__)
      // Defeat another clang analyzer false positive. Unit tests validate the code is correct.
      ink_assert(block.use_count() > 1);
#endif
    }
  } else {
    zret = current->alloc(n);
  }
  return zret;
}

MemArena &
MemArena::freeze(size_t n)
{
  prev       = current;
  prev_alloc = current_alloc;
  current.reset();
  next_block_size = n ? n : current_alloc;
  current_alloc   = 0;

  return *this;
}

MemArena &
MemArena::thaw()
{
  prev_alloc = 0;
  prev.reset();
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
  prev.reset();
  prev_alloc = 0;
  current.reset();
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
