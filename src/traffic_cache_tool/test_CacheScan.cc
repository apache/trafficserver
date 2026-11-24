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

TEST_CASE("cache inspection rejects a request URL that runs past the document", "[cache_tool]")
{
  alignas(URLImpl) char buf[sizeof(URLImpl)] = {};
  ct::CacheScan         scan(nullptr);

  auto *url     = reinterpret_cast<URLImpl *>(buf);
  url->m_type   = static_cast<uint32_t>(HdrHeapObjType::URL);
  url->m_length = sizeof(URLImpl);

  // The URL starts inside the document but its last bytes are past the end.
  swoc::MemSpan<char> truncated(buf, sizeof(URLImpl) - 1);
  CHECK_FALSE(scan.check_url(truncated, url));
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
  static constexpr char host[] = "example.com";
  // Room for the URL followed by its host string, as a marshalled header heap lays them out.
  alignas(URLImpl) char buf[sizeof(URLImpl) + sizeof(host)] = {};
  swoc::MemSpan<char>   mem(buf, sizeof(buf));
  ct::CacheScan         scan(nullptr);

  auto *url       = reinterpret_cast<URLImpl *>(buf);
  url->m_type     = static_cast<uint32_t>(HdrHeapObjType::URL);
  url->m_length   = sizeof(URLImpl);
  char *in_doc    = buf + sizeof(URLImpl);
  url->m_len_host = sizeof(host) - 1;
  std::memcpy(in_doc, host, sizeof(host));

  SECTION("a component inside the document")
  {
    url->m_ptr_host = in_doc;
    CHECK(scan.check_url(mem, url));
  }

  SECTION("a component outside the document")
  {
    url->m_ptr_host = host;
    CHECK_FALSE(scan.check_url(mem, url));
  }

  SECTION("a component that starts inside the document and runs past it")
  {
    url->m_ptr_host = in_doc + 2;
    CHECK_FALSE(scan.check_url(mem, url));
  }

  SECTION("a component with a length but no string")
  {
    url->m_ptr_host = nullptr;
    CHECK_FALSE(scan.check_url(mem, url));
  }
}
