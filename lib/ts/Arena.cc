/** @file

  A brief file description

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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/Allocator.h"
#include "ts/Arena.h"
#include <cassert>
#include <cstring>

#define DEFAULT_ALLOC_SIZE 1024
#define DEFAULT_BLOCK_SIZE (DEFAULT_ALLOC_SIZE - (sizeof(ArenaBlock) - 8))

static Allocator defaultSizeArenaBlock("ArenaBlock", DEFAULT_ALLOC_SIZE);

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static inline ArenaBlock *
blk_alloc(int size)
{
  ArenaBlock *blk;

  if (size == DEFAULT_BLOCK_SIZE) {
    blk = (ArenaBlock *)defaultSizeArenaBlock.alloc_void();
  } else {
    blk = (ArenaBlock *)ats_malloc(size + sizeof(ArenaBlock) - 8);
  }

  blk->next          = nullptr;
  blk->m_heap_end    = &blk->data[size];
  blk->m_water_level = &blk->data[0];

  return blk;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static inline void
blk_free(ArenaBlock *blk)
{
  int size;

  size = blk->m_heap_end - &blk->data[0];
  if (size == DEFAULT_BLOCK_SIZE) {
    defaultSizeArenaBlock.free_void(blk);
  } else {
    ats_free(blk);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static void *
block_alloc(ArenaBlock *block, size_t size, size_t alignment)
{
  char *mem;

  mem = block->m_water_level;
  if (((size_t)mem) & (alignment - 1)) {
    mem += (alignment - ((size_t)mem)) & (alignment - 1);
  }

  if ((block->m_heap_end >= mem) && (((size_t)block->m_heap_end - (size_t)mem) >= size)) {
    block->m_water_level = mem + size;
    return mem;
  }

  return nullptr;
}

void *
Arena::alloc(size_t size, size_t alignment)
{
  ArenaBlock *b;
  unsigned int block_size;
  void *mem;

  ink_assert((alignment & (alignment - 1)) == 0);

  b = m_blocks;
  while (b) {
    mem = block_alloc(b, size, alignment);
    if (mem) {
      return mem;
    }
    b = b->next;
  }

  block_size = (unsigned int)(size * 1.5);
  if (block_size < DEFAULT_BLOCK_SIZE) {
    block_size = DEFAULT_BLOCK_SIZE;
  }

  b        = blk_alloc(block_size);
  b->next  = m_blocks;
  m_blocks = b;

  mem = block_alloc(b, size, alignment);
  return mem;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
Arena::free(void *mem, size_t size)
{
  if (m_blocks) {
    ArenaBlock *b;

    b = m_blocks;
    while (b->next) {
      b = b->next;
    }

    if (b->m_water_level == ((char *)mem + size)) {
      b->m_water_level = (char *)mem;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
Arena::reset()
{
  ArenaBlock *b;

  while (m_blocks) {
    b = m_blocks->next;
    blk_free(m_blocks);
    m_blocks = b;
  }
  ink_assert(m_blocks == nullptr);
}
