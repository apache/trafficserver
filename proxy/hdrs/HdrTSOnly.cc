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

   HdrTSOnly.cc

   Description:
      IRIX compiler is rather annoying and likes to use symbols from inline
        code that is not called in the file.  Thus to make our libhdrs.a
        library link with traffic_manager and test_header, we can't
        include IOBuffer.h in any file where the corresponding object file
        gets linked in for these two targets.  Thus HdrTSOnly.cc is where
        we put the functions that only traffic_server uses since they
        need to know about IOBuffers.


 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "HTTP.h"
#include "P_EventSystem.h"

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

ParseResult
HTTPHdr::parse_req(HTTPParser *parser, IOBufferReader *r, int *bytes_used, bool eof, bool strict_uri_parsing,
                   size_t max_request_line_size, size_t max_hdr_field_size)
{
  const char *start;
  const char *tmp;
  const char *end;
  int used;

  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTP_TYPE_REQUEST);

  ParseResult state = PARSE_RESULT_CONT;
  *bytes_used       = 0;

  do {
    int64_t b_avail = r->block_read_avail();

    if (b_avail <= 0 && eof == false) {
      break;
    }

    tmp = start = r->start();
    end         = start + b_avail;

    int heap_slot = m_heap->attach_block(r->get_current_block(), start);

    m_heap->lock_ronly_str_heap(heap_slot);
    state = http_parser_parse_req(parser, m_heap, m_http, &tmp, end, false, eof, strict_uri_parsing, max_request_line_size,
                                  max_hdr_field_size);
    m_heap->set_ronly_str_heap_end(heap_slot, tmp);
    m_heap->unlock_ronly_str_heap(heap_slot);

    used = static_cast<int>(tmp - start);
    r->consume(used);
    *bytes_used += used;

  } while (state == PARSE_RESULT_CONT);

  return state;
}

ParseResult
HTTPHdr::parse_resp(HTTPParser *parser, IOBufferReader *r, int *bytes_used, bool eof)
{
  const char *start;
  const char *tmp;
  const char *end;
  int used;

  ink_assert(valid());
  ink_assert(m_http->m_polarity == HTTP_TYPE_RESPONSE);

  ParseResult state = PARSE_RESULT_CONT;
  *bytes_used       = 0;

  do {
    int64_t b_avail = r->block_read_avail();
    tmp = start = r->start();

    // No data currently available.
    if (b_avail <= 0) {
      if (eof == false) { // more data may arrive later, return CONTINUE state.
        break;
      } else if (nullptr == start) {
        // EOF on empty MIOBuffer - that's a fail, don't bother with parsing.
        // (otherwise will attempt to attach_block a non-existent block)
        state = PARSE_RESULT_ERROR;
        break;
      }
    }

    end = start + b_avail;

    int heap_slot = m_heap->attach_block(r->get_current_block(), start);

    m_heap->lock_ronly_str_heap(heap_slot);
    state = http_parser_parse_resp(parser, m_heap, m_http, &tmp, end, false, eof);
    m_heap->set_ronly_str_heap_end(heap_slot, tmp);
    m_heap->unlock_ronly_str_heap(heap_slot);

    used = static_cast<int>(tmp - start);
    r->consume(used);
    *bytes_used += used;

  } while (state == PARSE_RESULT_CONT);

  return state;
}

// void HdrHeap::set_ronly_str_heap_end(int slot)
//
//    The end pointer is where the header parser stopped parsing
//      so that we don't get extraneous space in the block
//      that then has to get marshalled (INKqa07409)
//
//    NOTE: the shortening the block relies on the fact that
//      IOBuffers are write once.  It's therefore not possible
//      that a previous call actually used more the block than
//      the current call which would mean we can't shorten the block
//
void
HdrHeap::set_ronly_str_heap_end(int slot, const char *end)
{
  ink_assert(m_ronly_heap[slot].m_heap_start != nullptr);
  ink_assert(m_ronly_heap[slot].m_heap_start <= end);
  ink_assert(end <= m_ronly_heap[slot].m_heap_start + m_ronly_heap[slot].m_heap_len);

  m_ronly_heap[slot].m_heap_len = static_cast<int>(end - m_ronly_heap[slot].m_heap_start);
}

// void HdrHeap::attach_block(IOBufferBlock* b, const char* use_start)
//
//    Attachs data from an IOBuffer block to
//      as a read-only string heap.  Looks through existing
//      to expand an existing string heap entry if necessary
//
//    Because the block may contain data at the front of it that
//      we don't want (and will end up getting marshalled)
//      use_start specificies where we start using the block (INKqa07409)
//
int
HdrHeap::attach_block(IOBufferBlock *b, const char *use_start)
{
  ink_assert(m_writeable);

RETRY:

  // It's my contention that since heaps are add to the first available slot, one you find an empty
  // slot it's not possible that a heap ptr for this block exists in a later slot

  for (auto &heap : m_ronly_heap) {
    if (heap.m_heap_start == nullptr) {
      // Add block to heap in this slot
      heap.m_heap_start    = static_cast<char const *>(use_start);
      heap.m_heap_len      = static_cast<int>(b->end() - b->start());
      heap.m_ref_count_ptr = b->data.object();
      //          printf("Attaching block at %X for %d in slot %d\n",
      //                 m_ronly_heap[i].m_heap_start,
      //                 m_ronly_heap[i].m_heap_len,
      //                 i);
      return &heap - m_ronly_heap;
    } else if (heap.m_heap_start == b->buf()) {
      // This block is already on the heap so just extend
      //   it's range
      heap.m_heap_len = static_cast<int>(b->end() - b->buf());
      //          printf("Extending block at %X to %d in slot %d\n",
      //                 m_ronly_heap[i].m_heap_start,
      //                 m_ronly_heap[i].m_heap_len,
      //                 i);
      return &heap - m_ronly_heap;
    }
  }

  // We didn't find an open block slot so we'll have to create one
  coalesce_str_heaps();
  goto RETRY;
}
