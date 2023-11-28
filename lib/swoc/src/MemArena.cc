// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file

    MemArena memory allocator. Chunks of memory are allocated, frozen into generations and thawed
    away when unused.
 */
#include "swoc/MemArena.h"
#include <algorithm>

namespace swoc { inline namespace SWOC_VERSION_NS {

void (*MemArena::destroyer)(MemArena *) = std::destroy_at<MemArena>;

inline bool
MemArena::Block::satisfies(size_t n, size_t align) const {
  auto r = this->remaining();
  return r >= (n + align_padding(this->data() + allocated, align));
}

MemArena::MemArena(MemSpan<void> static_block) {
  static constexpr Scalar<16, size_t> MIN_BLOCK_SIZE = round_up(sizeof(Block) + Block::MIN_FREE_SPACE);
  if (static_block.size() < MIN_BLOCK_SIZE) {
    throw std::domain_error("MemArena static block is too small.");
  }
  // Construct the block data in the block and put it on the list. Make a note this is the
  // static block that shouldn't be deleted.
  auto space       = static_block.size() - sizeof(Block);
  _static_block    = new (static_block.data()) Block(space);
  _active_reserved = space;
  _active.prepend(_static_block);
}

// Need to break these out because the default implementation doesn't clear the
// integral values in @a that.

MemArena::MemArena(swoc::MemArena::self_type &&that) noexcept
  : _active_allocated(that._active_allocated),
    _active_reserved(that._active_reserved),
    _frozen_allocated(that._frozen_allocated),
    _frozen_reserved(that._frozen_reserved),
    _reserve_hint(that._reserve_hint),
    _frozen(std::move(that._frozen)),
    _active(std::move(that._active)),
    _static_block(that._static_block) {
  // Clear data in @a that to indicate all of the memory has been moved.
  that._active_allocated = that._active_reserved = 0;
  that._frozen_allocated = that._frozen_reserved = 0;
  that._reserve_hint                             = 0;
  that._static_block                             = nullptr;
}

MemArena *
MemArena::construct_self_contained(size_t n) {
  MemArena tmp{n + sizeof(MemArena)};
  return tmp.make<MemArena>(std::move(tmp));
}

MemArena &
MemArena::operator=(swoc::MemArena::self_type &&that) noexcept {
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
MemArena::make_block(size_t n) {
  // If there's no reservation hint, use the extent. This is transient because the hint is cleared.
  if (_reserve_hint == 0) {
    if (_active_reserved) {
      _reserve_hint = _active_reserved;
    } else if (_frozen_allocated) { // immediately after freezing - use that extent.
      _reserve_hint = _frozen_allocated;
    }
  }
  n             = std::max<size_t>(n, _reserve_hint);
  _reserve_hint = 0; // did this, clear for next time.
  // Add in overhead and round up to paragraph units.
  n = Paragraph{round_up(n + ALLOC_HEADER_SIZE + sizeof(Block))};
  // If more than a page or withing a quarter page of a full page,
  // round up to page unit size and clip back to account for alloc header.
  if (n >= (Page::SCALE - QuarterPage::SCALE)) {
    n = Page{round_up(n)} - ALLOC_HEADER_SIZE;
  } else if (n >= QuarterPage::SCALE) { // if at least a quarter page, round up to quarter pages.
    n = QuarterPage{round_up(n)};
  }

  // Allocate space for the Block instance and the request memory and construct a Block at the front.
  // In theory this could use ::operator new(n) but this causes a size mismatch during ::operator delete.
  // Easier to use malloc and override @c delete.
  auto free_space   = n - sizeof(Block);
  _active_reserved += free_space;
  return new (::malloc(n)) Block(free_space);
}

MemSpan<void>
MemArena::alloc(size_t n, size_t align) {
  MemSpan<void> zret;
  this->require(n, align);
  auto block         = _active.head();
  zret               = block->alloc(n, align);
  _active_allocated += n;
  // If this block is now full, move it to the back.
  if (block->is_full() && block != _active.tail()) {
    _active.erase(block);
    _active.append(block);
  }
  return zret;
}

MemArena &
MemArena::freeze(size_t n) {
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
MemArena::thaw() {
  this->destroy_frozen();
  _frozen_reserved = _frozen_allocated = 0;
  if (_static_block) {
    _static_block->discard();
    _active.prepend(_static_block);
    _active_reserved += _static_block->remaining();
  }
  return *this;
}

bool
MemArena::contains(const void *ptr) const {
  auto pred = [ptr](const Block &b) -> bool { return b.contains(ptr); };

  return std::any_of(_active.begin(), _active.end(), pred) || std::any_of(_frozen.begin(), _frozen.end(), pred);
}

MemArena &
MemArena::require(size_t n, size_t align) {
  auto spot = _active.begin();
  Block *block{nullptr};

  // Search back through the list until a full block is hit, which is a miss.
  while (spot != _active.end() && !spot->satisfies(n, align)) {
    if (spot->is_full()) {
      spot = _active.end();
    } else {
      ++spot;
    }
  }
  if (spot == _active.end()) {   // no block has enough free space
    block = this->make_block(n); // assuming a new block is sufficiently aligned.
    _active.prepend(block);
  } else if (spot != _active.begin()) {
    // big enough space, move to the head of the list.
    block = spot;
    _active.erase(block);
    _active.prepend(block);
  }
  // Invariant - the head active block has at least @a n bytes of free storage.
  return *this;
}

void
MemArena::destroy_active() {
  auto sb = _static_block; // C++20 nonsense - capture of @a this is incompatible with C++17.
  _active
    .apply([=](Block *b) {
      if (b != sb) {
        delete b;
      }
    })
    .clear();
}

void
MemArena::destroy_frozen() {
  auto sb = _static_block; // C++20 nonsense - capture of @a this is incompatible with C++17.
  _frozen
    .apply([=](Block *b) {
      if (b != sb) {
        delete b;
      }
    })
    .clear();
}

MemArena &
MemArena::clear(size_t hint) {
  _reserve_hint    = hint ? hint : _frozen_allocated + _active_allocated;
  _frozen_reserved = _frozen_allocated = 0;
  _active_reserved = _active_allocated = 0;
  this->destroy_frozen();
  this->destroy_active();

  return *this;
}

MemArena &
MemArena::discard(size_t hint) {
  _reserve_hint = hint ? hint : _frozen_allocated + _active_allocated;
  for (auto &block : _active) {
    block.discard();
  }
  _active_allocated = 0;
  return *this;
}

MemArena::~MemArena() {
  // Destruct in a way that makes it safe for the instance to be in one of its own memory blocks.
  // This means copying members that will be used during the delete.
  Block *ba = _active.head();
  Block *bf = _frozen.head();
  Block *sb = _static_block;

  _active.clear();
  _frozen.clear();
  while (bf) {
    Block *b = bf;
    bf       = bf->_link._next;
    if (b != sb) {
      delete b;
    }
  }
  while (ba) {
    Block *b = ba;
    ba       = ba->_link._next;
    if (b != sb) {
      delete b;
    }
  }
}

#if __has_include(<memory_resource>)
void *
MemArena::do_allocate(std::size_t bytes, std::size_t align) {
  return this->alloc(bytes, align).data();
}

void
MemArena::do_deallocate(void *, size_t, size_t) {}

bool
MemArena::do_is_equal(std::pmr::memory_resource const &that) const noexcept {
  return this == &that;
}
#endif

}} // namespace swoc::SWOC_VERSION_NS
