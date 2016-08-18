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

   load_http_hdr.cc

   Description:

   Opens a file with a dbx dump of an http hdr and loads it


 ****************************************************************************/

/***************************************************************************
 *  USAGE NOTE:  This program was orignally built for reading TS 3.0.X &
 *    TS 3.5.X header dumps.  These data structures were always continguous.
 *    TS 4.0.X and later have completely redesigned data structures that
 *    are more complicated but much faster.  This program has been adapted
 *    read headers that have been unmarshalled from cache, in which
 *    case they are contiguious.  It's conversion is in a half baked
 *    state and is therefore not useful for must purposes.
 ***************************************************************************/

#include "HdrHeap.h"
#include "MIME.h"
#include "HTTP.h"
#include "ts/Tokenizer.h"
#include "ts/Diags.h"
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void *low_load_addr  = NULL;
void *high_load_addr = NULL;
int heap_load_size   = 0;
int marshalled       = 0;

// Diags *diags;

enum hdr_type {
  UNKNOWN_HDR,
  REQUEST_HDR,
  RESPONSE_HDR,
  HTTP_INFO_HDR,
  RAW_MBUFFER,
};

char *
load_string(const char *s, int len, int offset)
{
  const char *copy_from;
  if (marshalled == 0 && s > low_load_addr && s + len < high_load_addr) {
    copy_from = s + offset;
  } else if (marshalled && ((unsigned int)s) + len < (unsigned int)heap_load_size) {
    copy_from = s + offset;
  } else {
    copy_from = "<BAD>";
    len       = strlen(copy_from);
  }
  char *r = (char *)ats_malloc(len + 1);
  memcpy(r, copy_from, len);
  r[len] = '\0';
  return r;
}

void
process_http_hdr_impl(HdrHeapObjImpl *obj, int offset)
{
  char *s;
  HTTPHdrImpl *hhdr = (HTTPHdrImpl *)obj;

  if (hhdr->m_polarity == HTTP_TYPE_REQUEST) {
    printf("    is a request hdr\n");
    s = load_string(hhdr->u.req.m_ptr_method, hhdr->u.req.m_len_method, offset);
    printf("    method: %s\n", s);
    ats_free(s);
  } else if (hhdr->m_polarity == HTTP_TYPE_RESPONSE) {
    printf("    is a response hdr\n");
    printf("    status code: %d\n", (int)hhdr->u.resp.m_status);
    s = load_string(hhdr->u.resp.m_ptr_reason, hhdr->u.resp.m_len_reason, offset);
    printf("    method: %s\n", s);
    ats_free(s);
  }
}

void
process_mime_block_impl(MIMEFieldBlockImpl *mblock, int offset)
{
  printf("   Processing mime_flock_impl - freetop %d\n", mblock->m_freetop);
  int freetop;
  if (mblock->m_freetop <= MIME_FIELD_BLOCK_SLOTS) {
    freetop = mblock->m_freetop;
  } else {
    freetop = MIME_FIELD_BLOCK_SLOTS;
  }

  char *n, *v;
  for (unsigned int i = 0; i < freetop; i++) {
    MIMEField *f = &mblock->m_field_slots[i];
    if (hdrtoken_is_valid_wks_idx(f->m_wks_idx)) {
      n = ats_strdup(hdrtoken_index_to_wks(f->m_wks_idx));
    } else {
      n = load_string(f->m_ptr_name, f->m_len_name, offset);
    }
    v = load_string(f->m_ptr_value, f->m_len_value, offset);
    printf("    (%d) %s: %s\n", i, n, v);
    ats_free(n);
    ats_free(v);
  }
}

void
process_mime_hdr_impl(HdrHeapObjImpl *obj, int offset)
{
  MIMEHdrImpl *mhdr = (MIMEHdrImpl *)obj;

  process_mime_block_impl(&mhdr->m_first_fblock, offset);
}

void
loop_over_heap_objs(HdrHeap *hdr_heap, int offset)
{
  char *buffer_end;

  printf("Looping over HdrHeap objects @ 0x%X\n", hdr_heap);

  if (hdr_heap->m_magic == HDR_BUF_MAGIC_MARSHALED) {
    printf(" marshalled heap - size %d\n", hdr_heap->m_size);
    hdr_heap->m_data_start = ((char *)hdr_heap) + ROUND(sizeof(HdrHeap), HDR_PTR_SIZE);
    hdr_heap->m_free_start = ((char *)hdr_heap) + hdr_heap->m_size;
  } else {
    buffer_end = hdr_heap->m_free_start;
  }

  printf(" heap len is %d bytes\n", hdr_heap->m_free_start - hdr_heap->m_data_start);

  char *obj_data = hdr_heap->m_data_start;
  while (obj_data < hdr_heap->m_free_start) {
    HdrHeapObjImpl *obj = (HdrHeapObjImpl *)obj_data;

    switch (obj->m_type) {
    case HDR_HEAP_OBJ_HTTP_HEADER:
      printf("  HDR_HEAP_OBJ_HTTP_HEADER %d bytes\n", obj->m_length);
      process_http_hdr_impl(obj, offset);
      break;
    case HDR_HEAP_OBJ_URL:
      printf("  HDR_HEAP_OBJ_URL         %d bytes\n", obj->m_length);
      break;
    case HDR_HEAP_OBJ_FIELD_BLOCK:
      printf("  HDR_HEAP_OBJ_FIELD_BLOCK %d bytes\n", obj->m_length);
      break;
    case HDR_HEAP_OBJ_MIME_HEADER:
      printf("  HDR_HEAP_OBJ_MIME_HEADER %d bytes\n", obj->m_length);
      process_mime_hdr_impl(obj, offset);
      break;
    case HDR_HEAP_OBJ_EMPTY:
      printf("  HDR_HEAP_OBJ_EMPTY       %d bytes\n", obj->m_length);
      break;
    default:
      printf("  OBJ UNKONWN (%d)  %d bytes\n", obj->m_type, obj->m_length);
    }

    if (obj->m_length == 0) {
      printf(" ERROR zero length object\n");
      break;
    }
    obj_data = obj_data + obj->m_length;
  }
}

void
load_buffer(int fd, hdr_type h_type)
{
  struct stat s_info;

  if (fstat(fd, &s_info) < 0) {
    fprintf(stderr, "Failed to stat data file : %s\n", strerror(errno));
    exit(1);
  }

  char *file_buf           = (char *)ats_malloc(sizeof(char) * (s_info.st_size + 1));
  file_buf[s_info.st_size] = '\0';

  // Read in the entire file
  int bytes_to_read = s_info.st_size;
  while (bytes_to_read > 0) {
    int done = read(fd, file_buf, bytes_to_read);

    if (done < 0) {
      fprintf(stderr, "Failed to read data file : %s\n", strerror(errno));
      exit(1);
    } else if (done == 0) {
      fprintf(stderr, "EOF encounted\n");
      exit(1);
    }

    bytes_to_read -= done;
  }

  Tokenizer line_tok("\n");
  Tokenizer el_tok(" \t");

  int num_lines = line_tok.Initialize(file_buf);
  int num_el    = el_tok.Initialize(line_tok[0]);

  if (num_el < 3) {
    fprintf(stderr, "Corrupted data file\n");
    exit(1);
  }

  void *old_addr = NULL;
  if (sscanf(el_tok[0], "%x:", &old_addr) != 1) {
    fprintf(stderr, "Corrupted data file\n");
    exit(1);
  }
  low_load_addr = old_addr;

  int hdr_size;
  if (sscanf(el_tok[4], "%x", &hdr_size) != 1) {
    fprintf(stderr, "Corrupted data file\n");
    exit(1);
  }
  hdr_size       = (num_lines * 16);
  heap_load_size = hdr_size;

  char *hdr_heap = (char *)ats_malloc(hdr_size);
  int bytes_read = 0;
  int cur_line   = 0;

  while (cur_line < num_lines && bytes_read < hdr_size) {
    int *cur_ptr;
    num_el = el_tok.Initialize(line_tok[cur_line]);

    if (sscanf(el_tok[0], "%x:", &high_load_addr) != 1) {
      fprintf(stderr, "Corrupted data file\n");
      exit(1);
    }
    high_load_addr = ((char *)high_load_addr) + (num_el * 4);

    int el;
    for (int i = 1; i < num_el; i++) {
      if (sscanf(el_tok[i], "%x", &el) != 1) {
        fprintf(stderr, "Corrupted data file\n");
        exit(1);
      }
      cur_ptr  = (int *)(hdr_heap + bytes_read);
      *cur_ptr = el;
      bytes_read += 4;
    }
    cur_line++;
  }

  HdrHeap *my_heap = (HdrHeap *)hdr_heap;
  int offset       = hdr_heap - (char *)old_addr;

  // Patch up some values
  if (my_heap->m_magic == HDR_BUF_MAGIC_MARSHALED) {
    //      HdrHeapObjImpl* obj;
    //      my_heap->unmarshal(hdr_size, HDR_HEAP_OBJ_HTTP_HEADER, &obj, NULL);
    marshalled = 1;
    offset     = (int)hdr_heap;
  } else {
    my_heap->m_free_start                 = my_heap->m_free_start + offset;
    my_heap->m_data_start                 = my_heap->m_data_start + offset;
    my_heap->m_ronly_heap[0].m_heap_start = my_heap->m_ronly_heap[0].m_heap_start + offset;
  }
  loop_over_heap_objs(my_heap, offset);
}

int
main(int argc, const char *argv[])
{
  hdr_type h_type = UNKNOWN_HDR;

  http_init();
  diags = new Diags(NULL, NULL);
  if (argc != 3) {
    fprintf(stderr, "Usage: %s req|res <file>\n", argv[0]);
    exit(1);
  }

  if (strcasecmp(argv[1], "req") == 0) {
    h_type = REQUEST_HDR;
  } else if (strcasecmp(argv[1], "resp") == 0) {
    h_type = RESPONSE_HDR;
  } else if (strcasecmp(argv[1], "hinfo") == 0) {
    h_type = HTTP_INFO_HDR;
  } else if (strcasecmp(argv[1], "mbuf") == 0) {
    h_type = RAW_MBUFFER;
  } else {
    fprintf(stderr, "Usage: %s req|resp|hinfo|mbuf <file>\n", argv[0]);
    exit(1);
  }

  int fd = open(argv[2], O_RDONLY);

  if (fd < 0) {
    fprintf(stderr, "Could not open file %s : %s\n", argv[2], strerror(errno));
    exit(1);
  }
  load_buffer(fd, h_type);

  return 0;
}
