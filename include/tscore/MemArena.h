/** @file

    Memory arena for allocations

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

#pragma once

#include <new>
#include <mutex>
#include <memory>
#include <utility>
#include "tscpp/util/MemSpan.h"
#include "tscore/Scalar.h"
#include <tsconfig/IntrusivePtr.h>

/// Apache Traffic Server commons.
namespace ts
{
/** A memory arena.

    The intended use is for allocating many small chunks of memory - few, large allocations are best
    handled through other mechanisms. The purpose is to amortize the cost of allocation of each
    chunk across larger internal allocations ("reserving memory"). In addition the allocated memory
    chunks are presumed to have similar lifetimes so all of the memory in the arena can be released
    when the arena is destroyed.
 */
class MemArena
{
  using self_type = MemArena; ///< Self reference type.
protected:
  struct Block; // Forward declare
  using BlockPtr = ts::IntrusivePtr<Block>;
  friend struct IntrusivePtrPolicy<Block>;
  /** Simple internal arena block of memory. Maintains the underlying memory.
   *
   * Intrusive pointer is used to keep all of the memory in this single block. This struct is just
   * the header on the full memory block allowing the raw memory and the meta data to be obtained
   * in a single memory allocation.
   */
  struct Block : public ts::IntrusivePtrCounter {
    size_t size;         ///< Actual block size.
    size_t allocated{0}; ///< Current allocated (in use) bytes.
    BlockPtr next;       ///< List of previous blocks.

    /** Construct to have @a n bytes of available storage.
     *
     * Note this is descriptive - this presumes use via placement new and the size value describes
     * memory already allocated immediately after this instance.
     * @param n The amount of storage.
     */
    Block(size_t n);

    /// Get the start of the data in this block.
    char *data();

    /// Get the start of the data in this block.
    const char *data() const;

    /// Amount of unallocated storage.
    size_t remaining() const;

    /// Span of unallocated storage.
    MemSpan remnant();

    /** Allocate @a n bytes from this block.
     *
     * @param n Number of bytes to allocate.
     * @return The span of memory allocated.
     */
    MemSpan alloc(size_t n);

    /** Check if the byte at address @a ptr is in this block.
     *
     * @param ptr Address of byte to check.
     * @return @c true if @a ptr is in this block, @c false otherwise.
     */
    bool contains(const void *ptr) const;

    /** Override standard delete.
     *
     * This is required because the allocated memory size is larger than the class size which requires
     * calling @c free differently.
     *
     * @param ptr Memory to be de-allocated.
     */
    static void operator delete(void *ptr);
  };

public:
  /** Construct with reservation hint.
   *
   * No memory is initially reserved, but when memory is needed this will be done so at least
   * @a n bytes of available memory is reserved.
   *
   * To pre-reserve call @c alloc(0), e.g.
   * @code
   * MemArena arena(512); // Make sure at least 512 bytes available in first block.
   * arena.alloc(0); // Force allocation of first block.
   * @endcode
   *
   * @param n Minimum number of available bytes in the first internally reserved block.
   */
  explicit MemArena(size_t n = DEFAULT_BLOCK_SIZE);

  /** Allocate @a n bytes of storage.

      Returns a span of memory within the arena. alloc() is self expanding but DOES NOT self
      coalesce. This means that no matter the arena size, the caller will always be able to alloc()
      @a n bytes.

      @param n number of bytes to allocate.
      @return a MemSpan of the allocated memory.
   */
  MemSpan alloc(size_t n);

  /** Allocate and initialize a block of memory.

      The template type specifies the type to create and any arguments are forwarded to the constructor. Example:
      @code
      struct Thing { ... };
      Thing* thing = arena.make<Thing>(...constructor args...);
      @endcode

      Do @b not call @c delete an object created this way - that will attempt to free the memory and break. A
      destructor may be invoked explicitly but the point of this class is that no object in it needs to be
      deleted, the memory will all be reclaimed when the Arena is destroyed. In general it is a bad idea
      to make objects in the Arena that own memory that is not also in the Arena.
  */
  template <typename T, typename... Args> T *make(Args &&... args);

  /** Freeze reserved memory.

      All internal memory blocks are frozen and will not be involved in future allocations. Subsequent
      allocation will reserve new internal blocks. By default the first reserved block will be large
      enough to contain all frozen memory. If this is not correct a different target can be
      specified as @a n.

      @param n Target number of available bytes in the next reserved internal block.
      @return @c *this
   */
  MemArena &freeze(size_t n = 0);

  /** Unfreeze arena.
   *
   * Frozen memory is released.
   *
   * @return @c *this
   */
  self_type &thaw();

  /** Release all memory.

      Empties the entire arena and deallocates all underlying memory. The hint for the next reserved block size will
      be @a n if @a n is not zero, otherwise it will be the sum of all allocations when this method was called.

      @return @c *this

   */
  MemArena &clear(size_t n = 0);

  /// @returns the memory allocated in the generation.
  size_t size() const;

  /// @returns the @c remaining space within the generation.
  size_t remaining() const;

  /// @returns the remaining contiguous space in the active generation.
  MemSpan remnant() const;

  /// @returns the total number of bytes allocated within the arena.
  size_t allocated_size() const;

  /** Check if a the byte at @a ptr is in memory owned by this arena.
   *
   * @param ptr Address of byte to check.
   * @return @c true if the byte at @a ptr is in the arena, @c false if not.
   */
  bool contains(const void *ptr) const;

  /** Total memory footprint, including wasted space.
   * @return Total memory footprint.
   */
  size_t reserved_size() const;

protected:
  /** Internally allocates a new block of memory of size @a n bytes.
   *
   * @param n Size of block to allocate.
   * @return
   */
  BlockPtr make_block(size_t n);

  using Page      = ts::Scalar<4096>; ///< Size for rounding block sizes.
  using Paragraph = ts::Scalar<16>;   ///< Minimum unit of memory allocation.

  static constexpr size_t ALLOC_HEADER_SIZE = 16; ///< Guess of overhead of @c malloc
  /// Initial block size to allocate if not specified via API.
  static constexpr size_t DEFAULT_BLOCK_SIZE = Page::SCALE - Paragraph{round_up(ALLOC_HEADER_SIZE + sizeof(Block))};

  size_t _active_allocated = 0; ///< Total allocations in the active generation.
  size_t _active_reserved  = 0; ///< Total current reserved memory.
  /// Total allocations in the previous generation. This is only non-zero while the arena is frozen.
  size_t _prev_allocated = 0;
  /// Total frozen reserved memory.
  size_t _prev_reserved = 0;

  /// Minimum free space needed in the next allocated block.
  /// This is not zero iff @c reserve was called.
  size_t _reserve_hint = 0;

  BlockPtr _prev;   ///< Previous generation, frozen memory.
  BlockPtr _active; ///< Current generation. Allocate here.
};

// Implementation

inline MemArena::Block::Block(size_t n) : size(n) {}

inline char *
MemArena::Block::data()
{
  return reinterpret_cast<char *>(this + 1);
}

inline const char *
MemArena::Block::data() const
{
  return reinterpret_cast<const char *>(this + 1);
}

inline bool
MemArena::Block::contains(const void *ptr) const
{
  const char *base = this->data();
  return base <= ptr && ptr < base + size;
}

inline size_t
MemArena::Block::remaining() const
{
  return size - allocated;
}

inline MemSpan
MemArena::Block::alloc(size_t n)
{
  ink_assert(n <= this->remaining());
  MemSpan zret = this->remnant().prefix(n);
  allocated += n;
  return zret;
}

template <typename T, typename... Args>
T *
MemArena::make(Args &&... args)
{
  return new (this->alloc(sizeof(T)).data()) T(std::forward<Args>(args)...);
}

inline MemArena::MemArena(size_t n) : _reserve_hint(n) {}

inline MemSpan
MemArena::Block::remnant()
{
  return {this->data() + allocated, static_cast<ptrdiff_t>(this->remaining())};
}

inline size_t
MemArena::size() const
{
  return _active_allocated;
}

inline size_t
MemArena::allocated_size() const
{
  return _prev_allocated + _active_allocated;
}

inline size_t
MemArena::remaining() const
{
  return _active ? _active->remaining() : 0;
}

inline MemSpan
MemArena::remnant() const
{
  return _active ? _active->remnant() : MemSpan{};
}

inline size_t
MemArena::reserved_size() const
{
  return _active_reserved + _prev_reserved;
}

} // namespace ts
