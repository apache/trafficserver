/** @file

  Cache inspection tool alternate scanning tests.

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
#include <catch2/catch_test_macros.hpp>

#include <cstring>

TEST_CASE("cache inspection rejects an alternate without a request URL", "[cache_tool]")
{
  // A damaged or partly written document can carry a request header whose URL
  // pointer is null once unmarshalled.
  char                buf[256] = {};
  swoc::MemSpan<char> mem(buf, sizeof(buf));
  ct::CacheScan       scan(nullptr);

  CHECK_FALSE(scan.check_url(mem, nullptr));
}

TEST_CASE("cache inspection accepts a request URL inside the document", "[cache_tool]")
{
  alignas(URLImpl) char buf[2 * sizeof(URLImpl)] = {};
  swoc::MemSpan<char>   mem(buf, sizeof(buf));
  ct::CacheScan         scan(nullptr);

  auto *url     = reinterpret_cast<URLImpl *>(buf);
  url->m_type   = static_cast<uint32_t>(HdrHeapObjType::URL);
  url->m_length = sizeof(URLImpl);

  CHECK(scan.check_url(mem, url));

  // The same URL is rejected once it lies outside the bytes read.
  swoc::MemSpan<char> elsewhere(buf + sizeof(URLImpl), sizeof(URLImpl));
  CHECK_FALSE(scan.check_url(elsewhere, url));
}

TEST_CASE("cache inspection checks the whole request URL object", "[cache_tool]")
{
  // Backed by three objects' worth of memory so that every pointer below, even past the span, is inside a real array.
  alignas(URLImpl) char backing[3 * sizeof(URLImpl)] = {};
  ct::CacheScan         scan(nullptr);

  auto *url     = reinterpret_cast<URLImpl *>(backing);
  url->m_type   = static_cast<uint32_t>(HdrHeapObjType::URL);
  url->m_length = sizeof(URLImpl);

  SECTION("a URL that exactly fills the document")
  {
    swoc::MemSpan<char> exact(backing, sizeof(URLImpl));
    CHECK(scan.check_url(exact, url));
  }

  SECTION("a URL that starts inside the document but whose last byte is past the end")
  {
    swoc::MemSpan<char> short_by_one(backing, sizeof(URLImpl) - 1);
    CHECK_FALSE(scan.check_url(short_by_one, url));
  }

  SECTION("a URL past the end of the document")
  {
    auto *later = reinterpret_cast<URLImpl *>(backing + 2 * sizeof(URLImpl));
    std::memcpy(later, url, sizeof(URLImpl));
    swoc::MemSpan<char> first(backing, sizeof(URLImpl));
    CHECK_FALSE(scan.check_url(first, later));
  }
}

TEST_CASE("cache inspection rejects a misaligned request URL", "[cache_tool]")
{
  alignas(URLImpl) char src[sizeof(URLImpl)] = {};
  auto                 *proto                = reinterpret_cast<URLImpl *>(src);
  proto->m_type                              = static_cast<uint32_t>(HdrHeapObjType::URL);
  proto->m_length                            = sizeof(URLImpl);

  // Copied one byte in, so the object is whole but cannot be read through a URLImpl pointer.
  alignas(URLImpl) char buf[2 * sizeof(URLImpl)] = {};
  std::memcpy(buf + 1, src, sizeof(URLImpl));
  swoc::MemSpan<char> mem(buf, sizeof(buf));
  ct::CacheScan       scan(nullptr);

  CHECK_FALSE(scan.check_url(mem, reinterpret_cast<URLImpl *>(buf + 1)));
}

TEST_CASE("cache inspection checks every URL component it prints", "[cache_tool]")
{
  struct Component {
    char const *name;
    char const *URLImpl::*ptr;
    uint16_t URLImpl::*len;
  };
  static constexpr Component printed[] = {
    {"scheme", &URLImpl::m_ptr_scheme, &URLImpl::m_len_scheme},
    {"host",   &URLImpl::m_ptr_host,   &URLImpl::m_len_host  },
    {"port",   &URLImpl::m_ptr_port,   &URLImpl::m_len_port  },
    {"path",   &URLImpl::m_ptr_path,   &URLImpl::m_len_path  },
    {"query",  &URLImpl::m_ptr_query,  &URLImpl::m_len_query },
  };
  static constexpr char outside[] = "abcd";
  static constexpr int  STR_ROOM  = 16;

  // The document holds the URL followed by STR_ROOM bytes of strings, as a marshalled header heap lays them out. The
  // backing array runs on past the document so that pointers past its end are still inside a real array.
  alignas(URLImpl) char backing[sizeof(URLImpl) + 2 * STR_ROOM] = {};
  swoc::MemSpan<char>   mem(backing, sizeof(URLImpl) + STR_ROOM);
  char                 *strings = backing + sizeof(URLImpl);
  char                 *doc_end = strings + STR_ROOM;
  ct::CacheScan         scan(nullptr);

  auto *url     = reinterpret_cast<URLImpl *>(backing);
  url->m_type   = static_cast<uint32_t>(HdrHeapObjType::URL);
  url->m_length = sizeof(URLImpl);

  // Whether check_url() accepts the URL with only component c set to the given string.
  auto accepts = [&](Component const &c, char const *ptr, uint16_t len) {
    for (auto const &other : printed) {
      url->*other.ptr = nullptr;
      url->*other.len = 0;
    }
    url->*c.ptr = ptr;
    url->*c.len = len;
    return scan.check_url(mem, url);
  };

  for (auto const &c : printed) {
    CAPTURE(c.name);
    CHECK(accepts(c, strings, 4));
    CHECK(accepts(c, doc_end - 4, 4));       // ends on the document's last byte
    CHECK(accepts(c, doc_end + 4, 0));       // an empty string is never read, wherever it points
    CHECK_FALSE(accepts(c, outside, 4));     // somewhere else entirely
    CHECK_FALSE(accepts(c, doc_end - 3, 4)); // starts inside, runs one byte past the end
    CHECK_FALSE(accepts(c, doc_end, 1));     // starts at the end
    CHECK_FALSE(accepts(c, doc_end + 4, 4)); // past the end
    CHECK_FALSE(accepts(c, nullptr, 4));     // a length but no string
  }
}

TEST_CASE("cache inspection rejects a header heap that is not inside the alternate", "[cache_tool]")
{
  // An alternate with room for nothing past its own fixed part except a few bytes at the end.
  alignas(HTTPCacheAlt) char buf[sizeof(HTTPCacheAlt) + 64] = {};
  ct::CacheScan              scan(nullptr);

  auto *alt                = reinterpret_cast<HTTPCacheAlt *>(buf);
  alt->m_magic             = CacheAltMagic::MARSHALED;
  alt->m_writeable         = 0;
  alt->m_unmarshal_len     = -1;
  alt->m_frag_offset_count = 0;

  // A heap header that starts in the last eight bytes, so its magic and size can be read but the rest of it cannot.
  intptr_t const late = sizeof(buf) - 8;
  auto const     magic{HdrBufMagic::MARSHALED};
  uint32_t const size{4096};
  std::memcpy(buf + late, &magic, sizeof(magic));
  std::memcpy(buf + late + sizeof(magic), &size, sizeof(size));

  SECTION("a request heap that runs past the end")
  {
    alt->m_request_hdr.m_heap = reinterpret_cast<HdrHeap *>(late);
    CHECK(scan.unmarshal(buf, sizeof(buf), nullptr, ts::VersionNumber{}).length() > 0);
  }

  SECTION("a request heap before the alternate")
  {
    alt->m_request_hdr.m_heap = reinterpret_cast<HdrHeap *>(intptr_t{-64});
    CHECK(scan.unmarshal(buf, sizeof(buf), nullptr, ts::VersionNumber{}).length() > 0);
  }

  SECTION("a response heap that runs past the end")
  {
    alt->m_response_hdr.m_heap = reinterpret_cast<HdrHeap *>(late);
    CHECK(scan.unmarshal(buf, sizeof(buf), nullptr, ts::VersionNumber{}).length() > 0);
  }
}
