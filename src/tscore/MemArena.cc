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

#include "tscore/MemArena.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_assert.h"

using namespace ts;

void
MemArena::Block::operator delete(void *ptr)
{
  ::free(ptr);
}

MemArena::BlockPtr
MemArena::make_block(size_t n)
{
  // If there's no reservation hint, use the extent. This is transient because the hint is cleared.
  if (_reserve_hint == 0) {
    if (_active_reserved) {
      _reserve_hint = _active_reserved;
    } else if (_prev_allocated) {
      _reserve_hint = _prev_allocated;
    }
  }

  // If post-freeze or reserved, allocate at least that much.
  n             = std::max<size_t>(n, _reserve_hint);
  _reserve_hint = 0; // did this, clear for next time.
  // Add in overhead and round up to paragraph units.
  n = Paragraph{round_up(n + ALLOC_HEADER_SIZE + sizeof(Block))};
  // If a page or more, round up to page unit size and clip back to account for alloc header.
  if (n >= Page::SCALE) {
    n = Page{round_up(n)} - ALLOC_HEADER_SIZE;
  }

  // Allocate space for the Block instance and the request memory and construct a Block at the front.
  // In theory this could use ::operator new(n) but this causes a size mismatch during ::operator delete.
  // Easier to use malloc and override @c delete.
  auto free_space = n - sizeof(Block);
  _active_reserved += free_space;
  return BlockPtr(new (::malloc(n)) Block(free_space));
}

MemSpan
MemArena::alloc(size_t n)
{
  MemSpan zret;
  _active_allocated += n;

  if (!_active) {
    _active = this->make_block(n);
    zret    = _active->alloc(n);
  } else if (n > _active->remaining()) { // too big, need another block
    BlockPtr block = this->make_block(n);
    // For the new @a current, pick the block which will have the most free space after taking
    // the request space out of the new block.
    zret = block->alloc(n);
    if (block->remaining() > _active->remaining()) {
      block->next = _active;
      _active     = block;
#if defined(__clang_analyzer__)
      // Defeat another clang analyzer false positive. Unit tests validate the code is correct.
      ink_assert(current.use_count() > 1);
#endif
    } else {
      block->next   = _active->next;
      _active->next = block;
#if defined(__clang_analyzer__)
      // Defeat another clang analyzer false positive. Unit tests validate the code is correct.
      ink_assert(block.use_count() > 1);
#endif
    }
  } else {
    zret = _active->alloc(n);
  }
  return zret;
}

MemArena &
MemArena::freeze(size_t n)
{
  _prev = _active;
  _active.reset(); // it's in _prev now, start fresh.
  // Update the meta data.
  _prev_allocated   = _active_allocated;
  _active_allocated = 0;
  _prev_reserved    = _active_reserved;
  _active_reserved  = 0;

  _reserve_hint = n;

  return *this;
}

MemArena &
MemArena::thaw()
{
  _prev.reset();
  _prev_reserved = _prev_allocated = 0;
  return *this;
}

bool
MemArena::contains(const void *ptr) const
{
  for (Block *b = _active.get(); b; b = b->next.get()) {
    if (b->contains(ptr)) {
      return true;
    }
  }
  for (Block *b = _prev.get(); b; b = b->next.get()) {
    if (b->contains(ptr)) {
      return true;
    }
  }

  return false;
}

MemArena &
MemArena::clear(size_t n)
{
  _reserve_hint = n ? n : _prev_allocated + _active_allocated;
  _prev.reset();
  _prev_reserved = _prev_allocated = 0;
  _active.reset();
  _active_reserved = _active_allocated = 0;

  return *this;
}
