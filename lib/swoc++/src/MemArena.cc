/** @file

    MemArena memory allocator. Chunks of memory are allocated, frozen into generations and thawed
    away when unused.
 */

/*  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
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

// Need to break these out because the default implementation doesn't clear the
// integral values in @a that.

MemArena::MemArena(swoc::MemArena::self_type &&that)
  : _active_allocated(that._active_allocated),
    _active_reserved(that._active_reserved),
    _frozen_allocated(that._frozen_allocated),
    _frozen_reserved(that._frozen_reserved),
    _reserve_hint(that._reserve_hint),
    _frozen(std::move(that._frozen)),
    _active(std::move(that._active))
{
  that._active_allocated = that._active_reserved = 0;
  that._frozen_allocated = that._frozen_reserved = 0;
  that._reserve_hint                             = 0;
}

MemArena *
MemArena::make(size_t n)
{
  MemArena tmp;
  return tmp.make<MemArena>(std::move(tmp));
}

MemArena &
MemArena::operator=(swoc::MemArena::self_type &&that)
{
  this->clear();
  std::swap(_active_allocated, that._active_allocated);
  std::swap(_active_reserved, that._active_reserved);
  std::swap(_frozen_allocated, that._frozen_allocated);
  std::swap(_frozen_reserved, that._frozen_reserved);
  std::swap(_reserve_hint, that._reserve_hint);
  _active = std::move(that._active);
  _frozen = std::move(that._frozen);
  return *this;
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
  MemSpan<void> zret;
  this->require(n);
  auto block = _active.head();
  zret       = block->alloc(n);
  _active_allocated += n;
  // If this block is now full, move it to the back.
  if (block->full() && block != _active.tail()) {
    _active.erase(block);
    _active.append(block);
  }
  return zret;
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

MemArena &
MemArena::require(size_t n)
{
  auto spot = _active.begin();
  Block *block{nullptr};

  if (spot == _active.end()) {
    block = this->make_block(n);
    _active.prepend(block);
  } else {
    // Search back through the list until a full block is hit, which is a miss.
    while (spot != _active.end() && n > spot->remaining()) {
      if (spot->full())
        spot = _active.end();
      else
        ++spot;
    }
    if (spot == _active.end()) { // no block has enough free space
      block = this->make_block(n);
      _active.prepend(block);
    } else if (spot != _active.begin()) {
      // big enough space, if it's not at the head, move it there.
      block = spot;
      _active.erase(block);
      _active.prepend(block);
    }
  }
  return *this;
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
