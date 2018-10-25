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
#include "swoc/MemArena.h"

using namespace swoc;

void
MemArena::Block::operator delete(void *ptr)
{
  ::free(ptr);
}

MemArena::Block *
MemArena::make_block(size_t n)
{
  // If there's no reservation hint, use the extent. This is transient because the hint is cleared.
  if (_reserve_hint == 0) {
    if (_active_reserved) {
      _reserve_hint = _active_reserved;
    } else if (_frozen_allocated) {
      _reserve_hint = _frozen_allocated;
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
  return new (::malloc(n)) Block(free_space);
}

MemSpan<void>
MemArena::alloc(size_t n)
{
  Block *block = _active.head();

  if (nullptr == block) {
    block = this->make_block(n);
    _active.prepend(block);
  } else if (n > block->remaining()) { // too big, need another block
    block = this->make_block(n);
    // For the resulting active allocation block, pick the block which will have the most free space
    // after taking the request space out of the new block.
    if (block->remaining() - n > _active.head()->remaining()) {
      _active.prepend(block);
    } else {
      _active.insert_after(_active.head(), block);
    }
  }
  _active_allocated += n;
  return block->alloc(n);
}

MemArena &
MemArena::freeze(size_t n)
{
  this->destroy_frozen();
  _frozen = std::move(_active);
  // Update the meta data.
  _frozen_allocated = _active_allocated;
  _active_allocated = 0;
  _frozen_reserved  = _active_reserved;
  _active_reserved  = 0;

  _reserve_hint = n;

  return *this;
}

MemArena &
MemArena::thaw()
{
  this->destroy_frozen();
  _frozen_reserved = _frozen_allocated = 0;
  return *this;
}

bool
MemArena::contains(const void *ptr) const
{
  auto pred = [ptr](const Block &b) -> bool { return b.contains(ptr); };

  return std::any_of(_active.begin(), _active.end(), pred) || std::any_of(_frozen.begin(), _frozen.end(), pred);
}

void
MemArena::destroy_active()
{
  _active.apply([](Block *b) { delete b; }).clear();
}

void
MemArena::destroy_frozen()
{
  _frozen.apply([](Block *b) { delete b; }).clear();
}

MemArena &
MemArena::clear(size_t n)
{
  _reserve_hint    = n ? n : _frozen_allocated + _active_allocated;
  _frozen_reserved = _frozen_allocated = 0;
  _active_reserved = _active_allocated = 0;
  this->destroy_frozen();
  this->destroy_active();

  return *this;
}

MemArena::~MemArena()
{
  // Destruct in a way that makes it safe for the instance to be in one of its own memory blocks.
  Block *ba = _active.head();
  Block *bf = _frozen.head();
  _active.clear();
  _frozen.clear();
  while (bf) {
    Block *b = bf;
    bf       = bf->_link._next;
    delete b;
  }
  while (ba) {
    Block *b = ba;
    ba       = ba->_link._next;
    delete b;
  }
}
