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

/**@name Allocation routines
  Block allocation management.
  Currently, the documentation is exposed, but it is intended that
  <strong><em>only</em></strong> implementors of block cache make use
  of these calls.
 */

// this whole file is in Allocation routines group
//@{

// allocator test 2.

// simple bitmap

/*
 * TODO better fragementation stats
 * TODO better simulation of pinned documents
 * TODO simulation of streaming document allocation sizes
 * TODO regression tests for allocation scenarios
 * TODO fix allocation of chunks >= 128.
 *
 */

/* observations:

 * fragmentation increases gradually as we reuse space in the
 * non-pinned region.  Maybe this suggests partitioning pinned and
 * non-pinned documents into different regions.  For greater fraction
 * of pinned space, fragmentation increases faster.
 
 * for both 5% pinned or 75% pinned, top level RLE map is on the order
 * of 4 entries, so probably not expensive to maintain.  */

#include "sizes.h"              // get allocation trace
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

                                // how many allocation to do for
                                // timing purposes

//#define PRELOAD_COUNT 30
#define TEST_COUNT 20000000

// fraction of cache that is pinned.
int pct_pinned = 75;

// when cache is full, new documents coming in are not pinned if this is true.
int steady_state_is_unpinned_incoming = 1;

// if this is true, then we don't measure op time (because some is
// chewed up with the stats.)
int print_fraginfo = 1;

int maskdebug = 0;
int fmaskdebug = 0;
int rledebug = 0;
int rlecheck = 0;
int regression = 0;

// simulate a 128GB disk which  consists of 4kb blocks
//#define DISKSIZE (128LL * (1<<30))
// start with a 2MB disk.

#define DISKSIZE (128LL * (1<<30))

// 10MB
//#define DISKSIZE (64LL * (1<<20))

#define MB (1<<20)
#define BLOCK_SHIFT 12
#define BLOCK_SIZE (1 << BLOCK_SHIFT)
#define MAX_BLOCKS (DISKSIZE >> BLOCK_SHIFT)

#define MAX_EXTENT_BYTES (1 << 19)
#define MAX_EXTENT_BLOCKS (MAX_EXTENT_BYTES >> BLOCK_SHIFT)

typedef struct
{
  unsigned short bitmap;
} lt16freemap;
typedef struct
{
  unsigned char bitmap;
  unsigned char partialmap;
} lt128freemap;
typedef struct
{
  unsigned int bitmap;
  unsigned int partialmap;
} freemap4k;

typedef struct
{
  unsigned short wasted:2;
#define CM_ALLOC 0
#define CM_FREE 1
  unsigned short state:1;       // 1 partially or completely free, 0 fully alloc.
  unsigned short count:13;      // 0 == 1, 8191 == 8192 (2^13-1)
} rle;

#define N_LT16_FREEMAP (MAX_BLOCKS >> 4)
#define OFFSET_TO_LT16(off) ((off) >> 4)
#define OFFSET_TO_LT16bit(off) ((off) & 0x0f)
lt16freemap *lt16;

#define N_LT128_FREEMAP (N_LT16_FREEMAP >> 3)
#define LT128_OFFSET_TO_LT16(m128,bit) (((m128) << 3) + bit)
#define OFFSET_TO_LT128(off) (OFFSET_TO_LT16(off) >> 3)
#define OFFSET_TO_LT128bit(off) (OFFSET_TO_LT16(off) & 0x07)
lt128freemap *lt128;

#define N_DISK_FREEMAP (N_LT128_FREEMAP >> 5)
#define DM_OFFSET_TO_LT128(m4096,bit) (((m4096) << 5) + bit)
#define OFFSET_TO_DM(off) (OFFSET_TO_LT128(off) >> 5)
#define OFFSET_TO_DMbit(off) (OFFSET_TO_LT128(off) & 0x1f)
#define N_LT128_IN_DM 32
freemap4k *diskmap;

#define TO_BLOCK(m16,b) (((m16) << 4) + (b))

rle *compressedmap;
int cmap_max;

// bits are:  0 1 2 3
char first_set4[] = {
  -1, 3, 2, 2,                  // 0000 to 0011
  1, 1, 1, 1,                   // 0100 to 0111
  0, 0, 0, 0,                   // 1000 to 1011
  0, 0, 0, 0                    // 1100 to 1111
};

// starting from bit 0, runlength of 1's
char run_len4[] = {
  // 0000 0001 0010 0011
  0, 0, 0, 0,
  // 0100 to 0111
  0, 0, 0, 0,
  // 1000 1001 1010 1011
  1, 1, 1, 1,
  // 1100 1101 1110 1111
  2, 2, 3, 4
};

#define CLEAR_BIT8(b)  ~(1 << (7 - b))

#define CLEAR_BIT16(b)  ~(1 << (15 - b))
#define CLEAR_BIT32(b)  ~(1 << (31 - b))

#define SET_BIT8(b) (1 << (7-b))
#define SET_BIT16(b) (1 << (15 - b))
#define SET_BIT32(b) (1 << (31 - b))

void
here()
{                               // breakpoint
}

char zero_count4[] = {
  4, 3, 3, 2,                   // 0000 to 0011
  3, 2, 2, 1,                   // 0100 to 0111
  3, 2, 2, 1,                   // 1000 to 1011
  2, 1, 1, 0                    // 1100 to 1111
};

int
count8_zeros(unsigned char b)
{
  return zero_count4[b & 0x0f] + zero_count4[(b >> 4) & 0x0f];
}

int
count16_zeros(unsigned short b)
{
  return count8_zeros(b & 0xff) + count8_zeros((b >> 8) & 0xff);
}


unsigned short
set_range16(int first, int last)
{
  unsigned short mask = (0xffff >> 15 - last);
  mask = mask << (15 - last + first);
  mask = mask >> first;
  return mask;
}

unsigned short
clear_range16(int first, int last)
{
  return ~set_range16(first, last);
}

unsigned char
set_range8(int first, int last)
{
  unsigned char mask = 0xff >> (7 - last);
  mask = mask << (7 - last + first);
  mask = mask >> first;
  return mask;
}

unsigned int
set_range32(int first, int last)
{
  unsigned int mask = 0xffffffff;
  mask = mask >> (31 - last);
  mask = mask << (31 - last + first);
  mask = mask >> first;
  return mask;
}

int
first_set8(unsigned char bitmap, int *runlen)
{
  int bit;
  if (bitmap & 0xf0) {
    bit = first_set4[bitmap >> 4];
  } else {
    bit = 4 + first_set4[bitmap & 0x0f];
  }
  int sum = 0, l;
  unsigned char newmap = bitmap << bit;
  while (newmap) {
    l = run_len4[newmap >> 4];
    sum += l;
    if (l == 4) {
      newmap = newmap << 4;
    } else {
      break;
    }
  }
  *runlen = sum;
  return bit;
}

int
first_set16(unsigned short bitmap, int *runlen)
{
  int bit;
  if (bitmap & 0xff00) {
    if (bitmap & 0xf000) {
      bit = first_set4[(bitmap >> 12) & 0x0f];
    } else {
      bit = 4 + first_set4[(bitmap >> 8) & 0x0f];
    }
  } else {
    if (bitmap & 0x00f0) {
      bit = 8 + first_set4[(bitmap >> 4) & 0x0f];
    } else {
      bit = 12 + first_set4[bitmap & 0x0f];
    }
  }
  // also return runlen
  int sum = 0, l;
  int newmap = (bitmap << bit);
  while (newmap) {
    l = run_len4[(newmap >> 12) & 0x0f];
    sum += l;
    if (l == 4) {
      newmap = newmap << 4;
    } else {
      break;
    }
  }
  *runlen = sum;
  return bit;
}

int
first_set32(unsigned int bitmap, int *runlen)
{
  int bit;
  if (bitmap & 0xffff0000) {
    bit = first_set16(bitmap >> 16 & 0xffff, runlen);
    int also;
    if (0 == first_set16(bitmap & 0xffff, &also)) {
      *runlen += also;
    }
    return bit;
  } else {
    return 16 + first_set16(bitmap & 0xffff, runlen);
  }
}

void
freeAll()
{
  memset(lt16, 0xff, sizeof(lt16freemap) * N_LT16_FREEMAP);
  memset(lt128, 0xff, sizeof(lt128freemap) * N_LT128_FREEMAP);
  memset(diskmap, 0xff, sizeof(freemap4k) * N_DISK_FREEMAP);
  compressedmap[0].state = CM_FREE;
  compressedmap[0].count = N_DISK_FREEMAP - 1;
  cmap_max = 1;
}

void
allocInit()
{
  lt16 = (lt16freemap *) calloc(sizeof(lt16freemap), N_LT16_FREEMAP);
  lt128 = (lt128freemap *) calloc(sizeof(lt128freemap), N_LT128_FREEMAP);
  diskmap = (freemap4k *) calloc(sizeof(freemap4k), N_DISK_FREEMAP);
  compressedmap = (rle *) calloc(sizeof(rle), N_DISK_FREEMAP * 2);
  cmap_max = 0;

  freeAll();
  printf("16freemap: %d bytes, 128freemap: %d, diskmap %d bytes\n",
         (int) (N_LT16_FREEMAP * sizeof(lt16freemap)),
         (int) (N_LT128_FREEMAP * sizeof(lt128freemap)), (int) (N_DISK_FREEMAP * sizeof(freemap4k)));
  int sum = (int) ((N_LT16_FREEMAP * sizeof(lt16freemap)) +
                   (N_LT128_FREEMAP * sizeof(lt128freemap)) + (N_DISK_FREEMAP * sizeof(freemap4k)));
  printf("pct is %5.4f%\n", sum * 100.0 / DISKSIZE);
}

void
print_rlestate()
{
  int i = 0;
  int firstblock = 0;
  while (i < cmap_max) {
    printf("%d-%d: %s\n", firstblock, firstblock + compressedmap[i].count, compressedmap[i].state ? "free" : "alloc");
    firstblock += compressedmap[i].count + 1;
    i++;
  }
}

void
check_rlestate()
{
  int i = 0;
  int firstblock = 0;
  int prevstate = -1;
  while (i < cmap_max) {
    assert(compressedmap[i].state != prevstate);
    prevstate = compressedmap[i].state;
    firstblock += (compressedmap[i].count + 1) * (32 * 8 * 16);
    i++;
  }
  assert(firstblock == MAX_BLOCKS);
}

void
update(unsigned int block, unsigned int newstate)
{
  if (rledebug) {
    printf("%d -> %d\n", block, newstate);
    print_rlestate();
  }
  assert(block < N_DISK_FREEMAP);
  int i = 0;
  int firstblock = 0;
  while (firstblock <= block) {
    int count = compressedmap[i].count + 1;
    if (rledebug)
      printf("firstblock = %d, firstblock+count = %d\n", firstblock, firstblock + count);
    if (firstblock + count == block) {
      // just past end of region, so we can extend or reduce this block.
      if (newstate == compressedmap[i].state) {
        // extend current (same as newstate) region
        compressedmap[i].count++;
        assert(i + 1 < cmap_max);
        if (compressedmap[i + 1].count == 0) {  // just remove it
          // merge next block and shift over if it exists
          if (i + 2 < cmap_max) {
            // now reduce subsequent block if it exists
            compressedmap[i].count += compressedmap[i + 2].count + 1;
            if (i + 3 < cmap_max) {
              memmove(&compressedmap[i + 1], &compressedmap[i + 3], sizeof(compressedmap) * (cmap_max - (i + 1)));
            }
            cmap_max -= 2;
          } else {
            // else just remove subsequent block
            cmap_max--;
          }
          assert(cmap_max > 0);
          if (rlecheck)
            check_rlestate();
        } else {
          compressedmap[i + 1].count--;
          if (rlecheck)
            check_rlestate();
        }
        return;
      } else {
        // extend subsequent (same as newstate) region
        compressedmap[i].count--;
        // now extend subsequent block
        compressedmap[i + 1].count++;
        if (rlecheck)
          check_rlestate();
        return;
      }
    } else if (block < firstblock + count) {    // need to split this block
      // firstblock... block..firstblock+count.
      int untilblock = (block - firstblock);
      // == # of entries until block (change of state).
      int afterblock = (firstblock + count - block - 1);
      // == # of entries after block with original state, not including block
      if (rledebug)
        printf("until %d, after %d\n", untilblock, afterblock);

      // either
      //   B ->  ACD (if untilblock > 0)
      //   B ->  CB' (if untilblock == 0)
      if (untilblock > 0) {
        // make space
        if (afterblock > 0) {
          memmove(&compressedmap[i + 3], &compressedmap[i + 1], sizeof(compressedmap) * (cmap_max - (i + 1)));
          compressedmap[i].count = untilblock - 1;
          compressedmap[i + 1].state = newstate;
          compressedmap[i + 1].count = 0;
          compressedmap[i + 2].state = compressedmap[i].state;
          compressedmap[i + 2].count = afterblock - 1;
          assert(compressedmap[i].state == compressedmap[i + 2].state);
          assert(compressedmap[i].state != newstate);
          cmap_max += 2;
          assert(cmap_max > 0);
          if (rlecheck)
            check_rlestate();
        } else {
          //print_rlestate();
          // else we should decrement this block, increment next block
          compressedmap[i].count--;
          assert(compressedmap[i + 1].state == newstate);
          compressedmap[i + 1].count++;
#if 0
          memmove(&compressedmap[i + 2], &compressedmap[i + 1], sizeof(compressedmap) * (cmap_max - (i + 1)));
          compressedmap[i].count = untilblock - 1;
          compressedmap[i + 1].state = newstate;
          compressedmap[i + 1].count = 0;
          assert(compressedmap[i].state != newstate);
          cmap_max++;
#endif
          assert(cmap_max > 0);
          if (rlecheck)
            check_rlestate();
        }
      } else {
        if (newstate == compressedmap[i].state) {
          assert(!"shouldn't happen");
          if (compressedmap[i].count == 0) {
            compressedmap[i + 1].count++;
            // remove B'.
            memmove(&compressedmap[i], &compressedmap[i + 1], sizeof(compressedmap) * (cmap_max - (i + 1)));
            cmap_max--;
            assert(cmap_max > 0);
            if (rlecheck)
              check_rlestate();
          } else {
            compressedmap[i].count--;
            compressedmap[i + 1].count++;
            if (rlecheck)
              check_rlestate();
          }
        } else {
          if (compressedmap[i].count == 0) {
            // BC -> C'
            // merge into C'
            memmove(&compressedmap[i], &compressedmap[i + 1], sizeof(compressedmap) * (cmap_max - (i + 1)));
            cmap_max--;
            compressedmap[i].count++;
            assert(cmap_max > 0);
            assert(compressedmap[i].state == newstate);
            if (rlecheck)
              check_rlestate();
          } else {
            // B -> CB'
            memmove(&compressedmap[i + 1], &compressedmap[i], sizeof(compressedmap) * (cmap_max - i));
            compressedmap[i].count = untilblock;
            compressedmap[i].state = newstate;
            compressedmap[i + 1].count--;
            cmap_max++;
            assert(cmap_max > 0);
            if (rlecheck)
              check_rlestate();
          }
        }
      }
      return;
    }
    firstblock += count;
    i++;
  }
  assert(!"shouldn't reach here");
}

void
test_rle0()
{
  print_rlestate();
  update(0, 0);
  print_rlestate();
  update(0, 1);
  print_rlestate();
  update(1, 0);
  print_rlestate();
  update(2, 0);
  print_rlestate();
  update(1, 1);
  print_rlestate();
  update(8191, 0);
  print_rlestate();
  update(8191, 1);
  print_rlestate();
}

void
first_dm(freemap4k ** dm, lt128freemap ** l128, int *bit)
{
  *dm = NULL;
  *l128 = NULL;
  int i = 0;
  int dm_i = 0;
  while (i < cmap_max) {
    if (compressedmap[i].state == CM_FREE) {
      // found region of partially allocated blocks
      *dm = &diskmap[dm_i];
      if (maskdebug)
        printf("at %d, disk partial map is %08x\n", (*dm) - diskmap, (*dm)->partialmap);
      break;
    }
    dm_i += compressedmap[i].count + 1;
    i++;
  }
  if (!*dm) {                   // out of space
    return;
  }
  if ((*dm) - diskmap < N_DISK_FREEMAP) {
    // find which 128 block chunk
    int b128len;
    *bit = first_set32((*dm)->partialmap, &b128len);
    *l128 = &lt128[DM_OFFSET_TO_LT128((*dm) - diskmap, *bit)];
    assert(((*l128) - lt128) < N_LT128_FREEMAP);
  }
}

// XXX: need to make use compressedmap
void
next_dm(freemap4k ** dm, lt128freemap ** l128, int *bit)
{
  (*dm)++;
  *l128 = NULL;
  while (((*dm) - diskmap) < N_DISK_FREEMAP) {
    if ((*dm)->partialmap) {
      break;
    }
    (*dm)++;
  }
  if ((*dm) - diskmap < N_DISK_FREEMAP) {
    // find which 128 block chunk
    int b128len;
    *bit = first_set8((*dm)->partialmap, &b128len);
    *l128 = &lt128[DM_OFFSET_TO_LT128(*dm - diskmap, *bit)];
    assert((*l128 - lt128) < N_LT128_FREEMAP);
  }
}

void
first_partial16(lt128freemap * l128, lt16freemap ** l16, int *bit16, int *bit, int *runlen)
{
  // find which 16block chunk is free.
  int b16len;
  *bit16 = first_set8(l128->partialmap, &b16len);
  *l16 = &lt16[LT128_OFFSET_TO_LT16(l128 - lt128, *bit16)];
  *bit = first_set16((*l16)->bitmap, runlen);
  assert(runlen > 0);
}

void
first_full16(lt128freemap * l128, lt16freemap ** l16, int *bit16)
{
  // find which 16block chunk is completely free.
  int b16len;
  if (l128->bitmap) {
    *bit16 = first_set8(l128->bitmap, &b16len);
    *l16 = &lt16[LT128_OFFSET_TO_LT16(l128 - lt128, *bit16)];
    assert((*l16)->bitmap == 0xffff);
  } else {
    // all are partial allocated
    *l16 = NULL;
  }
}

extern void pmap128_mark(unsigned int offset, int partial, int all);
extern void pmap128_unmark(unsigned int offset, int partial, int all);
extern void pmap4k_mark(unsigned int offset, int partial, int all);

void
pmap16_unmarkpartial(unsigned int offset, int len)
{
  int b = OFFSET_TO_LT16bit(offset);
  // only allocate up to runlen
  unsigned short m = clear_range16(b, b + len - 1);
  if (maskdebug)
    printf("clear mask for l16: %04x (%d through %d)\n", (unsigned int) m, b, b + len - 1);

  int lt16o = OFFSET_TO_LT16(offset);
  lt16freemap *freemap = &lt16[lt16o];
  freemap->bitmap &= m;
  // unmark 128map
  if (freemap->bitmap == 0x0000) {
    pmap128_unmark(offset, 1, 1);
  } else {
    pmap128_unmark(offset, 0, 1);       // leave partial set.
  }
}

void
pmap16_markpartial(unsigned int offset, int len)
{
  int lt16o = OFFSET_TO_LT16(offset);
  lt16freemap *freemap = &lt16[lt16o];
  int first = OFFSET_TO_LT16bit(offset);
  unsigned short mask = set_range16(first, first + len - 1);
  assert(first + len - 1 <= 15);
  if (fmaskdebug) {
    printf("lt16: %d, b16: %d, mask=%04x\n", lt16o, first, mask);
  }
  freemap->bitmap |= mask;
  if (freemap->bitmap == 0xffff) {
    // time to coalesce
    pmap128_mark(offset & 0xfffffff0, 1, 1);
  } else {
    // just mark lt128 and diskmap as partially free
    pmap128_mark(offset, 1, 0);
    pmap4k_mark(offset, 1, 0);
  }
}

void
pmap16_markall(unsigned int offset)
{
  int lt16o = OFFSET_TO_LT16(offset);
  lt16freemap *freemap = &lt16[lt16o];
  int first = OFFSET_TO_LT16bit(offset);
  freemap->bitmap = 0xffff;
  // coalesce
  pmap128_mark(offset & 0xfffffff0, 1, 1);
}

void
pmap128_mark(unsigned int offset, int partial, int all)
{
  int lt128o = OFFSET_TO_LT128(offset);
  int b128 = OFFSET_TO_LT128bit(offset);
  lt128freemap *b128freemap = &lt128[lt128o];
  unsigned char mask = SET_BIT8(b128);
  if (partial)
    b128freemap->partialmap |= mask;
  if (all)
    b128freemap->bitmap |= mask;
  if (fmaskdebug) {
    printf("lt128: %d, b128: %d, mask=%02x (<16)\n", lt128o, b128, mask);
  }
  if (b128freemap->bitmap == 0xff) {
    pmap4k_mark(offset & 0xffffff80, 1, 1);
  } else {
    pmap4k_mark(offset & 0xffffff80, 1, 0);
  }
}

extern void pmap4k_unmark(unsigned int offset, int partial, int all);
/* given offset,
 * mark as not completely or not partially (i.e. completely) unallocated
 */
void
pmap128_unmark(unsigned int offset, int partial, int all)
{
  // API check:
  assert(!(partial && !all));

  int lt128o = OFFSET_TO_LT128(offset);
  int b128 = OFFSET_TO_LT128bit(offset);
  lt128freemap *b128freemap = &lt128[lt128o];
  unsigned char mask = CLEAR_BIT8(b128);
  if (maskdebug)
    printf("clear mask for l128: %02x\n", mask);
  if (partial)
    b128freemap->partialmap &= mask;
  if (all)
    b128freemap->bitmap &= mask;
  if (b128freemap->partialmap == 0x00) {
    pmap4k_unmark(offset & 0xffffff80, 1, 1);
  } else {
    pmap4k_unmark(offset & 0xffffff80, 0, 1);   // leave partial set
  }
}

void
pmap4k_unmark(unsigned int offset, int partial, int all)
{
  int dmapo = OFFSET_TO_DM(offset);
  int bdmap = OFFSET_TO_DMbit(offset);
  freemap4k *freemap = &diskmap[dmapo];
  unsigned int lmask = CLEAR_BIT32(bdmap);
  unsigned int prev = freemap->partialmap;
  if (maskdebug)
    printf("clear mask for diskmap: %08x\n", lmask);
  if (partial)
    freemap->partialmap &= lmask;
  if (all)
    freemap->bitmap &= lmask;
  if (prev && !freemap->partialmap) {
    update(dmapo, 0);           // all 128blocks are alloc.
  }
}

void
pmap4k_mark(unsigned int offset, int partial, int all)
{
  int dmapo = OFFSET_TO_DM(offset);
  int bdmap = OFFSET_TO_DMbit(offset);
  freemap4k *freemap = &diskmap[dmapo];
  unsigned int lmask = SET_BIT32(bdmap);
  unsigned int prev = freemap->partialmap;
  if (partial)
    freemap->partialmap |= lmask;
  if (all)
    freemap->bitmap |= lmask;
  if (fmaskdebug) {
    printf("dm: %d, bdmap: %d, mask=%08x (<16)\n", dmapo, bdmap, lmask);
  }
  if (!prev && freemap->partialmap) {
    update(dmapo, 1);           // set partially free.
  }
}

/**
 Mark len contiguous blocks starting at offset as free.

 @return none
 @param offset disk offset of start of region being freed.
 @param len length of region being freed in blocks.

 */
void
bfree2(unsigned int offset, unsigned int len)
{
  if (fmaskdebug)
    printf("bfree2(%d,%d)\n", offset, len);
  if (len < 16) {
    pmap16_markpartial(offset, len);
  } else if (len < 128) {       // 16 or more
    // clear 16 maps...
    unsigned int l = len;
    unsigned int start_offset = offset;
    if (start_offset & 0x0f) {  // not on boundary
      int p = 16 - (start_offset & 0x0f);
      bfree2(start_offset, p);
      l -= p;
      start_offset += p;
    }
    while (l >= 16) {
      pmap16_markall(start_offset);
      start_offset += 16;
      l -= 16;
    }
    if (l > 0) {                // free last partial
      bfree2(start_offset, l);
      len -= l;
    }
  } else {                      // length >= 128
    assert(len <= 128);
    // XXX: should do unaligned free check like above.
    pmap4k_mark(offset, 1, 0);
  }
}

extern void dumpAllocState();
/**
 Allocate contiguous blocks.  Returns offset and actual length, up to
 length blocks.
 
 @return none
 @param length requested length of contiguous region
 @param offset starting offset of region which has been allocated.
 @param actuallen actual length of region which has been allocated.
 actuallen will be <= length, but will be > 0.


 */
void
balloc(int length, unsigned int *offset, unsigned int *actuallen)
{
  freemap4k *dm = NULL;
  lt128freemap *l128 = NULL;
  lt16freemap *l16 = NULL;
  int runlen;
  int b128;
  int b16;
  int b;
  if (length < 16) {
    first_dm(&dm, &l128, &b128);
    if (!dm || !l128) {
      dumpAllocState();
    }
    assert(dm);
    assert(l128);
    first_partial16(l128, &l16, &b16, &b, &runlen);
    assert(l16);
    assert(runlen > 0);
    // adjust offset to match first block.
    *offset = TO_BLOCK(l16 - lt16, b);

    if (maskdebug)
      printf
        ("alloc of %d, diskmap: %d, b128map: %d, bit128: %d, b16map: %d, bit16: %d, bit: %d, len = %d, offset = %d\n",
         length, dm - diskmap, l128 - lt128, b128, l16 - lt16, b16, b, runlen, *offset);
    if (runlen > length) {
      // only allocate up to length
      pmap16_unmarkpartial(*offset, length);
      *actuallen = length;
    } else {
      // allocate whole thing
      assert(runlen <= (16 - ((*offset) & 0x0f)));
      pmap16_unmarkpartial(*offset, runlen);
      *actuallen = runlen;
    }
  } else if (length >= 16 && length < 128) {
    // find 128block region with space
    first_dm(&dm, &l128, &b128);
    first_full16(l128, &l16, &b16);
    if (maskdebug)
      printf("alloc of %d, diskmap: %d, b128map: %d, bit128: %d, b16map: %d, bit16: %d\n",
             length, dm - diskmap, l128 - lt128, b128, l16 - lt16, b16);
    if (!l16) {
      // XXX: need to fix to walk through 128 regions...
      b128++;
      l128++;
      while (!l16) {
        while (b128 < N_LT128_IN_DM) {
          first_full16(l128, &l16, &b16);
          if (!l16) {
            b128++;
            l128++;
          } else {
            break;
          }
        }
        if (l16)
          break;
        // next 128block region with space
        next_dm(&dm, &l128, &b128);
        if (!l128) {
          // can't find a full 128 region, then alloc only 16.
          // print out alloc map
          printf("no more space for contig. alloc of %d???\n", length);
          //here();
          //dumpAllocState();
          return balloc(15, offset, actuallen);
        }
      }
    }
    *offset = TO_BLOCK(l16 - lt16, 0);
    int allocated = 0;
    // allocate 16.
    int remainder = length;
    while (allocated<length && remainder>= 0) {
      if (remainder >= 16) {    // allocate full
        assert(l16->bitmap == 0xffff);
        pmap16_unmarkpartial((*offset) + allocated, 16);
        allocated += 16;
        remainder -= 16;
      } else {                  // allocate partial
        assert(((*offset) & 0x0f) == 0);
        pmap16_unmarkpartial((*offset) + allocated, remainder);
        allocated += remainder;
        remainder = 0;
      }
      if (maskdebug)
        printf("...diskmap: %d, b128map: %d, b16map: %d, offset = %d (remainder = %d)\n",
               dm - diskmap, l128 - lt128, l16 - lt16, *offset, remainder);
      l16++;
      b16++;
      // hopefully allocate in next block, or just give up
      if (l16->bitmap != 0xffff) {
        break;
      }
    }
    *actuallen = allocated;
  }
}

void
charToBitString(unsigned char c, char *bit)
{
  int n = 8;
  while (n > 0) {
    if (c & 0x80) {
      *bit = '1';
    } else {
      *bit = '0';
    }
    bit++;
    c = c << 1;
    n--;
  }
  *bit = '\0';
}

void
shortToBitString(unsigned short s, char *bit)
{
  int n = 16;
  while (n > 0) {
    if (s & 0x8000) {
      *bit = '1';
    } else {
      *bit = '0';
    }
    bit++;
    s = s << 1;
    n--;
  }
  *bit = '\0';
}

void
intToBitString(unsigned int s, char *bit)
{
  int n = 32;
  while (n > 0) {
    if (s & 0x80000000) {
      *bit = '1';
    } else {
      *bit = '0';
    }
    bit++;
    s = s << 1;
    n--;
  }
  *bit = '\0';
}

void
checkAllocState(int nalloc)
{
  int actual = 0;
  lt16freemap *fm = lt16;
  while (fm - lt16 < N_LT16_FREEMAP) {
    if (fm->bitmap != 0xffff) {
      actual += count16_zeros(fm->bitmap);
    }
    fm++;
  }
  assert(nalloc == actual);
}

void
dumpAllocState()
{
  lt16freemap *fm = lt16;
  printf("free lt16 map offset (128map offset):\n");
  while (fm - lt16 < N_LT16_FREEMAP) {
    if (fm->bitmap != 0xffff) {
      char bstr[17];
      shortToBitString(fm->bitmap, bstr);
      // report on allocation
      printf("%08d (%08d): %s\n", (fm - lt16) << 4, (fm - lt16) >> 3, bstr);
    }
    fm++;
  }
  printf("free lt128 map (disk map offset):\n");
  lt128freemap *fm128 = lt128;
  while (fm128 - lt128 < N_LT128_FREEMAP) {
    if (fm128->bitmap != fm128->partialmap || fm128->bitmap != 0xff) {
      char bstr[9];
      charToBitString(fm128->bitmap, bstr);
      char pbstr[9];
      charToBitString(fm128->partialmap, pbstr);
      // report on allocation
      printf("%08d (%08d): map=%s, partial=%s\n", (fm128 - lt128) << 7, (fm128 - lt128) >> 5, bstr, pbstr);
    }
    fm128++;
  }
  printf("free (disk map offset):\n");
  freemap4k *dm = diskmap;
  while (dm - diskmap < N_DISK_FREEMAP) {
    if (dm->bitmap != dm->partialmap || dm->bitmap != 0xffffffff) {
      char bstr[33];
      intToBitString(dm->bitmap, bstr);
      char pbstr[33];
      intToBitString(dm->partialmap, pbstr);
      // report on allocation
      printf("%08d (%d): map=%s,partial=%s\n", (dm - diskmap) << 12, dm - diskmap, bstr, pbstr);
    }
    dm++;
  }
}

void
test()
{
  dumpAllocState();
  //dumpFreeBuckets();
  unsigned int offset;
  unsigned int actual;
  balloc(5, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 0 && actual == 5);
  balloc(10, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 5 && actual == 10);
  balloc(2, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 15 && actual == 1);
  balloc(2, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 16 && actual == 2);
  dumpAllocState();
  bfree2(5, 10);
  dumpAllocState();
  balloc(5, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 5 && actual == 5);

  balloc(16, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 32 && actual == 16);
  dumpAllocState();
  balloc(64, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 48 && actual == 64);
  dumpAllocState();
}

void
test2()
{
  freeAll();
  unsigned int offset, actual;
  balloc(127, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 0 && actual == 127);
  balloc(2, &offset, &actual);
  printf("got <%d,%d>\n", offset, actual);
  assert(offset == 127 && actual == 1);
  balloc(2, &offset, &actual);
  assert(offset == 128 && actual == 2);
  printf("got <%d,%d>\n", offset, actual);
  dumpAllocState();
  freeAll();
  dumpAllocState();
}

/*
 * --------------------------------------------------------------------------
 * Simulation support
 * --------------------------------------------------------------------------
 */

// this is for trace generation, not part of block allocation algorithm
// when picking which "document" to free, we get it from here.
typedef struct AllocEntry_
{
  unsigned int offset:24;
  unsigned int length:8;
  struct AllocEntry_ *next;     // next in list
} AllocEntry;

typedef struct
{
  int pin_time;
  int alloced;
  AllocEntry *segments;
  AllocEntry *last_segment;
} Doc;

AllocEntry *freelist;

// number of distinct pin time groupings for purpose of GC
#define N_PIN_TIME 2

Doc *docs[N_PIN_TIME];

// ring buffer, empty if firstEntry == lastEntry.
// add to lastEntry, then advance. lastEntry points at empty entry.
int firstDoc[N_PIN_TIME];
int lastDoc[N_PIN_TIME];
int maxDocs = 24 * 1024 * 1024; // say, 24 million documents ok.

int
choose_pin_time()
{
  if ((lrand48() % 100) < pct_pinned) {
    return 1;
  } else {
    return 0;
  }
}

AllocEntry *
newAlloc()
{
  if (freelist) {
    AllocEntry *ret = freelist;
    freelist = ret->next;
    ret->next = NULL;
    return ret;
  } else {
    AllocEntry *ret = (AllocEntry *) calloc(sizeof(AllocEntry), 1);
    return ret;
  }
}

void
freeAlloc(AllocEntry * e)
{
  e->next = freelist;
  freelist = e;
}

void
initDoc()
{
  for (int i = 0; i < N_PIN_TIME; i++) {
    docs[i] = (Doc *) calloc(sizeof(Doc), maxDocs);
    firstDoc[i] = 0;
    lastDoc[i] = 0;
  }
}

int
docEmpty(int pin_time)
{
  return (firstDoc[pin_time] == lastDoc[pin_time]);
}

int
docFull(int pin_time)
{
  // no more entries for storing alloc info
  if (lastDoc[pin_time] <= firstDoc[pin_time]) {
    return (lastDoc[pin_time] + 1 == firstDoc[pin_time]);
  } else {
    return (firstDoc[pin_time] == 0 && lastDoc[pin_time] == maxDocs);
  }
}

Doc *
addDoc(int pin_time)
{
  assert(!docFull(pin_time));

  Doc *doc = &docs[pin_time][lastDoc[pin_time]];
  doc->pin_time = pin_time;
  doc->segments = NULL;
  doc->alloced = 0;

  lastDoc[pin_time]++;
  if (lastDoc[pin_time] == maxDocs) {
    lastDoc[pin_time] = 0;
  }
  assert(!docEmpty(pin_time));
  assert(!docFull(pin_time));
  return doc;
}

void
addSegment(Doc * d, unsigned int offset, unsigned int len)
{
  AllocEntry *e = newAlloc();
  e->offset = offset;
  e->length = len;
  d->alloced += len;
  assert(e->next == NULL);
  if (d->last_segment) {
    d->last_segment->next = e;
    d->last_segment = e;
  } else {
    d->segments = d->last_segment = e;
  }
}

Doc *
removeDoc(int pin_time)
{
  if (firstDoc[pin_time] == lastDoc[pin_time]) {
    // empty
    return NULL;
  } else {
    Doc *doc = &docs[pin_time][firstDoc[pin_time]];
    firstDoc[pin_time]++;
    if (firstDoc[pin_time] == maxDocs) {
      firstDoc[pin_time] = 0;
    }
    return doc;
  }
}

void
freeDoc(Doc * d)
{
  // just free the AllocEntry info
  AllocEntry *nexte;
  for (AllocEntry * e = d->segments; e; e = nexte) {
    nexte = e->next;
    e->next = NULL;
    freeAlloc(e);
  }
  d->segments = d->last_segment = NULL;
  d->alloced = 0;
}

void
printDoc(Doc * d)
{
  printf("pt=%d, ", d->pin_time);
  for (AllocEntry * e = d->segments; e; e = e->next) {
    printf("<%d,%d> ", e->offset, e->length);
  }
  printf("\n");
}

// # of allocated regions for doc -- measure of fragmentation
int
docSegs(Doc * d)
{
  AllocEntry *e = d->segments;
  int i = 0;
  while (e) {
    i++;
    e = e->next;
  }
  return i;
}

void
docInfo(int pin_time)
{
  int i;
  printf("Documents (pin time = %d):--------\n", pin_time);
  if (firstDoc[pin_time] < lastDoc[pin_time]) {
    for (i = firstDoc[pin_time]; i < lastDoc[pin_time]; i++) {
      printf("%d: ", i);
      printDoc(&docs[pin_time][i]);
    }
  } else {
    for (i = firstDoc[pin_time]; i < maxDocs; i++) {
      printf("%d: ", i);
      printDoc(&docs[pin_time][i]);
    }
    for (i = 0; i < lastDoc[pin_time]; i++) {
      printf("%d: ", i);
      printDoc(&docs[pin_time][i]);
    }
  }
}

void
docStats(int pin_time)
{
  int i;
#define N_SEGS 20
  int segcount[N_SEGS];
#define N_SIZES 128
  // # of docs of particular intended allocation resulting in particular
  // # of segments -- for computing median, etc.
  int segallocsize[N_SEGS][N_SIZES];
  int largest = 0;
  int total = 0;
  for (i = 0; i < N_SEGS; i++) {
    segcount[i] = 0;
    for (int k = 0; k < N_SIZES; k++) {
      segallocsize[i][k] = 0;
    }
  }
  printf("Documents (pin time = %d) fragmentation:--------\n", pin_time);
  if (firstDoc[pin_time] < lastDoc[pin_time]) {
    for (i = firstDoc[pin_time]; i < lastDoc[pin_time]; i++) {
      Doc *d = &docs[pin_time][i];
      int s = docSegs(d);
      total++;
      assert(s > 0);
      assert((s - 1) < N_SEGS);
      segcount[s - 1]++;
      segallocsize[s - 1][d->alloced - 1]++;
      if (s - 1 > largest)
        largest = s - 1;
    }
  } else {
    for (i = firstDoc[pin_time]; i < maxDocs; i++) {
      Doc *d = &docs[pin_time][i];
      int s = docSegs(d);
      total++;
      assert(s > 0);
      assert((s - 1) < N_SEGS);
      segcount[s - 1]++;
      segallocsize[s - 1][d->alloced - 1]++;
      if (s - 1 > largest)
        largest = s - 1;
    }
    for (i = 0; i < lastDoc[pin_time]; i++) {
      Doc *d = &docs[pin_time][i];
      int s = docSegs(d);
      total++;
      assert(s > 0);
      assert((s - 1) < N_SEGS);
      segcount[s - 1]++;
      segallocsize[s - 1][d->alloced - 1]++;
      if (s - 1 > largest)
        largest = s - 1;
    }
  }

  printf("#segs  #docs\n");
  printf("-----  -----\n");
  int cumcount = 0;
  for (i = 0; i <= largest; i++) {
    cumcount += segcount[i];
    // compute median;
    int sofar = 0;
    int allocsize = 1;
    while (sofar <= segcount[i] / 2) {
      sofar += segallocsize[i][allocsize - 1];
      allocsize++;
    }
    printf("%5d %6d (%3.2f%% (%3.2f%% below)) median alloc size was %d\n", i + 1, segcount[i],
           (100.0 * segcount[i]) / total, (100.0 * (total - cumcount)) / total, allocsize);
  }
}


void
test0()
{
  unsigned char mask;
  mask = set_range8(3, 3 + (16 >> 4) - 1);
  assert(mask == 0x10);
  unsigned short smask = clear_range16(0, 16);
  assert(smask == 0xffff);
}

#define N_TEST3 3000
void
test3a()
{
  int i, j, k, size;
  int cumulative = 0;
  unsigned int offset[N_TEST3];
  unsigned int actual[N_TEST3];
  for (size = 1; size <= 16; size++) {
    for (j = 0; j < 3; j++) {
      for (i = 0; i < N_TEST3; i++) {
        balloc(size, &offset[i], &actual[i]);
        cumulative += actual[i];
        checkAllocState(cumulative);
        check_rlestate();
        if (cumulative + size > MAX_BLOCKS) {
          break;
        }
      }
      for (k = 0; k <= i && k < N_TEST3; k++) {
        bfree2(offset[k], actual[k]);
        cumulative -= actual[k];
        checkAllocState(cumulative);
        check_rlestate();
      }
      assert(cumulative == 0);
    }
  }
  checkAllocState(0);
  dumpAllocState();
}

void
test3b()
{
  int i, j, k, size;
  int cumulative = 0;
  unsigned int offset[N_TEST3];
  unsigned int actual[N_TEST3];
  for (j = 0; j < 3; j++) {
    for (i = 0; i < N_TEST3; i++) {
      size = 1 + lrand48() % 32;
      balloc(size, &offset[i], &actual[i]);
      cumulative += actual[i];
      checkAllocState(cumulative);
      check_rlestate();
      if (cumulative + 32 > MAX_BLOCKS) {
        break;
      }
    }

    for (k = 0; k <= i && k < N_TEST3; k++) {
      bfree2(offset[k], actual[k]);
      cumulative -= actual[k];
      checkAllocState(cumulative);
      check_rlestate();
    }
    assert(cumulative == 0);
  }
  dumpAllocState();
  print_rlestate();
}

int
main(int argc, char *argv[])
{
  printf("disksize = %lld MB, %lld blocks\n", DISKSIZE / (1 << 20), MAX_BLOCKS);
  allocInit();
  check_rlestate();
  //test_rle0();
  //exit(0);

  if (regression) {
    test0();
    check_rlestate();
    test2();
    check_rlestate();
    test3a();
    check_rlestate();
    test3b();
    check_rlestate();

    if (0) {
      test();
      exit(0);

      freeAll();
    }
  }
  openSizes();
  initDoc();

  int n = 0;
  // allocate until full
  int alloced = 0;
  int last_alloced = 0;
  while (1) {
    int s = nextSize();
    if (s <= 0)                 // actually present
      continue;
    int blocks = ((s + (BLOCK_SIZE - 1)) >> BLOCK_SHIFT);
    if (blocks >= 128)          // XXX: debugging
      continue;
    unsigned int offset, actual;
    int pt = choose_pin_time();
    Doc *d = addDoc(pt);
    int needed = blocks;
    while (needed > 0) {
      balloc(needed, &offset, &actual);
      assert(actual > 0);
      alloced += actual;
      needed -= actual;
      addSegment(d, offset, actual);
      //printf("got <%d,%d> for desired %d (%d bytes) (%d so far)\n",offset,actual,blocks,s,cumulative);
      // allocate repeatedly if we didn't get enough
    }
    n++;
    if (alloced > 0.96 * MAX_BLOCKS) {
      break;
    }
    if (alloced - last_alloced > MAX_BLOCKS * 0.1) {
      printf("%d alloced, %d docs\n", alloced, n);
      last_alloced = alloced;
    }
  }

  printf("%d docs added\n", n);
  print_rlestate();
  check_rlestate();
//    for (int pt = 0; pt < N_PIN_TIME; pt++) {
//      docInfo(pt);
//    }
//    dumpAllocState();
//    print_rlestate();
  struct timeval start, end;
  int calls = 0;
  int ops = 0;
  gettimeofday(&start, NULL);
  n = 0;
  while (n < TEST_COUNT) {
    // free as needed
    while (alloced > 0.96 * MAX_BLOCKS) {
      // free a document
      // first try non-pinned doc
      Doc *d = removeDoc(0);
      if (!d) {
        printf("removing pinned doc!\n");
        d = removeDoc(1);
      }
      assert(d);
      for (AllocEntry * e = d->segments; e; e = e->next) {
        bfree2(e->offset, e->length);
        calls++;
        if (rlecheck)
          check_rlestate();
        alloced -= e->length;
        //printf("free %d\n",e->length);
      }
      ops++;
      freeDoc(d);
    }
    unsigned int offset, actual;

    // allocate another
    int s = nextSize();
    if (s <= 0)                 // actually present
      continue;
    int blocks = ((s + (BLOCK_SIZE - 1)) >> BLOCK_SHIFT);
    if (blocks >= 128)          // debug only
      continue;
    int pt;
    if (steady_state_is_unpinned_incoming) {
      pt = 0;
    } else {
      pt = choose_pin_time();
    }
    Doc *d = addDoc(pt);

    int needed = blocks;
    while (needed > 0) {
      balloc(needed, &offset, &actual);
      calls++;
      assert(actual > 0);
      if (rlecheck)
        check_rlestate();
      alloced += actual;
      needed -= actual;
      addSegment(d, offset, actual);
      //printf("got <%d,%d> for desired %d (%d bytes) (%d so far)\n",offset,actual,blocks,s,alloced);
      // allocate repeatedly if we didn't get enough
    }
    ops++;
    n++;
    if (n % 500000 == 0) {
      printf("%d\n", n);
      if (print_fraginfo)
        docStats(0);
      //docStats(1);
    }
  }
  gettimeofday(&end, NULL);
  if (!print_fraginfo) {
    int usecs = (end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec;
    printf("%d us\n", usecs);
    printf("%lld ns (elapsed) per call (%d calls)\n", usecs * 1000LL / calls, calls);
    printf("%lld ns (elapsed) per op (consisting of multiple free & alloc) (%d ops)\n", usecs * 1000LL / ops, ops);
    //dumpAllocState();
  }
  print_rlestate();
  check_rlestate();
}


//@}
