/** @file

  A brief file description

  @par License

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
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HdrHeap.h"
#include "proxy/hdrs/MIME.h"
#include "proxy/hdrs/URL.h"

#include <algorithm>

// using namespace ct;

constexpr HdrHeapMarshalBlocks HTTP_ALT_MARSHAL_SIZE = swoc::round_up(sizeof(HTTPCacheAlt));

namespace ct
{
namespace
{
  // Whether the n bytes at p all lie inside mem. An empty range never reads p, so it always fits.
  bool
  span_fits(swoc::MemSpan<char> const &mem, void const *p, size_t n)
  {
    if (n == 0) {
      return true;
    }
    auto const begin = reinterpret_cast<uintptr_t>(mem.data());
    auto const end   = begin + mem.size();
    auto const addr  = reinterpret_cast<uintptr_t>(p);
    return p != nullptr && begin <= addr && addr <= end && n <= end - addr;
  }

  // Whether obj is aligned for T and every byte of it lies inside mem, so its fields can be read.
  template <typename T>
  bool
  object_fits(swoc::MemSpan<char> const &mem, T const *obj)
  {
    return obj != nullptr && reinterpret_cast<uintptr_t>(obj) % alignof(T) == 0 && span_fits(mem, obj, sizeof(T));
  }

  // The marshalled header heap at offset bytes into buf, whose alt_len bytes run from this alternate to the end of the
  // document. nullptr when the heap would overlap the alternate's fixed part, is not aligned for HdrHeap, or leaves too
  // few bytes for a whole HdrHeap header. On success room is set to the bytes from the heap to the end of buf.
  HdrHeap *
  heap_at(char *buf, int alt_len, intptr_t offset, int &room)
  {
    if (offset < static_cast<intptr_t>(sizeof(HTTPCacheAlt)) || offset > alt_len - static_cast<intptr_t>(sizeof(HdrHeap)) ||
        reinterpret_cast<uintptr_t>(buf + offset) % alignof(HdrHeap) != 0) {
      return nullptr;
    }
    room = alt_len - static_cast<int>(offset);
    return reinterpret_cast<HdrHeap *>(buf + offset);
  }
} // namespace

Errata
CacheScan::Scan(bool search)
{
  int64_t            guessed_size = 1048576; // 1M
  Errata             zret;
  std::bitset<65536> dir_bitset;
  char              *stripe_buff2 = static_cast<char *>(ats_memalign(ats_pagesize(), guessed_size));
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
            stripe_buff2 = static_cast<char *>(ats_memalign(ats_pagesize(), dir_approx_size(e)));
          }
          int     fd     = this->stripe->_span->_fd;
          int64_t offset = this->stripe->stripe_offset(e);
          ssize_t n      = pread(fd, stripe_buff2, size, offset);
          Doc    *doc    = reinterpret_cast<Doc *>(stripe_buff2);
          if (n < 0) {
            std::cout << "Failed to read content from the Stripe.  " << strerror(errno) << std::endl;
          } else if (!doc->fits_in(n)) {
            std::cout << "Skipping a document whose lengths do not fit in the " << n << " bytes read from the Stripe" << std::endl;
          } else {
            get_alternates(doc->hdr(), doc->hlen, search,
                           ts::VersionNumber{static_cast<unsigned short>(doc->v_major), static_cast<unsigned short>(doc->v_minor)});
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
  if (obj->m_polarity == HTTPType::REQUEST) {
    HDR_UNMARSHAL_STR(obj->u.req.m_ptr_method, offset);
    HDR_UNMARSHAL_PTR(obj->u.req.m_url_impl, URLImpl, offset);
  } else if (obj->m_polarity == HTTPType::RESPONSE) {
    HDR_UNMARSHAL_STR(obj->u.resp.m_ptr_reason, offset);
  } else {
    zret.note("Unknown Polarity of HTTPHdrImpl* obj");
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
  swoc::MemSpan mf_mem(reinterpret_cast<char *>(mf), mf->m_length);
  for (uint32_t index = 0; index < mf->m_freetop; index++) {
    MIMEField *field = &(mf->m_field_slots[index]);

    // check if out of bounds
    if (!mf_mem.contains(reinterpret_cast<char *>(field))) {
      zret.note("Out of bounds memory in the deserialized MIMEFieldBlockImpl");
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
  if (hh->m_magic != HdrBufMagic::MARSHALED) {
    ink_assert(!"HdrHeap::unmarshal bad magic");
    return zret;
  }

  if (hh->m_size < static_cast<uint32_t>(HDR_HEAP_HDR_SIZE) ||     // heap too small for header
      hh->m_size != (uintptr_t)hh->m_ronly_heap[0].m_heap_start || // string heap offset inconsistent
      hh->m_ronly_heap[0].m_heap_len < 0 ||                        // invalid string heap length
      buf_length < 0 ||                                            // invalid buf_length
      static_cast<uint64_t>(hh->m_size) + static_cast<uint64_t>(hh->m_ronly_heap[0].m_heap_len) >
        static_cast<uint64_t>(buf_length)) {
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

  // HdrHeap::marshal() always puts the objects right after the header, and the object walk below starts there.
  if (reinterpret_cast<uintptr_t>(hh->m_data_start) != static_cast<uintptr_t>(HDR_HEAP_HDR_SIZE.value())) {
    std::cerr << "m_data_start is not right after the heap header" << std::endl;
    return zret;
  }

  // Convert Heap offsets to pointers
  hh->m_data_start                 = (reinterpret_cast<char *>(hh)) + (intptr_t)hh->m_data_start;
  hh->m_free_start                 = (reinterpret_cast<char *>(hh)) + hh->m_size;
  hh->m_ronly_heap[0].m_heap_start = (reinterpret_cast<char *>(hh)) + (intptr_t)hh->m_ronly_heap[0].m_heap_start;

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

  // Loop over objects and swizzle their pointers to
  //  live offsets
  //
  // The loop condition keeps each object's start inside the heap, but nothing yet checks that the object fits before
  // m_free_start, either at its m_length or at the size of the type the switch below treats it as. A MIME field
  // block's unmarshal also checks only that each slot starts inside the block. A damaged heap can therefore make the
  // swizzle below read and write past its end.
  char    *obj_data = hh->m_data_start;
  intptr_t offset   = (intptr_t)hh;

  while (obj_data < hh->m_free_start) {
    HdrHeapObjImpl *obj = reinterpret_cast<HdrHeapObjImpl *>(obj_data);
    if (!obj_is_aligned(obj)) {
      std::cout << "Invalid alignment of object of type HdrHeapObjImpl" << std::endl;
      return zret;
    }

    if (obj->m_type == static_cast<unsigned>(obj_type) && *found_obj == nullptr) {
      *found_obj = obj;
    }
    // TODO : fix this switch
    switch (static_cast<HdrHeapObjType>(obj->m_type)) {
    case HdrHeapObjType::HTTP_HEADER:
      this->unmarshal((HTTPHdrImpl *)obj, offset);
      break;
    case HdrHeapObjType::URL:
      this->unmarshal((URLImpl *)obj, offset);
      break;
    case HdrHeapObjType::FIELD_BLOCK:
      this->unmarshal((MIMEFieldBlockImpl *)obj, offset);
      break;
    case HdrHeapObjType::MIME_HEADER:
      this->unmarshal((MIMEHdrImpl *)obj, offset);
      break;
    case HdrHeapObjType::EMPTY:
      // Nothing to do
      break;
    default:
      std::cout << "WARNING: Unmarshal failed due to unknown obj type " << static_cast<int>(obj->m_type) << " after "
                << static_cast<int>(obj_data - reinterpret_cast<char *>(hh)) << " bytes" << std::endl;
      // dump_heap(unmarshal_size);
      return zret;
    }
    if (obj->m_length <= 0) {
      std::cerr << "Invalid object length for deserialization" << obj->m_length << std::endl;
      break;
    }

    obj_data = obj_data + obj->m_length;
  }

  hh->m_magic = HdrBufMagic::ALIVE;

  return HdrHeapMarshalBlocks(swoc::round_up(hh->unmarshal_size()));
}

Errata
CacheScan::unmarshal(char *buf, int len, RefCountObj *block_ref, ts::VersionNumber version)
{
  Errata        zret;
  HTTPCacheAlt *alt      = reinterpret_cast<HTTPCacheAlt *>(buf);
  int           orig_len = len;

  if (alt->m_magic == CacheAltMagic::ALIVE) {
    // Already unmarshalled, must be a ram cache
    //  it
    ink_assert(alt->m_unmarshal_len > 0);
    ink_assert(alt->m_unmarshal_len <= len);
    return zret;
  } else if (alt->m_magic != CacheAltMagic::MARSHALED && alt->m_magic != CacheAltMagic::MARSHALED_WKS) {
    ink_assert(!"HTTPInfo::unmarshal bad magic");
    return zret;
  }

  bool const has_wks_identity = alt->m_magic == CacheAltMagic::MARSHALED_WKS;

  ink_assert(alt->m_unmarshal_len < 0);
  alt->m_magic = CacheAltMagic::ALIVE;
  ink_assert(alt->m_writeable == 0);
  len -= HTTP_ALT_MARSHAL_SIZE;

  // usually the fragment count is less or equal to 4
  if (alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    if (version >= CACHE_DB_FRAG_OFFSET_TABLE_VERSION) {
      int64_t const  size   = static_cast<int64_t>(alt->m_frag_offset_count) * sizeof(uint64_t);
      intptr_t const offset = reinterpret_cast<intptr_t>(alt->m_frag_offsets);
      if (offset < 0 || size > len || offset > orig_len - size) {
        zret.note("Invalid fragment offset table");
        return zret;
      }
      alt->m_frag_offsets  = reinterpret_cast<uint64_t *>(buf + offset);
      len                 -= size;
    } else {
      // stuff that didn't fit in the integral slots.
      int extra = sizeof(uint64_t) * alt->m_frag_offset_count - sizeof(alt->m_integral_frag_offsets);
      if (extra >= len || extra < 0) {
        zret.note("Invalid Fragment Count {}", extra);
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
    }
  } else if (alt->m_frag_offset_count > 0) {
    alt->m_frag_offsets = alt->m_integral_frag_offsets;
  } else {
    alt->m_frag_offsets = nullptr; // should really already be zero.
  }

  // request hdrs

  HdrHeap     *heap = nullptr;
  HTTPHdrImpl *hh   = nullptr;
  int          tmp  = 0;
  int          room = 0;
  if (alt->m_request_hdr.m_heap != nullptr) {
    heap = heap_at(buf, orig_len, reinterpret_cast<intptr_t>(alt->m_request_hdr.m_heap), room);
    if (heap == nullptr) {
      zret.note("HTTPInfo::request header heap offset is invalid");
      return zret;
    }
    tmp = this->unmarshal(heap, std::min(len, room), static_cast<int>(HdrHeapObjType::HTTP_HEADER),
                          reinterpret_cast<HdrHeapObjImpl **>(&hh), block_ref);
    if (hh == nullptr || tmp < 0) {
      zret.note("HTTPInfo::request unmarshal failed");
      return zret;
    }
    len                                    -= tmp;
    alt->m_request_hdr.m_heap               = heap;
    alt->m_request_hdr.m_http               = hh;
    alt->m_request_hdr.m_mime               = hh->m_fields_impl;
    alt->m_request_hdr.m_url_cached.m_heap  = heap;
  }

  // response hdrs

  if (alt->m_response_hdr.m_heap != nullptr) {
    heap = heap_at(buf, orig_len, reinterpret_cast<intptr_t>(alt->m_response_hdr.m_heap), room);
    if (heap == nullptr) {
      zret.note("HTTPInfo::response header heap offset is invalid");
      return zret;
    }
    tmp = this->unmarshal(heap, std::min(len, room), static_cast<int>(HdrHeapObjType::HTTP_HEADER),
                          reinterpret_cast<HdrHeapObjImpl **>(&hh), block_ref);
    if (hh == nullptr || tmp < 0) {
      zret.note("HTTPInfo::response unmarshal failed");
      return zret;
    }
    len -= tmp;

    alt->m_response_hdr.m_heap = heap;
    alt->m_response_hdr.m_http = hh;
    alt->m_response_hdr.m_mime = hh->m_fields_impl;
  }

  // Unlike HTTPInfo::unmarshal(), this does not rebuild the well-known string indexes the object
  // stores. Those indexes belong to the traffic_server that wrote the object, whose well-known
  // string table need not match this tool's, so nothing here may read them; the scan reads the URL
  // through its stored strings only. Anything added here that wants an index must call
  // HTTPHdrImpl::recompute_wks_indices() first, which means linking libhdrs.
  if (has_wks_identity) {
    if (len < static_cast<int>(sizeof(uint64_t))) {
      zret.note("Truncated WKS identity");
      return zret;
    }
    len -= sizeof(uint64_t);
  }
  alt->m_unmarshal_len = orig_len - len;

  return zret;
}

// Whether url is an aligned URL object wholly inside mem, with every component get_alternates() prints also inside mem.
bool
CacheScan::check_url(swoc::MemSpan<char> &mem, URLImpl *url)
{
  if (!object_fits(mem, url) || url->m_length == 0 || static_cast<HdrHeapObjType>(url->m_type) != HdrHeapObjType::URL) {
    return false;
  }

  // get_alternates() prints these components, so each must lie inside the document as well.
  return span_fits(mem, url->m_ptr_scheme, url->m_len_scheme) && span_fits(mem, url->m_ptr_host, url->m_len_host) &&
         span_fits(mem, url->m_ptr_port, url->m_len_port) && span_fits(mem, url->m_ptr_path, url->m_len_path) &&
         span_fits(mem, url->m_ptr_query, url->m_len_query);
}

Errata
CacheScan::get_alternates(const char *buf, int length, bool search, ts::VersionNumber version)
{
  Errata zret;
  ink_assert(!(((intptr_t)buf) & 3)); // buf must be aligned

  char               *start     = const_cast<char *>(buf);
  RefCountObj        *block_ref = nullptr;
  swoc::MemSpan<char> doc_mem(const_cast<char *>(buf), length);

  while (length - (buf - start) > static_cast<int>(sizeof(HTTPCacheAlt))) {
    HTTPCacheAlt *a = (HTTPCacheAlt *)buf;

    if (a->m_magic == CacheAltMagic::MARSHALED || a->m_magic == CacheAltMagic::MARSHALED_WKS) {
      zret = this->unmarshal(const_cast<char *>(buf), length - (buf - start), block_ref, version);
      if (zret.length()) {
        std::cerr << zret << std::endl;
        return zret;
      } else if (!a->m_request_hdr.m_http) {
        std::cerr << "no http object found in the request header object" << std::endl;
        return zret;
      } else if (!object_fits(doc_mem, a->m_request_hdr.m_http)) {
        std::cerr << "out of bounds request header in the alternate" << std::endl;
        return zret;
      }

      auto *url = a->m_request_hdr.m_http->u.req.m_url_impl;
      if (check_url(doc_mem, url)) {
        std::string str;

        if (search) {
          swoc::bwprint(str, "{}://{}:{}/{}?{}", std::string_view(url->m_ptr_scheme, url->m_len_scheme),
                        std::string_view(url->m_ptr_host, url->m_len_host), std::string_view(url->m_ptr_port, url->m_len_port),
                        std::string_view(url->m_ptr_path, url->m_len_path), std::string_view(url->m_ptr_query, url->m_len_query));
          if (u_matcher->match(str.data())) {
            str = this->stripe->hashText + " " + str;
            std::cout << "match found " << str << std::endl;
          }
        } else {
          swoc::bwprint(str, "stripe: {} : {}://{}:{}/{}?{}", std::string_view(this->stripe->hashText),
                        std::string_view(url->m_ptr_scheme, url->m_len_scheme), std::string_view(url->m_ptr_host, url->m_len_host),
                        std::string_view(url->m_ptr_port, url->m_len_port), std::string_view(url->m_ptr_path, url->m_len_path),
                        std::string_view(url->m_ptr_query, url->m_len_query));
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
