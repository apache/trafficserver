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

#include <mutex>
#include <memory>
#include <ts/MemSpan.h>

/// Apache Traffic Server commons.
namespace ts
{
/** MemArena is a memory arena for allocations.

    The intended use is for allocating many small chunks of memory - few, large allocations are best handled independently.
    The purpose is to amortize the cost of allocation of each chunk across larger allocations in a heap style. In addition the
    allocated memory is presumed to have similar lifetimes so that all of the memory in the arena can be de-allocatred en masse.

    A generation is essentially a block of memory. The normal workflow is to freeze() the current generation, alloc() a larger and
    newer generation, copy the contents of the previous generation to the new generation, and then thaw() the previous generation.
    Note that coalescence must be done by the caller because MemSpan will only give a reference to the underlying memory.
 */
class MemArena
{
  using self_type = MemArena; ///< Self reference type.
public:
  /** Simple internal arena block of memory. Maintains the underlying memory.
   */
  struct Block {
    size_t size;                 ///< Actual block size.
    size_t allocated{0};         ///< Current allocated (in use) bytes.
    std::shared_ptr<Block> next; ///< Previously allocated block list.

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
    /** Check if the byte at address @a ptr is in this block.
     *
     * @param ptr Address of byte to check.
     * @return @c true if @a ptr is in this block, @c false otherwise.
     */
    bool contains(const void *ptr) const;
  };

  /** Default constructor.
   * Construct with no memory.
   */
  MemArena();
  /** Construct with @a n bytes of storage.
   *
   * @param n Number of bytes in the initial block.
   */
  explicit MemArena(size_t n);

  /** Allocate @a n bytes of storage.

      Returns a span of memory within the arena. alloc() is self expanding but DOES NOT self coalesce. This means
      that no matter the arena size, the caller will always be able to alloc() @a n bytes.

      @param n number of bytes to allocate.
      @return a MemSpan of the allocated memory.
   */
  MemSpan alloc(size_t n);

  /** Adjust future block allocation size.
   * This does not cause allocation, but instead makes a note of the size @a n and when a new block
   * is needed, it will be at least @a n bytes. This is most useful for default constructed instances
   * where the initial allocation should be delayed until use.
   * @param n Minimum size of next allocated block.
   * @return @a this
   */
  self_type &reserve(size_t n);

  /** Freeze memory allocation.

      Will "freeze" a generation of memory. Any memory previously allocated can still be used. This is an
      important distinction as freeze does not mean that the memory is immutable, only that subsequent allocations
      will be in a new generation.

      If @a n == 0, the first block of next generation will be large enough to hold all existing allocations.
      This enables coalescence for locality of reference.

      @param n Number of bytes for new generation.
      @return @c *this
   */
  MemArena &freeze(size_t n = 0);

  /** Unfreeze memory allocation, discard previously frozen memory.

      Will "thaw" away any previously frozen generations. Any generation that is not the current generation is considered
      frozen because there is no way to allocate in any of those memory blocks. thaw() is the only mechanism for deallocating
      memory in the arena (other than destroying the arena itself). Thawing away previous generations means that all spans
      of memory allocated in those generations are no longer safe to use.

      @return @c *this
   */
  MemArena &thaw();

  /** Release all memory.

      Empties the entire arena and deallocates all underlying memory. Next block size will be equal to the sum of all
      allocations before the call to empty.
   */
  MemArena &clear();

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
  size_t extent() const;

private:
  /// creates a new @Block of size @n and places it within the @allocations list.
  /// @return a pointer to the block to allocate from.
  Block *newInternalBlock(size_t n, bool custom);

  static constexpr size_t DEFAULT_BLOCK_SIZE = 1 << 15; ///< 32kb
  static constexpr size_t DEFAULT_PAGE_SIZE  = 1 << 12; ///< 4kb
  static constexpr size_t ALLOC_HEADER_SIZE  = 16;      ///< Guess of overhead of @c malloc
  /// Never allocate less than this.
  static constexpr size_t ALLOC_MIN_SIZE = 2 * ALLOC_HEADER_SIZE;

  size_t current_alloc = 0; ///< Total allocations in the active generation.
  /// Total allocations in the previous generation. This is only non-zero while the arena is frozen.
  size_t prev_alloc = 0;

  size_t next_block_size = DEFAULT_BLOCK_SIZE; ///< Next internal block size

  std::shared_ptr<Block> prev    = nullptr; ///< Previous generation.
  std::shared_ptr<Block> current = nullptr; ///< Head of allocations list. Allocate from this.
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

inline MemArena::MemArena() {}

inline MemSpan
MemArena::Block::remnant()
{
  return {this->data() + allocated, static_cast<ptrdiff_t>(this->remaining())};
}

inline size_t
MemArena::size() const
{
  return current_alloc;
}

inline size_t
MemArena::allocated_size() const
{
  return prev_alloc + current_alloc;
}

inline MemArena &
MemArena::reserve(size_t n)
{
  next_block_size = n;
  return *this;
}

inline size_t
MemArena::remaining() const
{
  return current ? current->remaining() : 0;
}

inline MemSpan
MemArena::remnant() const
{
  return current ? current->remnant() : MemSpan{};
}

} // namespace ts
