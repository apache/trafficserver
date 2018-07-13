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
CacheScan::Scan(bool search)
{
  int64_t guessed_size = 1048576; // 1M
  Errata zret;
  std::bitset<65536> dir_bitset;
  char *stripe_buff2 = (char *)ats_memalign(ats_pagesize(), guessed_size);
  for (int s = 0; s < this->stripe->_segments; s++) {
    dir_bitset.reset();
    for (int b = 0; b < this->stripe->_buckets; b++) {
      CacheDirEntry *seg = this->stripe->dir_segment(s);
      CacheDirEntry *e   = dir_bucket(b, seg);
      if (dir_offset(e)) {
        do {
          // loop detected
          if (dir_bitset[dir_to_offset(e, seg)]) {
            break;
          }
          int64_t size = dir_approx_size(e);
          if (size > guessed_size) {
            ats_free(stripe_buff2);
            stripe_buff2 = (char *)ats_memalign(ats_pagesize(), dir_approx_size(e));
          }
          int fd         = this->stripe->_span->_fd;
          int64_t offset = this->stripe->stripe_offset(e);
          ssize_t n      = pread(fd, stripe_buff2, size, offset);
          if (n < 0) {
            std::cout << "Failed to read content from the Stripe.  " << strerror(errno) << std::endl;
          } else {
            Doc *doc = reinterpret_cast<Doc *>(stripe_buff2);
            get_alternates(doc->hdr(), doc->hlen, search);
          }
          dir_bitset[dir_to_offset(e, seg)] = true;
          e                                 = next_dir(e, seg);
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
    zret.push(0, 0, "Unknown Polarity of HTTPHdrImpl* obj");
    return zret;
  }

  HDR_UNMARSHAL_PTR(obj->m_fields_impl, MIMEHdrImpl, offset);
  return zret;
}

Errata
CacheScan::unmarshal(MIMEHdrImpl *obj, intptr_t offset)
{
  Errata zret;
  HDR_UNMARSHAL_PTR(obj->m_fblock_list_tail, MIMEFieldBlockImpl, offset);
  this->unmarshal(&obj->m_first_fblock, offset);
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
CacheScan::unmarshal(MIMEFieldBlockImpl *mf, intptr_t offset)
{
  Errata zret;
  HDR_UNMARSHAL_PTR(mf->m_next, MIMEFieldBlockImpl, offset);
  ts::MemSpan mf_mem((char *)mf, mf->m_length);
  for (uint32_t index = 0; index < mf->m_freetop; index++) {
    MIMEField *field = &(mf->m_field_slots[index]);

    // check if out of bounds
    if (!mf_mem.contains((char *)field)) {
      zret.push(0, 0, "Out of bounds memory in the deserialized MIMEFieldBlockImpl");
      return zret;
    }
    if (field && field->m_readiness == MIME_FIELD_SLOT_READINESS_LIVE) {
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

int
CacheScan::unmarshal(HdrHeap *hh, int buf_length, int obj_type, HdrHeapObjImpl **found_obj, RefCountObj *block_ref)
{
  int zret   = -1;
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

  if (hh->m_writeable != false) {
    std::cerr << "m_writable has to be true" << std::endl;
    return 0;
  } else if (hh->m_free_size != 0) {
    std::cerr << "m_free_size is not 0" << std::endl;
    return 0;
  } else if (hh->m_ronly_heap[0].m_heap_start == nullptr) {
    std::cerr << "m_ronly_heap is nullptr" << std::endl;
    return 0;
  }

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
    if (!obj_is_aligned(obj)) {
      std::cout << "Invalid alignmgnt of object of type HdrHeapObjImpl" << std::endl;
      return zret;
    }

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
    case HDR_HEAP_OBJ_FIELD_BLOCK:
      this->unmarshal((MIMEFieldBlockImpl *)obj, offset);
      break;
    case HDR_HEAP_OBJ_MIME_HEADER:
      this->unmarshal((MIMEHdrImpl *)obj, offset);
      break;
    case HDR_HEAP_OBJ_EMPTY:
      // Nothing to do
      break;
    default:
      std::cout << "WARNING: Unmarshal failed due to unknown obj type " << (int)obj->m_type << " after "
                << (int)(obj_data - (char *)hh) << " bytes" << std::endl;
      // dump_heap(unmarshal_size);
      return zret;
    }
    if (obj->m_length <= 0) {
      std::cerr << "Invalid object length for deserialization" << obj->m_length << std::endl;
      break;
    }

    obj_data = obj_data + obj->m_length;
  }

  hh->m_magic = HDR_BUF_MAGIC_ALIVE;

  int unmarshal_length = ROUND(hh->unmarshal_size(), HDR_PTR_SIZE);
  return unmarshal_length;
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

  // usually the fragment count is less or equal to 4
  if (alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    // stuff that didn't fit in the integral slots.
    int extra = sizeof(uint64_t) * alt->m_frag_offset_count - sizeof(alt->m_integral_frag_offsets);
    if (extra >= len || extra < 0) {
      zret.push(0, 0, "Invalid Fragment Count ", extra);
      return zret;
    }
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
  if (heap != nullptr && ((char *)heap - buf) < len) {
    tmp = this->unmarshal(heap, len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&hh, block_ref);
    if (hh == nullptr || tmp < 0) {
      zret.push(0, 0, "HTTPInfo::request unmarshal failed");
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
  if (heap != nullptr && ((char *)heap - buf) < len) {
    tmp = this->unmarshal(heap, len, HDR_HEAP_OBJ_HTTP_HEADER, (HdrHeapObjImpl **)&hh, block_ref);
    if (hh == nullptr || tmp < 0) {
      zret.push(0, 0, "HTTPInfo::response unmarshal failed");
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

// check if the url looks valid
bool
CacheScan::check_url(ts::MemSpan &mem, URLImpl *url)
{
  bool in_bound = false; // boolean to check if address in bound
  if (!url->m_ptr_scheme) {
    in_bound = true; // nullptr is valid
  } else if (mem.contains((char *)url->m_ptr_scheme)) {
    in_bound = true;
  }

  return in_bound && mem.contains((char *)url) && !(url == nullptr || url->m_length <= 0 || url->m_type != HDR_HEAP_OBJ_URL);
}

Errata
CacheScan::get_alternates(const char *buf, int length, bool search)
{
  Errata zret;
  ink_assert(!(((intptr_t)buf) & 3)); // buf must be aligned

  char *start            = (char *)buf;
  RefCountObj *block_ref = nullptr;
  ts::MemSpan doc_mem((char *)buf, length);

  while (length - (buf - start) > (int)sizeof(HTTPCacheAlt)) {
    HTTPCacheAlt *a = (HTTPCacheAlt *)buf;

    if (a->m_magic == CACHE_ALT_MAGIC_MARSHALED) {
      zret = this->unmarshal((char *)buf, length, block_ref);
      if (zret.size()) {
        std::cerr << zret << std::endl;
        return zret;
      } else if (!a->m_request_hdr.m_http) {
        std::cerr << "no http object found in the request header object" << std::endl;
        return zret;
      } else if (!doc_mem.contains((char *)a->m_request_hdr.m_http)) {
        std::cerr << "out of bounds request header in the alternate" << std::endl;
        return zret;
      }

      auto *url = a->m_request_hdr.m_http->u.req.m_url_impl;
      if (check_url(doc_mem, url)) {
        std::string str;

        if (search) {
          ts::bwprint(str, "{}://{}:{}/{};{}?{}", std::string_view(url->m_ptr_scheme, url->m_len_scheme),
                      std::string_view(url->m_ptr_host, url->m_len_host), std::string_view(url->m_ptr_port, url->m_len_port),
                      std::string_view(url->m_ptr_path, url->m_len_path), std::string_view(url->m_ptr_params, url->m_len_params),
                      std::string_view(url->m_ptr_query, url->m_len_query));
          if (u_matcher->match(str.data())) {
            str = this->stripe->hashText + " " + str;
            std::cout << "match found " << str << std::endl;
          }
        } else {
          ts::bwprint(str, "stripe: {} : {}://{}:{}/{};{}?{}", std::string_view(this->stripe->hashText),
                      std::string_view(url->m_ptr_scheme, url->m_len_scheme), std::string_view(url->m_ptr_host, url->m_len_host),
                      std::string_view(url->m_ptr_port, url->m_len_port), std::string_view(url->m_ptr_path, url->m_len_path),
                      std::string_view(url->m_ptr_params, url->m_len_params), std::string_view(url->m_ptr_query, url->m_len_query));
          std::cout << str << std::endl;
        }
      } else {
        std::cerr << "The retrieved url object is invalid" << std::endl;
      }
    } else {
      // std::cout << "alternate retrieval failed" << std::endl;
      break;
    }

    buf += a->m_unmarshal_len;
  }

  return zret;
}

} // end namespace ct
