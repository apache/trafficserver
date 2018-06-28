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
public:
  /** Simple internal arena block of memory. Maintains the underlying memory.
   */
  struct Block {
    size_t size;
    size_t allocated;
    std::shared_ptr<Block> next;

    Block(size_t n);
    char *data();
  };

  MemArena();
  explicit MemArena(size_t n);

  /** MemSpan alloc(size_t n)

      Returns a span of memory within the arena. alloc() is self expanding but DOES NOT self coalesce. This means
      that no matter the arena size, the caller will always be able to alloc() @a n bytes.

      @param n number of bytes to allocate.
      @return a MemSpan of the allocated memory.
   */
  MemSpan alloc(size_t n);

  /** MemArena& freeze(size_t n = 0)

      Will "freeze" a generation of memory. Any memory previously allocated can still be used. This is an
      important distinction as freeze does not mean that the memory is immutable, only that subsequent allocations
      will be in a new generation.

      @param n Number of bytes for new generation.
        if @a n == 0, the next generation will be large enough to hold all existing allocations.
      @return @c *this
   */
  MemArena &freeze(size_t n = 0);

  /** MemArena& thaw()

      Will "thaw" away any previously frozen generations. Any generation that is not the current generation is considered
      frozen because there is no way to allocate in any of those memory blocks. thaw() is the only mechanism for deallocating
      memory in the arena (other than destroying the arena itself). Thawing away previous generations means that all spans
      of memory allocated in those generations are no longer safe to use.

      @return @c *this
   */
  MemArena &thaw();

  /** MemArena& empty

      Empties the entire arena and deallocates all underlying memory. Next block size will be equal to the sum of all
      allocations before the call to empty.
   */
  MemArena &empty();

  /// @returns the current generation @c size.
  size_t
  size() const
  {
    return arena_size;
  }

  /// @returns the @c remaining space within the generation.
  size_t
  remaining() const
  {
    return (current) ? current->size - current->allocated : 0;
  }

  /// @returns the total number of bytes allocated within the arena.
  size_t
  allocated_size() const
  {
    return total_alloc;
  }

  /// @returns the number of bytes that have not been allocated within the arena
  size_t
  unallocated_size() const
  {
    return size() - allocated_size();
  }

  /// @return a @c true if @ptr is in memory owned by this arena, @c false if not.
  bool contains(void *ptr) const;

private:
  /// creates a new @Block of size @n and places it within the @allocations list.
  /// @return a pointer to the block to allocate from.
  Block *newInternalBlock(size_t n, bool custom);

  static constexpr size_t DEFAULT_BLOCK_SIZE = 1 << 15; ///< 32kb
  static constexpr size_t DEFAULT_PAGE_SIZE  = 1 << 12; ///< 4kb
  static constexpr size_t ALLOC_HEADER_SIZE  = 16;

  /** generation_size and prev_alloc are used to help quickly figure out the arena
        info (arena_size and total_alloc) after a thaw().
   */
  size_t arena_size      = DEFAULT_BLOCK_SIZE; ///< --all
  size_t generation_size = 0;                  ///< Size of current generation -- all
  size_t total_alloc     = 0;                  ///< Total number of bytes allocated in the arena -- allocated
  size_t prev_alloc      = 0;                  ///< Total allocations before current generation -- allocated

  size_t next_block_size = 0; ///< Next internal block size

  std::shared_ptr<Block> generation = nullptr; ///< Marks current generation
  std::shared_ptr<Block> current    = nullptr; ///< Head of allocations list. Allocate from this.
};
} // namespace ts
