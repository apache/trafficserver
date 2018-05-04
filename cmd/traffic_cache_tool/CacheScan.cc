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

#include "CacheScan.h"
#include "../../proxy/hdrs/HTTP.h"
#include "../../proxy/hdrs/HdrHeap.h"
#include "../../proxy/hdrs/MIME.h"
#include "../../proxy/hdrs/URL.h"

// using namespace ct;

const int HTTP_ALT_MARSHAL_SIZE = ROUND(sizeof(HTTPCacheAlt), HDR_PTR_SIZE);

namespace ct
{
Errata
CacheScan::Scan()
{
  int64_t guessed_size = 1048576; // 1M
  Errata zret;
  char *stripe_buff2 = (char *)ats_memalign(ats_pagesize(), guessed_size);
  for (int s = 0; s < this->stripe->_segments; s++) {
    for (int b = 0; b < this->stripe->_buckets; b++) {
      CacheDirEntry *seg = this->stripe->dir_segment(s);
      CacheDirEntry *e   = dir_bucket(b, seg);
      if (dir_offset(e)) {
        do {
          int64_t size = dir_approx_size(e);
          if (size > guessed_size) {
            ats_free(stripe_buff2);
            stripe_buff2 = (char *)ats_memalign(ats_pagesize(), dir_approx_size(e));
          }
          int fd         = this->stripe->_span->_fd;
          int64_t offset = this->stripe->stripe_offset(e);
          ssize_t n      = pread(fd, stripe_buff2, size, offset);
          if (n < size)
            std::cout << "Failed to read content from the Stripe.  " << strerror(n) << std::endl;
          Doc *doc = reinterpret_cast<Doc *>(stripe_buff2);
          get_alternates(doc->hdr(), doc->hlen);

          e = next_dir(e, seg);
        } while (e);
      }
    }
  }
  ats_free(stripe_buff2);

  return zret;
}

Errata
CacheScan::unmarshal(HTTPHdrImpl *obj, intptr_t offset)
{
  Errata zret;
  if (obj->m_polarity == HTTP_TYPE_REQUEST) {
    HDR_UNMARSHAL_STR(obj->u.req.m_ptr_method, offset);
    HDR_UNMARSHAL_PTR(obj->u.req.m_url_impl, URLImpl, offset);
  } else if (obj->m_polarity == HTTP_TYPE_RESPONSE) {
    HDR_UNMARSHAL_STR(obj->u.resp.m_ptr_reason, offset);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }

  HDR_UNMARSHAL_PTR(obj->m_fields_impl, MIMEHdrImpl, offset);
  return zret;
}

Errata
CacheScan::unmarshal(MIMEHdrImpl *obj, intptr_t offset)
{
  Errata zret;
  HDR_UNMARSHAL_PTR(obj->m_fblock_list_tail, MIMEFieldBlockImpl, offset);

  HDR_UNMARSHAL_PTR(obj->m_first_fblock.m_next, MIMEFieldBlockImpl, offset);

  for (uint32_t index = 0; index < obj->m_first_fblock.m_freetop; index++) {
    MIMEField *field = &(obj->m_first_fblock.m_field_slots[index]);

    if (field->is_live()) {
      HDR_UNMARSHAL_STR(field->m_ptr_name, offset);
      HDR_UNMARSHAL_STR(field->m_ptr_value, offset);
      if (field->m_next_dup) {
        HDR_UNMARSHAL_PTR(field->m_next_dup, MIMEField, offset);
      }
    } else {
      // Clear out other types of slots
      field->m_readiness = MIME_FIELD_SLOT_READINESS_EMPTY;
    }
  }
  return zret;
}

Errata
CacheScan::unmarshal(URLImpl *obj, intptr_t offset)
{
  Errata zret;
  HDR_UNMARSHAL_STR(obj->m_ptr_scheme, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_user, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_password, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_host, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_port, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_path, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_params, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_query, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_fragment, offset);
  HDR_UNMARSHAL_STR(obj->m_ptr_printed_string, offset);
  return zret;
}

Errata
CacheScan::unmarshal(HdrHeap *hh, int buf_length, int obj_type, HdrHeapObjImpl **found_obj, RefCountObj *block_ref)
{
  Errata zret;
  *found_obj = nullptr;

  // Check out this heap and make sure it is OK
  if (hh->m_magic != HDR_BUF_MAGIC_MARSHALED) {
    ink_assert(!"HdrHeap::unmarshal bad magic");
    return zret;
  }

  int unmarshal_size = hh->unmarshal_size();
  if (unmarshal_size > buf_length) {
    ink_assert(!"HdrHeap::unmarshal truncated header");
    return zret;
  }

  hh->m_free_start = nullptr;

  ink_release_assert(hh->m_writeable == false);
  ink_release_assert(hh->m_free_size == 0);
  ink_release_assert(hh->m_ronly_heap[0].m_heap_start != nullptr);

  ink_assert(hh->m_free_start == nullptr);

  // Convert Heap offsets to pointers
  hh->m_data_start                 = ((char *)hh) + (intptr_t)hh->m_data_start;
  hh->m_free_start                 = ((char *)hh) + hh->m_size;
  hh->m_ronly_heap[0].m_heap_start = ((char *)hh) + (intptr_t)hh->m_ronly_heap[0].m_heap_start;

  // Crazy Invariant - If we are sitting in a ref counted block,
  //   the HdrHeap lifetime is externally determined.  Whoever
  //   unmarshalls us should keep the block around as long as
  //   they want to use the header.  However, the strings can
  //   live beyond the heap life time because they are copied
  //   by reference into other header heap therefore we need
  //   to the set the refcount ptr for the strings.  We don't
  //   actually increase the refcount here since for the header
  //   the lifetime is explicit but copies will increase
  //   the refcount
  if (block_ref) {
    hh->m_ronly_heap[0].m_ref_count_ptr.swizzle(block_ref);
  }

  // Loop over objects and swizzle there pointer to
  //  live offsets
  char *obj_data  = hh->m_data_start;
  intptr_t offset = (intptr_t)hh;

  while (obj_data < hh->m_free_start) {
    HdrHeapObjImpl *obj = (HdrHeapObjImpl *)obj_data;
    ink_assert(obj_is_aligned(obj));

    if (obj->m_type == (unsigned)obj_type && *found_obj == nullptr) {
      *found_obj = obj;
    }
    // TODO : fix this switch
    switch (obj->m_type) {
    case HDR_HEAP_OBJ_HTTP_HEADER:
      this->unmarshal((HTTPHdrImpl *)obj, offset);
      break;
    case HDR_HEAP_OBJ_URL:
      this->unmarshal((URLImpl *)obj, offset);
      break;
    //        case HDR_HEAP_OBJ_FIELD_BLOCK:
    //          this->unmarshal((MIMEFieldBlockImpl *)obj,offset);
    //          break;
    case HDR_HEAP_OBJ_MIME_HEADER:
      this->unmarshal((MIMEHdrImpl *)obj, offset);
      break;
    //    case HDR_HEAP_OBJ_EMPTY:
    //      // Nothing to do
    //      break;
    default:
      zret.push(0, 0, "WARNING: Unmarshal failed due to unknow obj type ", (int)obj->m_type, " after ",
                (int)(obj_data - (char *)this), " bytes");
      // dump_heap(unmarshal_size);
      return zret;
    }

    obj_data = obj_data + obj->m_length;
  }

  hh->m_magic = HDR_BUF_MAGIC_ALIVE;

  // hh->unmarshal_size = ROUND(unmarshal_size, HDR_PTR_SIZE);
  return zret;
}

Errata
CacheScan::unmarshal(char *buf, int len, RefCountObj *block_ref)
{
  Errata zret;
  HTTPCacheAlt *alt = (HTTPCacheAlt *)buf;
  int orig_len      = len;

  if (alt->m_magic == CACHE_ALT_MAGIC_ALIVE) {
    // Already unmarshaled, must be a ram cache
    //  it
    ink_assert(alt->m_unmarshal_len > 0);
    ink_assert(alt->m_unmarshal_len <= len);
    return zret;
  } else if (alt->m_magic != CACHE_ALT_MAGIC_MARSHALED) {
    ink_assert(!"HTTPInfo::unmarshal bad magic");
    return zret;
  }

  ink_assert(alt->m_unmarshal_len < 0);
  alt->m_magic = CACHE_ALT_MAGIC_ALIVE;
  ink_assert(alt->m_writeable == 0);
  len -= HTTP_ALT_MARSHAL_SIZE;

  if (alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    // stuff that didn't fit in the integral slots.
    int extra       = sizeof(uint64_t) * alt->m_frag_offset_count - sizeof(alt->m_integral_frag_offsets);
    char *extra_src = buf + reinterpret_cast<intptr_t>(alt->m_frag_offsets);
    // Actual buffer size, which must be a power of two.
    // Well, technically not, because we never modify an unmarshalled fragment
    // offset table, but it would be a nasty bug should that be done in the
    // future.
    int bcount = HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS * 2;

    while (bcount < alt->m_frag_offset_count) {
      bcount *= 2;
    }
    alt->m_frag_offsets =
      static_cast<uint64_t *>(ats_malloc(bcount * sizeof(uint64_t))); // WRONG - must round up to next power of 2.
    memcpy(alt->m_frag_offsets, alt->m_integral_frag_offsets, sizeof(alt->m_integral_frag_offsets));
    memcpy(alt->m_frag_offsets + HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS, extra_src, extra);
    len -= extra;
  } else if (alt->m_frag_offset_count > 0) {
    alt->m_frag_offsets = alt->m_integral_frag_offsets;
  } else {
    alt->m_frag_offsets = nullptr; // should really already be zero.
  }

  // request hdrs

  HdrHeap *heap   = (HdrHeap *)(alt->m_request_hdr.m_heap ? (buf + (intptr_t)alt->m_request_hdr.m_heap) : nullptr);
  HTTPHdrImpl *hh = nullptr;
  int tmp         = 0;
  if (heap != nullptr) {
    tmp = this->unmarshal(heap, len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&hh, block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::request unmarshal failed");
      return zret;
    }
    len -= tmp;
    alt->m_request_hdr.m_heap              = heap;
    alt->m_request_hdr.m_http              = hh;
    alt->m_request_hdr.m_mime              = hh->m_fields_impl;
    alt->m_request_hdr.m_url_cached.m_heap = heap;
  }

  // response hdrs

  heap = (HdrHeap *)(alt->m_response_hdr.m_heap ? (buf + (intptr_t)alt->m_response_hdr.m_heap) : nullptr);
  if (heap != nullptr) {
    // tmp = heap->unmarshal(len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&hh, block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::response unmarshal failed");
      return zret;
    }
    len -= tmp;

    alt->m_response_hdr.m_heap = heap;
    alt->m_response_hdr.m_http = hh;
    alt->m_response_hdr.m_mime = hh->m_fields_impl;
  }

  alt->m_unmarshal_len = orig_len - len;

  return zret;
}

Errata
CacheScan::get_alternates(const char *buf, int length)
{
  Errata zret;
  ink_assert(!(((intptr_t)buf) & 3)); // buf must be aligned

  char *start            = (char *)buf;
  RefCountObj *block_ref = nullptr;

  while (length - (buf - start) > (int)sizeof(HTTPCacheAlt)) {
    HTTPCacheAlt *a = (HTTPCacheAlt *)buf;

    if (a->m_magic == CACHE_ALT_MAGIC_MARSHALED) {
      this->unmarshal((char *)buf, length, block_ref);
      //        std::cout << "alternate unmarshal failed" << std::endl;
      //      }
      auto *url       = a->m_request_hdr.m_http->u.req.m_url_impl;
      std::string str = "stripe: " + this->stripe->hashText + " : " + std::string(url->m_ptr_scheme, url->m_len_scheme) + "://" +
                        std::string(url->m_ptr_host, url->m_len_host) + ":" + std::string(url->m_ptr_port, url->m_len_port) + "/" +
                        std::string(url->m_ptr_path, url->m_len_path) + ";" + std::string(url->m_ptr_params, url->m_len_params) +
                        "?" + std::string(url->m_ptr_query, url->m_len_query);
      std::cout << str << std::endl;
    } else {
      // std::cout << "alternate retrieval failed" << std::endl;
      break;
    }

    buf += a->m_unmarshal_len;
  }

  return zret;
}

} // end namespace ct
