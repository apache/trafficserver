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

#include "P_Cache.h"


int
CacheDisk::open(char *s, ink_off_t blocks, ink_off_t dir_skip, int fildes, bool clear)
{
  path = xstrdup(s);
  fd = fildes;
  skip = ROUND_TO_BLOCK((dir_skip < START_POS ? START_POS : dir_skip));
  start_offset = dir_skip;
  start = skip;
  /* we can't use fractions of store blocks. */
  len = blocks;
  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_reqprio = 0;
  io.action = this;
  inku64 l;
  for (int i = 0; i < 3; i++) {
    l = (len * STORE_BLOCK_SIZE) - (start - skip);
    if (l >= MIN_PART_SIZE) {
      header_len = sizeof(DiskHeader) + (l / MIN_PART_SIZE - 1) * sizeof(DiskPartBlock);
    } else {
      header_len = sizeof(DiskHeader);
    }
    start = skip + header_len;
  }

  disk_parts = (DiskPart **) xmalloc((l / MIN_PART_SIZE + 1) * sizeof(DiskPart **));

  memset(disk_parts, 0, (l / MIN_PART_SIZE + 1) * sizeof(DiskPart **));
  header_len = ROUND_TO_BLOCK(header_len);
  start = skip + header_len;
  num_usable_blocks = (ink_off_t(len * STORE_BLOCK_SIZE) - (start - start_offset)) >> STORE_BLOCK_SHIFT;

#if defined(_WIN32)
  header = (DiskHeader *) malloc(header_len);
#else
  header = (DiskHeader *) valloc(header_len);
#endif

  memset(header, 0, header_len);
  if (clear) {
    SET_HANDLER(&CacheDisk::clearDone);
    return clearDisk();
  }

  SET_HANDLER(&CacheDisk::openStart);
  io.aiocb.aio_offset = skip;
  io.aiocb.aio_buf = (char *) header;
  io.aiocb.aio_nbytes = header_len;
  io.thread = AIO_CALLBACK_THREAD_ANY;
  ink_aio_read(&io);
  return 0;
}

CacheDisk::~CacheDisk()
{
  if (path) {
    xfree(path);
    for (int i = 0; i < (int) header->num_partitions; i++) {
      DiskPartBlockQueue *q = NULL;
      while (disk_parts[i] && (q = (disk_parts[i]->dpb_queue.pop()))) {
        delete q;
      }
    }
    xfree(disk_parts);
    free(header);
  }
  if (free_blocks) {
    DiskPartBlockQueue *q = NULL;
    while ((q = (free_blocks->dpb_queue.pop()))) {
      delete q;
    }
    delete free_blocks;
  }
}

int
CacheDisk::clearDisk()
{
  delete_all_partitions();

  io.aiocb.aio_offset = skip;
  io.aiocb.aio_buf = header;
  io.aiocb.aio_nbytes = header_len;
  io.thread = AIO_CALLBACK_THREAD_ANY;
  ink_aio_write(&io);
  return 0;
}

int
CacheDisk::clearDone(int event, void *data)
{
  NOWARN_UNUSED(data);
  ink_assert(event == AIO_EVENT_DONE);

  if ((int) io.aiocb.aio_nbytes != (int) io.aio_result) {
    Warning("Could not clear disk header for disk %s: declaring disk bad", path);
    SET_DISK_BAD(this);
  }
//  update_header();

  SET_HANDLER(&CacheDisk::openDone);
  return openDone(EVENT_IMMEDIATE, 0);
}

int
CacheDisk::openStart(int event, void *data)
{
  NOWARN_UNUSED(data);
  ink_assert(event == AIO_EVENT_DONE);

  if ((int) io.aiocb.aio_nbytes != (int) io.aio_result) {
    Warning("could not read disk header for disk %s: declaring disk bad", path);
    SET_DISK_BAD(this);
    SET_HANDLER(&CacheDisk::openDone);
    return openDone(EVENT_IMMEDIATE, 0);
  }

  if (header->magic != DISK_HEADER_MAGIC || header->num_blocks != len) {
    Warning("disk header different for disk %s: clearing the disk", path);
    SET_HANDLER(&CacheDisk::clearDone);
    clearDisk();
    return EVENT_DONE;
  }

  cleared = 0;
  /* populate disk_parts */
  update_header();

  SET_HANDLER(&CacheDisk::openDone);
  return openDone(EVENT_IMMEDIATE, 0);
}

int
CacheDisk::openDone(int event, void *data)
{
  NOWARN_UNUSED(data);
  NOWARN_UNUSED(event);
  if (cacheProcessor.start_done) {
    SET_HANDLER(&CacheDisk::syncDone);
    cacheProcessor.diskInitialized();
    return EVENT_DONE;
  } else {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(5), ET_CALL);
    return EVENT_CONT;
  }
}

int
CacheDisk::sync()
{
  io.aiocb.aio_offset = skip;
  io.aiocb.aio_buf = header;
  io.aiocb.aio_nbytes = header_len;
  io.thread = AIO_CALLBACK_THREAD_ANY;
  ink_aio_write(&io);
  return 0;
}

int
CacheDisk::syncDone(int event, void *data)
{
  NOWARN_UNUSED(data);

  ink_assert(event == AIO_EVENT_DONE);

  if ((int) io.aiocb.aio_nbytes != (int) io.aio_result) {
    Warning("Error writing disk header for disk %s:disk bad", path);
    SET_DISK_BAD(this);
    return EVENT_DONE;
  }

  return EVENT_DONE;
}

/* size is in store blocks */
DiskPartBlock *
CacheDisk::create_partition(int number, ink_off_t size_in_blocks, int scheme)
{
  if (size_in_blocks == 0)
    return NULL;

  DiskPartBlockQueue *q = free_blocks->dpb_queue.head;
  DiskPartBlockQueue *closest_match = q;

  if (!q)
    return NULL;
  ink_off_t max_blocks = MAX_PART_SIZE >> STORE_BLOCK_SHIFT;
  size_in_blocks = (size_in_blocks <= max_blocks) ? size_in_blocks : max_blocks;

  int blocks_per_part = PART_BLOCK_SIZE / STORE_BLOCK_SIZE;
//  ink_assert(!(size_in_blocks % blocks_per_part));
  DiskPartBlock *p = 0;
  for (; q; q = q->link.next) {
    if (q->b->len >= (unsigned int) size_in_blocks) {
      p = q->b;
      q->new_block = 1;
      break;
    } else {
      if (closest_match->b->len < q->b->len)
        closest_match = q;
    }
  }

  if (!p && !closest_match)
    return NULL;

  if (!p && closest_match) {
    /* allocate from the closest match */
    q = closest_match;
    p = q->b;
    q->new_block = 1;
    ink_assert(size_in_blocks > (int) p->len);
    /* allocate in 128 megabyte chunks. The Remaining space should
       be thrown away */
    size_in_blocks = (p->len - (p->len % blocks_per_part));
    wasted_space += p->len % blocks_per_part;
  }

  free_blocks->dpb_queue.remove(q);
  free_space -= p->len;
  free_blocks->size -= p->len;

  int new_size = p->len - size_in_blocks;
  if (new_size >= blocks_per_part) {
    /* create a new partition */
    DiskPartBlock *dpb = &header->part_info[header->num_diskpart_blks];
    *dpb = *p;
    dpb->len -= size_in_blocks;
    dpb->offset += ((ink_off_t) size_in_blocks * STORE_BLOCK_SIZE);

    DiskPartBlockQueue *new_q = NEW(new DiskPartBlockQueue());
    new_q->b = dpb;
    free_blocks->dpb_queue.enqueue(new_q);
    free_blocks->size += dpb->len;
    free_space += dpb->len;
    header->num_diskpart_blks++;
  } else
    header->num_free--;

  p->len = size_in_blocks;
  p->free = 0;
  p->number = number;
  p->type = scheme;
  header->num_used++;

  unsigned int i;
  /* add it to its disk_part */
  for (i = 0; i < header->num_partitions; i++) {
    if (disk_parts[i]->part_number == number) {
      disk_parts[i]->dpb_queue.enqueue(q);
      disk_parts[i]->num_partblocks++;
      disk_parts[i]->size += q->b->len;
      break;
    }
  }
  if (i == header->num_partitions) {
    disk_parts[i] = NEW(new DiskPart());
    disk_parts[i]->num_partblocks = 1;
    disk_parts[i]->part_number = number;
    disk_parts[i]->disk = this;
    disk_parts[i]->dpb_queue.enqueue(q);
    disk_parts[i]->size = q->b->len;
    header->num_partitions++;
  }
  return p;
}


int
CacheDisk::delete_partition(int number)
{
  unsigned int i;
  for (i = 0; i < header->num_partitions; i++) {
    if (disk_parts[i]->part_number == number) {

      DiskPartBlockQueue *q;
      for (q = disk_parts[i]->dpb_queue.head; q;) {
        DiskPartBlock *p = q->b;
        p->type = CACHE_NONE_TYPE;
        p->free = 1;
        free_space += p->len;
        header->num_free++;
        header->num_used--;
        DiskPartBlockQueue *temp_q = q->link.next;
        disk_parts[i]->dpb_queue.remove(q);
        free_blocks->dpb_queue.enqueue(q);
        q = temp_q;
      }
      free_blocks->num_partblocks += disk_parts[i]->num_partblocks;
      free_blocks->size += disk_parts[i]->size;

      delete disk_parts[i];
      /* move all the other disk parts */
      for (unsigned int j = i; j < (header->num_partitions - 1); j++) {
        disk_parts[j] = disk_parts[j + 1];
      }
      header->num_partitions--;
      return 0;
    }
  }
  return -1;
}

void
CacheDisk::update_header()
{
  unsigned int n = 0;
  unsigned int i, j;
  if (free_blocks) {
    DiskPartBlockQueue *q = NULL;
    while ((q = (free_blocks->dpb_queue.pop()))) {
      delete q;
    }
    delete free_blocks;
  }
  free_blocks = NEW(new DiskPart());
  free_blocks->part_number = -1;
  free_blocks->disk = this;
  free_blocks->num_partblocks = 0;
  free_blocks->size = 0;
  free_space = 0;

  for (i = 0; i < header->num_diskpart_blks; i++) {
    DiskPartBlockQueue *dpbq = NEW(new DiskPartBlockQueue());
    bool dpbq_referenced = false;
    dpbq->b = &header->part_info[i];
    if (header->part_info[i].free) {
      free_blocks->num_partblocks++;
      free_blocks->size += dpbq->b->len;
      free_blocks->dpb_queue.enqueue(dpbq);
      dpbq_referenced = true;
      free_space += dpbq->b->len;
      continue;
    }
    int part_number = header->part_info[i].number;
    for (j = 0; j < n; j++) {
      if (disk_parts[j]->part_number == part_number) {
        disk_parts[j]->dpb_queue.enqueue(dpbq);
        dpbq_referenced = true;
        disk_parts[j]->num_partblocks++;
        disk_parts[j]->size += dpbq->b->len;
        break;
      }
    }
    if (j == n) {
      // did not find a matching partition number. create a new 
      // one
      disk_parts[j] = NEW(new DiskPart());
      disk_parts[j]->part_number = part_number;
      disk_parts[j]->disk = this;
      disk_parts[j]->num_partblocks = 1;
      disk_parts[j]->size = dpbq->b->len;
      disk_parts[j]->dpb_queue.enqueue(dpbq);
      dpbq_referenced = true;
      n++;
    }
    // check to see if we even used the dpbq allocated
    if (dpbq_referenced == false) {
      delete dpbq;
    }
  }

  ink_assert(n == header->num_partitions);
}

DiskPart *
CacheDisk::get_diskpart(int part_number)
{
  unsigned int i;
  for (i = 0; i < header->num_partitions; i++) {
    if (disk_parts[i]->part_number == part_number) {
      return disk_parts[i];
    }
  }
  return NULL;
}

int
CacheDisk::delete_all_partitions()
{
  header->part_info[0].offset = start;
  header->part_info[0].len = num_usable_blocks;
  header->part_info[0].type = CACHE_NONE_TYPE;
  header->part_info[0].free = 1;

  header->magic = DISK_HEADER_MAGIC;
  header->num_used = 0;
  header->num_partitions = 0;
  header->num_free = 1;
  header->num_diskpart_blks = 1;
  header->num_blocks = len;
  cleared = 1;
  update_header();

  return 0;
}
