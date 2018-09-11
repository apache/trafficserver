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

/****************************************************************************

   CoreUtils.cc

   Description:  Automated processing of core files on Linux
 ****************************************************************************/

/*
   Stack Unwinding procedure on ix86 architecture on Linux :
   Get the first frame pointer in $ebp.
   The value stored in $ebp is the address of prev frame pointer.
   Keep on unwinding till it is Ox0.
   $ebp+4 in each frame represents $eip.(PC)
*/

/*
 *  Accessing arguments on 386 :
 *  ----------------------------
 *  We need to start from $ebp+4 and then keep on reading args
 *  till we reach the base pointer for prev. frame
 *
 *
 *        (high memory)
 *    +                     +
 *    | Callers Stack Frame |
 *    +---------------------+
 *    |   function call     |
 *    |     arguments       |
 *    +---------------------+
 *    |   Return Address    +
 *    +-------------------- +
 *    |    Old base pointer + Base pointer BP
 *    +-------------------- +
 *    |                     |
 *    |                     |
 *    |                     | Local (automatic) variables
 *    |                     |
 *    |                     |
 *    |                     |
 *    |                     |
 *    |                     |
 *    |                     |
 *    +---------------------+ Stack pointer SP
 *    |     free stack      | (low memory, top of the stack)
 *    |    begins here      |
 *    +                     +
 *
 *
 *	  +-----------------+     +-----------------+
 *  FP -> | previous FP --------> | previous FP ------>...
 *	  |                 |     |                 |
 *        | return address  |     | return address  |
 *        +-----------------+     +-----------------+
 */

/* 32-bit arguments are pushed down stack in reverse syntactic order (hence accessed/popped in the right order), above the 32-bit
 * near return address. %ebp, %esi, %edi, %ebx are callee-saved, other registers are caller-saved; %eax is to hold the result, or
 * %edx:%eax for 64-bit results */

/*    has -fomit-frame-pointer has any repercussions??
      We assume that all the code is generated with frame pointers set.  */

/* modify the "note" in process_core */
/* Document properly */

#include "tscore/ink_config.h"

#if defined(linux)
#include "CoreUtils.h"

#define __p_type p_type // ugly hack? - see resolv.h
#define D(x) x          /* for debugging */
intptr_t f1, f2;
int framepointer    = 0;
int program_counter = 0;
#endif // linux check

#if defined(darwin) || defined(freebsd) || defined(solaris) || defined(openbsd) // FIXME: solaris x86
// TODO: Cleanup multiple includes
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "tscore/ink_platform.h"
#include "CoreUtils.h"
#endif /* darwin || freebsd || solaris */

#include "EventName.h"
#include "http/HttpSM.h"

#include <cstdlib>
#include <cmath>

bool inTable;
FILE *fp;
memTable default_memTable = {0, 0, 0};
DynArray<struct memTable> arrayMem(&default_memTable, 0);

HTTPHdrImpl *global_http;
HttpSM *last_seen_http_sm = nullptr;

char ethread_ptr_str[256] = "";
char netvc_ptr_str[256]   = "";

HdrHeap *swizzle_heap;
char *ptr_data;

// returns the index of the vaddr or the index after where it should be
intptr_t
CoreUtils::find_vaddr(intptr_t vaddr, intptr_t upper, intptr_t lower)
{
  intptr_t index = (intptr_t)floor((double)((upper + lower) / 2));

  // match in table, returns index to be inserted into
  if (arrayMem[index].vaddr == vaddr) {
    inTable = true;
    return index + 1;
    // no match
  } else if (upper == lower) {
    inTable = false;
    return upper;
    // no match
  } else if (index == lower) {
    inTable = false;
    if ((index == 0) && (arrayMem[index].vaddr > vaddr)) {
      return 0;
    } else {
      return index + 1;
    }
  } else {
    if (arrayMem[index].vaddr > vaddr) {
      return find_vaddr(vaddr, index, lower);
    } else {
      return find_vaddr(vaddr, upper, index);
    }
  }
  assert(0);
  return -1;
}

// inserts virtual address struct into the list
void
CoreUtils::insert_table(intptr_t vaddr1, intptr_t offset1, intptr_t fsize1)
{
// TODO: What was this intended for??
#if 0
  memTable m;
  m.vaddr = vaddr1;
  m.offset = offset1;
  m.fsize = fsize1;
#endif

  if (arrayMem.length() == 0) {
    arrayMem(0);
    arrayMem[(intptr_t)0].vaddr  = vaddr1;
    arrayMem[(intptr_t)0].offset = offset1;
    arrayMem[(intptr_t)0].fsize  = fsize1;
  } else {
    intptr_t index = find_vaddr(vaddr1, arrayMem.length(), 0);
    if (index == arrayMem.length()) {
      arrayMem(index);
      arrayMem[index].vaddr  = vaddr1;
      arrayMem[index].offset = offset1;
      arrayMem[index].fsize  = fsize1;
    } else if (index == 0) {
      arrayMem(arrayMem.length());
      for (intptr_t i = 0; i < arrayMem.length(); i++) {
        arrayMem[arrayMem.length() - i - 1].vaddr  = arrayMem[arrayMem.length() - i - 2].vaddr;
        arrayMem[arrayMem.length() - i - 1].offset = arrayMem[arrayMem.length() - i - 2].offset;
        arrayMem[arrayMem.length() - i - 1].fsize  = arrayMem[arrayMem.length() - i - 2].fsize;
      }
      arrayMem[(intptr_t)0].vaddr  = vaddr1;
      arrayMem[(intptr_t)0].offset = offset1;
      arrayMem[(intptr_t)0].fsize  = fsize1;
    } else {
      arrayMem(arrayMem.length());
      for (intptr_t i = 1; i < arrayMem.length() - index; i++) {
        arrayMem[arrayMem.length() - i].vaddr  = arrayMem[arrayMem.length() - i - 1].vaddr;
        arrayMem[arrayMem.length() - i].offset = arrayMem[arrayMem.length() - i - 1].offset;
        arrayMem[arrayMem.length() - i].fsize  = arrayMem[arrayMem.length() - i - 1].fsize;
      }
      arrayMem[index].vaddr  = vaddr1;
      arrayMem[index].offset = offset1;
      arrayMem[index].fsize  = fsize1;
    }
  }
}

// returns -1 on failure otherwise fills the buffer and
// returns the number of bytes read
intptr_t
CoreUtils::read_from_core(intptr_t vaddr, intptr_t bytes, char *buf)
{
  intptr_t index   = find_vaddr(vaddr, arrayMem.length(), 0);
  intptr_t vadd    = arrayMem[index - 1].vaddr;
  intptr_t offset  = arrayMem[index - 1].offset;
  intptr_t size    = arrayMem[index - 1].fsize;
  intptr_t offset2 = std::abs(vaddr - vadd);

  if (bytes > (size - offset2)) {
    return -1;
  } else {
    if (fseek(fp, offset2 + offset, SEEK_SET) != -1) {
      char *frameoff;
      if ((frameoff = (char *)ats_malloc(sizeof(char) * bytes))) {
        if (fread(frameoff, bytes, 1, fp) == 1) {
          memcpy(buf, frameoff, bytes);
          /*for(int j =0; j < bytes; j++) {
           *buf++ = getc(fp);
           }
           buf -= bytes;*/
          ats_free(frameoff);
          return bytes;
        }
        ats_free(frameoff);
      }
    } else {
      return -1;
    }
  }

  return -1;
}

/* Linux Specific functions */

#if defined(linux)
// copies stack info for the thread's base frame to the given
// core_stack_state pointer
void
CoreUtils::get_base_frame(intptr_t framep, core_stack_state *coress)
{
  // finds vaddress less than framep
  intptr_t index = find_vaddr(framep, arrayMem.length(), 0);
  intptr_t vadd  = arrayMem[index - 1].vaddr;
  intptr_t off   = arrayMem[index - 1].offset;
  intptr_t off2  = std::abs(vadd - framep);
  intptr_t size  = arrayMem[index - 1].fsize;
  intptr_t i     = 0;

  memset(coress, 0, sizeof(*coress));
  D(printf("stkbase=%p\n", (void *)(vadd + size)));
  // seek to the framep offset
  if (fseek(fp, off + off2, SEEK_SET) != -1) {
    void **frameoff;
    if ((frameoff = (void **)ats_malloc(sizeof(long)))) {
      if (fread(frameoff, 4, 1, fp) == 1) {
        coress->framep = (intptr_t)*frameoff;
        if (fread(frameoff, 4, 1, fp) == 1) {
          coress->pc = (intptr_t)*frameoff;
        }
        // read register arguments
        for (i = 0; i < NO_OF_ARGS; i++) {
          if (fread(frameoff, 4, 1, fp) == 1) {
            coress->arg[i] = (intptr_t)*frameoff;
          }
        }
      }
      ats_free(frameoff);
    }
  } else {
    printf("Failed to seek to top of the stack\n");
  }
  // coress->stkbase = vadd+size;
}

// returns 0 if current frame is already at the top of the stack
// or returns 1 and moves up the stack once
int
CoreUtils::get_next_frame(core_stack_state *coress)
{
  intptr_t i      = 0;
  intptr_t framep = coress->framep;

  intptr_t index = find_vaddr(framep, arrayMem.length(), 0);

  // finds vaddress less than framep
  intptr_t vadd = arrayMem[index - 1].vaddr;
  intptr_t off  = arrayMem[index - 1].offset;
  intptr_t off2 = std::abs(vadd - framep);

  // seek to the framep offset
  if (fseek(fp, off + off2, SEEK_SET) != -1) {
    void **frameoff;
    if ((frameoff = (void **)ats_malloc(sizeof(long)))) {
      if (fread(frameoff, 4, 1, fp) == 1) {
        coress->framep = (intptr_t)*frameoff;
        if (*frameoff == nullptr) {
          ats_free(frameoff);
          return 0;
        }
        if (fread(frameoff, 4, 1, fp) == 1) {
          coress->pc = (intptr_t)*frameoff;
        }
        for (i = 0; i < NO_OF_ARGS; i++) {
          if (fread(frameoff, 4, 1, fp) == 1) {
            coress->arg[i] = (intptr_t)*frameoff;
          }
        }
      }
      ats_free(frameoff);
    }
    return 1;
  }

  return 0;
}

// prints the http header
void
CoreUtils::find_stuff(StuffTest_f f)
{
  intptr_t framep = framepointer;
  intptr_t pc     = program_counter;
  core_stack_state coress;
  intptr_t i;
  void *test_val;
  int framecount = 0;

  // Unwinding the stack
  D(printf("\nStack Trace:\n"));
  D(printf("stack frame#%d framep=%p pc=%p\n", framecount, (void *)framep, (void *)pc));
  framecount++;
  get_base_frame(framep, &coress);
  f2 = framep;
  do {
    f1 = f2;
    f2 = coress.framep;
    D(printf("stack frame#%d framep=%p pc=%p f1-f2=%p coress=%p %p %p %p %p\n", framecount, (void *)coress.framep,
             (void *)coress.pc, (void *)(f2 - f1), (void *)coress.arg[0], (void *)coress.arg[1], (void *)coress.arg[2],
             (void *)coress.arg[3], (void *)coress.arg[4]));

    for (i = 0; i < NO_OF_ARGS; i++) {
      test_val = (void *)coress.arg[i];
      f(test_val);
    }
    framecount++;
  } while (get_next_frame(&coress) != 0);
}
#endif // linux check

// test whether a given register is an HttpSM
//   if it is, call process_HttpSM on it
void
CoreUtils::test_HdrHeap(void *arg)
{
  HdrHeap *hheap_test = (HdrHeap *)arg;
  uint32_t *magic_ptr = &(hheap_test->m_magic);
  uint32_t magic      = 0;

  if (read_from_core((intptr_t)magic_ptr, sizeof(uint32_t), (char *)&magic) != 0) {
    if (magic == HDR_BUF_MAGIC_ALIVE || magic == HDR_BUF_MAGIC_DEAD || magic == HDR_BUF_MAGIC_CORRUPT ||
        magic == HDR_BUF_MAGIC_MARSHALED) {
      printf("Found Hdr Heap @ 0x%p\n", arg);
    }
  }
}

// test whether a given register is an HttpSM
//   if it is, call process_HttpSM on it
//
// This code generates errors from Clang, on hsm_test not being initialized
// properly. Currently this is not used, so ifdef'ing out to suppress.
#ifndef __clang_analyzer__
void
CoreUtils::test_HttpSM_from_tunnel(void *arg)
{
  char *tmp        = (char *)arg;
  intptr_t offset  = (intptr_t) & (((HttpTunnel *)nullptr)->sm);
  HttpSM **hsm_ptr = (HttpSM **)(tmp + offset);
  HttpSM *hsm_test = nullptr;

  if (read_from_core((intptr_t)hsm_ptr, sizeof(HttpSM *), (char *)&hsm_test) == 0) {
    return;
  }

  unsigned int *magic_ptr = &(hsm_test->magic);
  unsigned int magic      = 0;

  if (read_from_core((intptr_t)magic_ptr, sizeof(int), (char *)&magic) != 0) {
    if (magic == HTTP_SM_MAGIC_ALIVE || magic == HTTP_SM_MAGIC_DEAD) {
      process_HttpSM(hsm_test);
    }
  }
}
#endif

// test whether a given register is an HttpSM
//   if it is, call process_HttpSM on it
void
CoreUtils::test_HttpSM(void *arg)
{
  HttpSM *hsm_test        = (HttpSM *)arg;
  unsigned int *magic_ptr = &(hsm_test->magic);
  unsigned int magic      = 0;

  if (read_from_core((intptr_t)magic_ptr, sizeof(int), (char *)&magic) != 0) {
    if (magic == HTTP_SM_MAGIC_ALIVE || magic == HTTP_SM_MAGIC_DEAD) {
      printf("test_HttpSM:******MATCH*****\n");
      process_HttpSM(hsm_test);
    }
  }
}

void
CoreUtils::process_HttpSM(HttpSM *core_ptr)
{
  // extracting the HttpSM from the core file
  if (last_seen_http_sm != core_ptr) {
    HttpSM *http_sm = (HttpSM *)ats_malloc(sizeof(HttpSM));

    if (read_from_core((intptr_t)core_ptr, sizeof(HttpSM), (char *)http_sm) < 0) {
      printf("ERROR: Failed to read httpSM @ 0x%p from core\n", core_ptr);
      ats_free(http_sm);
      return;
    }

    if (http_sm->magic == HTTP_SM_MAGIC_ALIVE) {
      last_seen_http_sm = core_ptr;

      if (is_debug_tag_set("magic")) {
#if defined(linux)
        printf("\n*****match-ALIVE*****\n");
#endif
      }
      printf("---- Found HttpSM --- id %" PRId64 "  ------ @ 0x%p -----\n\n", http_sm->sm_id, http_sm);

      print_http_hdr(&http_sm->t_state.hdr_info.client_request, "Client Request");
      print_http_hdr(&http_sm->t_state.hdr_info.server_request, "Server Request");
      print_http_hdr(&http_sm->t_state.hdr_info.server_response, "Server Response");
      print_http_hdr(&http_sm->t_state.hdr_info.client_response, "Client Response");

      dump_history(http_sm);

      printf("------------------------------------------------\n\n\n");
    } else if (http_sm->magic == HTTP_SM_MAGIC_DEAD) {
      if (is_debug_tag_set("magic")) {
#if defined(linux)
        printf("\n*****match-DEAD*****\n");
#endif
      }
    }
    ats_free(http_sm);
  } else {
    printf("process_HttpSM : last_seen_http_sm == core_ptr\n");
  }
}

void
CoreUtils::print_http_hdr(HTTPHdr *h, const char *name)
{
  HTTPHdr new_handle;

  if (h->m_heap && h->m_http) {
    int r = load_http_hdr(h, &new_handle);

    if (r > 0 && new_handle.m_http) {
      printf("----------- %s  ------------\n", name);
      new_handle.m_mime = new_handle.m_http->m_fields_impl;
      new_handle.print(nullptr, 0, nullptr, nullptr);
      printf("-----------------------------\n\n");
    }
  }
}

int
CoreUtils::load_http_hdr(HTTPHdr *core_hdr, HTTPHdr *live_hdr)
{
  char buf[sizeof(char) * sizeof(HdrHeap)];
  // Load HdrHeap chain
  HTTPHdr *http_hdr                 = core_hdr;
  HdrHeap *heap                     = (HdrHeap *)core_hdr->m_heap;
  HdrHeap *heap_ptr                 = (HdrHeap *)http_hdr->m_heap;
  intptr_t ptr_heaps                = 0;
  intptr_t ptr_heap_size            = 0;
  intptr_t ptr_xl_size              = 2;
  intptr_t str_size                 = 0;
  intptr_t str_heaps                = 0;
  MarshalXlate default_MarshalXlate = {nullptr, nullptr, nullptr};
  DynArray<struct MarshalXlate> ptr_xlation(&default_MarshalXlate, 2);
  // MarshalXlate static_table[2];
  // MarshalXlate* ptr_xlation = static_table;
  intptr_t used;
  intptr_t i;
  intptr_t copy_size;

  // extracting the header heap from the core file
  do {
    if (read_from_core((intptr_t)heap, sizeof(HdrHeap), buf) == -1) {
      printf("Cannot read from core\n");
      ::exit(0);
    }
    heap      = (HdrHeap *)buf;
    copy_size = (int)(heap->m_free_start - heap->m_data_start);
    ptr_heap_size += copy_size;
    heap = heap->m_next;
  } while (heap && ((intptr_t)heap != 0x1));

  swizzle_heap     = (HdrHeap *)ats_malloc(sizeof(HdrHeap));
  live_hdr->m_heap = swizzle_heap;
  ptr_data         = (char *)ats_malloc(sizeof(char) * ptr_heap_size);
  // heap = (HdrHeap*)http_hdr->m_heap;

  //  Build Hdr Heap Translation Table
  do {
    if (read_from_core((intptr_t)heap_ptr, sizeof(HdrHeap), buf) == -1) {
      printf("Cannot read from core\n");
      ::exit(0);
    }
    heap_ptr  = (HdrHeap *)buf;
    copy_size = (int)(heap_ptr->m_free_start - heap_ptr->m_data_start);

    if (read_from_core((intptr_t)heap_ptr->m_data_start, copy_size, ptr_data) == -1) {
      printf("Cannot read from core\n");
      ::exit(0);
    }
    // Expand ptr xlation table if necessary
    if (ptr_heaps >= ptr_xl_size) {
      ptr_xlation(ptr_heaps);
    }

    char *data, *free, *off;
    data = heap_ptr->m_data_start;
    free = heap_ptr->m_free_start;
    off  = (char *)(heap_ptr->m_data_start - ptr_data);

    ptr_xlation[ptr_heaps].start  = data;
    ptr_xlation[ptr_heaps].end    = free;
    ptr_xlation[ptr_heaps].offset = off;
    ptr_data += copy_size;
    ptr_heaps++;
    heap_ptr = heap_ptr->m_next;
  } while (heap_ptr && ((intptr_t)heap_ptr != 0x1));

  heap = (HdrHeap *)http_hdr->m_heap;
  if (read_from_core((intptr_t)heap, sizeof(HdrHeap), buf) == -1) {
    printf("Cannot read from core\n");
    ::exit(0);
  }
  heap = (HdrHeap *)buf;
  // filling in the live_hdr
  swizzle_heap->m_free_start            = nullptr;
  swizzle_heap->m_data_start            = (char *)ptr_data - ptr_heap_size; // offset
  swizzle_heap->m_magic                 = HDR_BUF_MAGIC_ALIVE;
  swizzle_heap->m_writeable             = false;
  swizzle_heap->m_size                  = ptr_heap_size;
  swizzle_heap->m_next                  = nullptr;
  swizzle_heap->m_free_size             = 0;
  swizzle_heap->m_read_write_heap.m_ptr = nullptr;

  // We'have one read-only string heap after marshalling
  swizzle_heap->m_ronly_heap[0].m_heap_start          = (char *)(intptr_t)swizzle_heap->m_size; // offset
  swizzle_heap->m_ronly_heap[0].m_ref_count_ptr.m_ptr = nullptr;

  for (int i = 1; i < HDR_BUF_RONLY_HEAPS; i++) {
    swizzle_heap->m_ronly_heap[i].m_heap_start = nullptr;
  }

  // Next order of business is to copy over string heaps
  //   As we are copying over the string heaps, build
  //   translation table for string marshaling in the heap
  //   objects
  MarshalXlate str_xlation[HDR_BUF_RONLY_HEAPS + 1];

  // Local String Heaps, building translation table
  if (heap->m_read_write_heap) {
    HdrStrHeap *hdr  = (HdrStrHeap *)heap->m_read_write_heap.m_ptr;
    char *copy_start = ((char *)heap->m_read_write_heap.m_ptr) + sizeof(HdrStrHeap);
    char *str_hdr    = (char *)ats_malloc(sizeof(char) * sizeof(HdrStrHeap));
    if (read_from_core((intptr_t)hdr, sizeof(HdrStrHeap), str_hdr) == -1) {
      printf("Cannot read from core\n");
      ::exit(0);
    }

    char *free_start = (char *)(((HdrStrHeap *)str_hdr)->m_free_start);
    int nto_copy     = std::abs((char *)copy_start - free_start);
    ats_free(str_hdr);
    char rw_heap[sizeof(char) * nto_copy];
    if (read_from_core((intptr_t)copy_start, nto_copy, rw_heap) == -1) {
      printf("Cannot read from core\n");
      ::exit(0);
    }
    // FIX ME - possible offset overflow issues?
    str_xlation[str_heaps].start  = copy_start;
    str_xlation[str_heaps].end    = copy_start + nto_copy;
    str_xlation[str_heaps].offset = (char *)(copy_start - rw_heap);

    str_size += nto_copy;
    str_heaps++;
  }

  for (i = 0; i < HDR_BUF_RONLY_HEAPS; i++) {
    if (heap->m_ronly_heap[i].m_heap_start != nullptr) {
      char ro_heap[sizeof(char) * heap->m_ronly_heap[i].m_heap_len];
      if (read_from_core((intptr_t)heap->m_ronly_heap[i].m_heap_start, heap->m_ronly_heap[i].m_heap_len, ro_heap) == -1) {
        printf("Cannot read from core\n");
        ::exit(0);
      }
      // Add translation table entry for string heaps
      str_xlation[str_heaps].start  = heap->m_ronly_heap[i].m_heap_start;
      str_xlation[str_heaps].end    = heap->m_ronly_heap[i].m_heap_start + heap->m_ronly_heap[i].m_heap_len;
      str_xlation[str_heaps].offset = (char *)(heap->m_ronly_heap[i].m_heap_start - ro_heap);

      ink_assert(str_xlation[str_heaps].start <= str_xlation[str_heaps].end);

      str_heaps++;
      str_size += heap->m_ronly_heap[i].m_heap_len;
    }
  }

  // Patch the str heap len
  swizzle_heap->m_ronly_heap[0].m_heap_len = str_size;

  char *obj_data  = swizzle_heap->m_data_start;
  char *mheap_end = swizzle_heap->m_data_start + swizzle_heap->m_size;

  while (obj_data < mheap_end) {
    HdrHeapObjImpl *obj = (HdrHeapObjImpl *)obj_data;
    ink_assert(obj_is_aligned(obj));

    switch (obj->m_type) {
    case HDR_HEAP_OBJ_URL:
      if (((URLImpl *)obj)->marshal(str_xlation, str_heaps) < 0) {
        goto Failed;
      }
      break;
    case HDR_HEAP_OBJ_HTTP_HEADER:
      if (((HTTPHdrImpl *)obj)->marshal(ptr_xlation, ptr_heaps, str_xlation, str_heaps) < 0) {
        goto Failed;
      }
      live_hdr->m_http = (HTTPHdrImpl *)obj;
      break;
    case HDR_HEAP_OBJ_FIELD_BLOCK:
      if (((MIMEFieldBlockImpl *)obj)->marshal(ptr_xlation, ptr_heaps, str_xlation, str_heaps) < 0) {
        goto Failed;
      }
      break;
    case HDR_HEAP_OBJ_MIME_HEADER:
      if (((MIMEHdrImpl *)obj)->marshal(ptr_xlation, ptr_heaps, str_xlation, str_heaps)) {
        goto Failed;
      }
      break;
    case HDR_HEAP_OBJ_EMPTY:
      break;
    case HDR_HEAP_OBJ_RAW:
      // Check to make sure we aren't stuck
      //   in an infinite loop
      if (obj->m_length <= 0) {
        ink_assert(0);
        goto Failed;
      }
      // Nothing to do
      break;
    default:
      ink_release_assert(0);
    }
    obj_data = obj_data + obj->m_length;
  }

  // Add up the total bytes used
  used = ptr_heap_size + str_size + HDR_HEAP_HDR_SIZE;
  used = ROUND(used, HDR_PTR_SIZE);

  return used;

Failed:
  swizzle_heap->m_magic = HDR_BUF_MAGIC_CORRUPT;
  return -1;
}

void
CoreUtils::dump_history(HttpSM *hsm)
{
  printf("-------- Begin History -------------\n");

  // Loop through the history and dump it
  for (unsigned int i = 0; i < hsm->history.size(); i++) {
    char loc[256];
    int r          = (int)hsm->history[i].reentrancy;
    int e          = (int)hsm->history[i].event;
    char *fileline = load_string(hsm->history[i].location.str(loc, sizeof(loc)));

    fileline = (fileline != nullptr) ? fileline : ats_strdup("UNKNOWN");

    printf("%d   %d   %s", e, r, fileline);

    char buffer[32];
    const char *msg = event_int_to_string(e, sizeof(buffer), buffer);
    printf("   event string: \"%s\"\n", msg);

    ats_free(fileline);
  }

  printf("-------- End History -----------\n\n");
}

void
CoreUtils::process_EThread(EThread *eth_test)
{
  char *buf = (char *)ats_malloc(sizeof(char) * sizeof(EThread));

  if (read_from_core((intptr_t)eth_test, sizeof(EThread), buf) != -1) {
    EThread *loaded_eth = (EThread *)buf;

    printf("----------- EThread @ 0x%p ----------\n", eth_test);
#if !defined(kfreebsd) && (defined(freebsd) || defined(darwin) || defined(openbsd))
    printf("   thread_id: %p\n", loaded_eth->tid);
#else
    printf("   thread_id: %i\n", (int)loaded_eth->tid);
#endif
    //    printf("   NetHandler: 0x%x\n\n", (int) loaded_eth->netHandler);
  }

  ats_free(buf);
}

void
CoreUtils::print_netstate(NetState *n)
{
  printf("      enabled: %d\n", n->enabled);
  printf("      op: %d  cont: 0x%p\n", n->vio.op, n->vio.cont);
  printf("      nbytes: %d  done: %d\n", (int)n->vio.nbytes, (int)n->vio.ndone);
  printf("      vc_server: 0x%p   mutex: 0x%p\n\n", n->vio.vc_server, n->vio.mutex.m_ptr);
}

void
CoreUtils::process_NetVC(UnixNetVConnection *nvc_test)
{
  char *buf = (char *)ats_malloc(sizeof(char) * sizeof(UnixNetVConnection));

  if (read_from_core((intptr_t)nvc_test, sizeof(UnixNetVConnection), buf) != -1) {
    UnixNetVConnection *loaded_nvc = (UnixNetVConnection *)buf;
    char addrbuf[INET6_ADDRSTRLEN];

    printf("----------- UnixNetVConnection @ 0x%p ----------\n", nvc_test);
    printf("     ip: %s    port: %d\n", ats_ip_ntop(loaded_nvc->get_remote_addr(), addrbuf, sizeof(addrbuf)),
           ats_ip_port_host_order(loaded_nvc->get_remote_addr()));
    printf("     closed: %d\n\n", loaded_nvc->closed);
    printf("     read state: \n");
    print_netstate(&loaded_nvc->read);
    printf("     write state: \n");
    print_netstate(&loaded_nvc->write);
  }

  ats_free(buf);
}

char *
CoreUtils::load_string(const char *addr)
{
  char buf[2048];
  int index = 0;

  if (addr == nullptr) {
    return ats_strdup("NONE");
  }

  while (index < 2048) {
    if (read_from_core((intptr_t)(addr + index), 1, buf + index) < 0) {
      return nullptr;
    }

    if (buf[index] == '\0') {
      return ats_strdup(buf);
    }
    index++;
  }

  return nullptr;
}

// parses core file
#if defined(linux)
void
process_core(char *fname)
{
  Elf32_Ehdr ehdr;
  Elf32_Phdr phdr;
  int phoff, phnum, phentsize;
  int framep = 0, pc = 0;

  /* Open the input file */
  if (!(fp = fopen(fname, "r"))) {
    printf("cannot open file\n");
    ::exit(1);
  }

  /* Obtain the .shstrtab data buffer */
  if (fread(&ehdr, sizeof ehdr, 1, fp) != 1) {
    printf("Unable to read ehdr\n");
    ::exit(1);
  }
  // program header offset
  phoff = ehdr.e_phoff;
  // number of program headers
  phnum = ehdr.e_phnum;
  // size of each program header
  phentsize = ehdr.e_phentsize;

  for (int i = 0; i < phnum; i++) {
    if (fseek(fp, phoff + i * phentsize, SEEK_SET) == -1) {
      fprintf(stderr, "Unable to seek to Phdr %d\n", i);
      ::exit(1);
    }

    if (fread(&phdr, sizeof phdr, 1, fp) != 1) {
      fprintf(stderr, "Unable to read Phdr %d\n", i);
      ::exit(1);
    }
    int poffset, psize;
    int pvaddr;
    /* This member gives the virtual address at which the first byte of the
       segment resides in memory. */
    pvaddr = phdr.p_vaddr;
    /* This member gives the offset from the beginning of the file at which
       the first byte of the segment resides. */
    poffset = phdr.p_offset;
    /* This member gives the number of bytes in the file image of the
       segment; it may be zero. */
    psize = phdr.p_filesz;

    if (pvaddr != 0) {
      CoreUtils::insert_table(pvaddr, poffset, psize);
    }

    if (is_debug_tag_set("phdr")) {
      printf("\n******* PHDR %d *******\n", i);
      printf("p_type = %u  ", phdr.p_type);
      printf("p_offset = %u  ", phdr.p_offset);
      printf("p_vaddr = %#x  ", pvaddr);

      printf("p_paddr = %#x\n", phdr.p_paddr);
      printf("p_filesz = %u  ", phdr.p_filesz);
      printf("p_memsz = %u  ", phdr.p_memsz);
      printf("p_flags = %u  ", phdr.p_flags);
      printf("p_align = %u\n", phdr.p_align);
    }

    if (phdr.p_type == PT_NOTE) {
      printf("NOTE\n");
      if (fseek(fp, phdr.p_offset, SEEK_SET) != -1) {
        Elf32_Nhdr *nhdr, *thdr;
        if ((nhdr = (Elf32_Nhdr *)ats_malloc(sizeof(Elf32_Nhdr) * phdr.p_filesz))) {
          if (fread(nhdr, phdr.p_filesz, 1, fp) == 1) {
            int size = phdr.p_filesz;
            int sum  = 0;
            thdr     = nhdr;
            while (size) {
              int len;

              len = sizeof *thdr + ((thdr->n_namesz + 3) & ~3) + ((thdr->n_descsz + 3) & ~3);
              // making sure the offset is byte aligned
              char *offset = (char *)(thdr + 1) + ((thdr->n_namesz + 3) & ~3);

              if (len < 0 || len > size) {
                ::exit(1);
              }
              printf("size=%d, len=%d\n", size, len);

              prstatus_t pstat;
              prstatus_t *ps;
              prpsinfo_t infostat, *ist;
              elf_gregset_t rinfo;
              unsigned int j;

              switch (thdr->n_type) {
              case NT_PRSTATUS:
                ps = (prstatus_t *)offset;
                memcpy(&pstat, ps, sizeof(prstatus_t));
                printf("\n*** printing registers****\n");
                for (j = 0; j < ELF_NGREG; j++) {
                  rinfo[j] = pstat.pr_reg[j];
                  printf("%#x ", (unsigned int)rinfo[j]);
                }
                printf("\n");

                printf("\n**** NT_PRSTATUS ****\n");

                printf("Process id = %d\n", pstat.pr_pid);
                printf("Parent Process id = %d\n", pstat.pr_ppid);

                printf("Signal that caused this core dump is signal  = %d\n", pstat.pr_cursig);

                printf("stack pointer = %#x\n", (unsigned int)pstat.pr_reg[SP_REGNUM]); // UESP
                framep = pstat.pr_reg[FP_REGNUM];
                pc     = pstat.pr_reg[PC_REGNUM];
                printf("frame pointer = %#x\n", (unsigned int)pstat.pr_reg[FP_REGNUM]); // EBP
                printf("program counter if no save = %#x\n", (unsigned int)pstat.pr_reg[PC_REGNUM]);
                break;

              case NT_PRPSINFO:
                ist = (prpsinfo_t *)offset;
                memcpy(&infostat, ist, sizeof(prpsinfo_t));

                if (is_debug_tag_set("note")) {
                  printf("\n**** NT_PRPSINFO of active process****\n");
                  printf("process state = %c\n", infostat.pr_state);
                  printf("Name of the executable = %s\n", infostat.pr_fname);
                  printf("Arg List = %s\n", infostat.pr_psargs);

                  printf("process id = %d\n", infostat.pr_pid);
                }
                break;
              }
              thdr = (Elf32_Nhdr *)((char *)thdr + len);
              sum += len;
              size -= len;
            }
          }
          ats_free(nhdr);
        }
      }
    }
  }
  framepointer    = framep;
  program_counter = pc;

  // Write your actual tests here
  CoreUtils::find_stuff(&CoreUtils::test_HdrHeap);
  CoreUtils::find_stuff(&CoreUtils::test_HttpSM);

  fclose(fp);
}
#endif

#if !defined(linux)
void
process_core(char *fname)
{
  // do not make it fatal!!!!
  Warning("Only supported on Sparc Solaris and Linux");
}
#endif
