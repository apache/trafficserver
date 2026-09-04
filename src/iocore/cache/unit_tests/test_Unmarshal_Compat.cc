/** @file

  Version compatibility tests for CacheVC::unmarshal_http_info.

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

#include "main.h"

#include "../P_CacheDoc.h"
#include "iocore/cache/CacheDefs.h"
#include "proxy/hdrs/HdrHeap.h"
#include "tscore/ink_memory.h"

#include <cstring>
#include <vector>

int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{
int constexpr ALT_MARSHAL_SIZE = HdrHeapMarshalBlocks{swoc::round_up(sizeof(HTTPCacheAlt))};
int constexpr N_INTEGRAL       = HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS;
int constexpr FRAG_COUNT       = N_INTEGRAL + 2;

// The static_assert in CacheDefs.h holds CACHE_DB_VERSION at or above {24, 2}, so this stays
// older than the current version wherever that moves. Subtracting one from
// CACHE_DB_MINOR_VERSION instead would wrap the minor when a major bump resets it to zero.
ts::VersionNumber constexpr OLDER_THAN_CURRENT{24, 1};

using FragOffset = HTTPInfo::FragOffset;

/** A Doc followed by a header block, versioned as an on-disk object would be. */
class DocBuffer
{
public:
  DocBuffer(uint8_t major, uint8_t minor, int hlen) : _storage((sizeof(Doc) + hlen + 7) / sizeof(uint64_t) + 1, 0)
  {
    Doc *d = this->doc();

    d->magic    = DOC_MAGIC;
    d->doc_type = CACHE_FRAG_TYPE_HTTP;
    d->v_major  = major;
    d->v_minor  = minor;
    d->hlen     = hlen;
    d->len      = sizeof(Doc) + hlen;
  }

  Doc *
  doc()
  {
    return reinterpret_cast<Doc *>(_storage.data());
  }

  HTTPCacheAlt *
  alt()
  {
    return reinterpret_cast<HTTPCacheAlt *>(this->doc()->hdr());
  }

private:
  std::vector<uint64_t> _storage;
};

/** Initialize the alt header the way HTTPInfo::marshal leaves it, with no header heaps. */
void
init_marshalled_alt(HTTPCacheAlt *alt, int frag_count, intptr_t frag_table_offset)
{
  alt->m_magic             = CacheAltMagic::MARSHALED;
  alt->m_writeable         = 0;
  alt->m_unmarshal_len     = -1;
  alt->m_frag_offset_count = frag_count;

  *reinterpret_cast<intptr_t *>(&alt->m_frag_offsets) = frag_table_offset;
}

/** The 24.2 layout: the whole fragment offset table follows the alt. */
DocBuffer
make_v24_2_doc(uint8_t major, uint8_t minor)
{
  DocBuffer buffer{major, minor, static_cast<int>(ALT_MARSHAL_SIZE + FRAG_COUNT * sizeof(FragOffset))};

  init_marshalled_alt(buffer.alt(), FRAG_COUNT, ALT_MARSHAL_SIZE);

  auto *table = reinterpret_cast<FragOffset *>(buffer.doc()->hdr() + ALT_MARSHAL_SIZE);

  for (int i = 0; i < FRAG_COUNT; ++i) {
    table[i] = i;
  }
  return buffer;
}

/** The 24.1 layout: the first N_INTEGRAL offsets are inline, only the rest follow. */
DocBuffer
make_v24_1_doc(uint8_t major, uint8_t minor)
{
  int constexpr extra = FRAG_COUNT - N_INTEGRAL;
  DocBuffer buffer{major, minor, static_cast<int>(ALT_MARSHAL_SIZE + extra * sizeof(FragOffset))};

  init_marshalled_alt(buffer.alt(), FRAG_COUNT, ALT_MARSHAL_SIZE);

  for (int i = 0; i < N_INTEGRAL; ++i) {
    buffer.alt()->m_integral_frag_offsets[i] = i;
  }

  auto *table = reinterpret_cast<FragOffset *>(buffer.doc()->hdr() + ALT_MARSHAL_SIZE);

  for (int i = 0; i < extra; ++i) {
    table[i] = N_INTEGRAL + i;
  }
  return buffer;
}

void
check_offsets_are_sequential(HTTPCacheAlt *alt)
{
  REQUIRE(alt->m_frag_offsets != nullptr);
  for (int i = 0; i < FRAG_COUNT; ++i) {
    CHECK(alt->m_frag_offsets[i] == static_cast<FragOffset>(i));
  }
}

/** unmarshal_v24_1 copies the table onto the heap; the caller owns it. */
void
free_frag_offsets(HTTPCacheAlt *alt)
{
  if (alt->m_frag_offsets != nullptr && alt->m_frag_offsets != alt->m_integral_frag_offsets) {
    ats_free(alt->m_frag_offsets);
    alt->m_frag_offsets = nullptr;
  }
}

} // end anonymous namespace

TEST_CASE("unmarshal_http_info selects the decoder matching the object version", "[cache][unmarshal][compat]")
{
  Ptr<IOBufferData> buf;

  SECTION("an object at the layout boundary is read with the current decoder")
  {
    DocBuffer doc{make_v24_2_doc(CACHE_DB_VERSION_HTTPINFO_V24_2._major, CACHE_DB_VERSION_HTTPINFO_V24_2._minor)};

    REQUIRE(CacheVC::unmarshal_http_info(doc.doc(), buf));
    check_offsets_are_sequential(doc.alt());
    // Only the current decoder leaves the table in the buffer; the 24.1 one copies it out,
    // so this is what says which of the two ran.
    CHECK(reinterpret_cast<char *>(doc.alt()->m_frag_offsets) == doc.doc()->hdr() + ALT_MARSHAL_SIZE);
  }

  SECTION("an object below the layout boundary is read with the 24.1 decoder")
  {
    DocBuffer doc{make_v24_1_doc(CACHE_DB_VERSION_HTTPINFO_V24_2._major, CACHE_DB_VERSION_HTTPINFO_V24_2._minor - 1)};

    // The current decoder would reject this layout outright: it expects the whole table
    // to follow the alt, and only the offsets past the integral ones are there.
    REQUIRE(CacheVC::unmarshal_http_info(doc.doc(), buf));
    check_offsets_are_sequential(doc.alt());
    CHECK(reinterpret_cast<char *>(doc.alt()->m_frag_offsets) != doc.doc()->hdr() + ALT_MARSHAL_SIZE);
    free_frag_offsets(doc.alt());
  }
}

TEST_CASE("unmarshal_http_info repairs stale accelerators only when it owns the block", "[cache][unmarshal][compat]")
{
  HTTPInfo info;

  info.create();
  build_hdrs(info, "http://www.example.com/test.html");

  int const             hlen = info.marshal_length();
  std::vector<uint64_t> marshalled(hlen / sizeof(uint64_t) + 1, 0);

  REQUIRE(info.marshal(reinterpret_cast<char *>(marshalled.data()), hlen) == hlen);

  auto load = [&](DocBuffer &doc) { memcpy(doc.doc()->hdr(), marshalled.data(), hlen); };

  // Unmarshal an untouched copy to learn where the response MIME header lands and what
  // its presence bits should be. Every copy below is byte identical, so the offset holds.
  ptrdiff_t mime_offset  = 0;
  uint64_t  correct_bits = 0;
  {
    DocBuffer         doc{CACHE_DB_MAJOR_VERSION, CACHE_DB_MINOR_VERSION, hlen};
    Ptr<IOBufferData> buf;

    load(doc);
    REQUIRE(CacheVC::unmarshal_http_info(doc.doc(), buf));
    REQUIRE(doc.alt()->m_response_hdr.valid());

    mime_offset  = reinterpret_cast<char *>(doc.alt()->m_response_hdr.m_mime) - doc.doc()->hdr();
    correct_bits = doc.alt()->m_response_hdr.m_mime->m_presence_bits;
    REQUIRE(correct_bits != 0);
  }

  auto corrupt_presence_bits = [&](DocBuffer &doc) {
    reinterpret_cast<MIMEHdrImpl *>(doc.doc()->hdr() + mime_offset)->m_presence_bits = 0;
  };

  SECTION("an older object is repaired")
  {
    DocBuffer         doc{OLDER_THAN_CURRENT._major, OLDER_THAN_CURRENT._minor, hlen};
    Ptr<IOBufferData> buf;

    load(doc);
    corrupt_presence_bits(doc);
    REQUIRE(CacheVC::unmarshal_http_info(doc.doc(), buf));
    CHECK(doc.alt()->m_response_hdr.m_mime->m_presence_bits == correct_bits);
  }

  SECTION("a current object is left alone")
  {
    DocBuffer         doc{CACHE_DB_MAJOR_VERSION, CACHE_DB_MINOR_VERSION, hlen};
    Ptr<IOBufferData> buf;

    load(doc);
    corrupt_presence_bits(doc);
    REQUIRE(CacheVC::unmarshal_http_info(doc.doc(), buf));
    CHECK(doc.alt()->m_response_hdr.m_mime->m_presence_bits == 0);
  }

  SECTION("an already unmarshalled block is left alone even for an older object")
  {
    // The block may be shared with other readers at this point, so the repair must not
    // run a second time. This is what the MARSHALED check buys.
    DocBuffer         doc{OLDER_THAN_CURRENT._major, OLDER_THAN_CURRENT._minor, hlen};
    Ptr<IOBufferData> buf;

    load(doc);
    REQUIRE(CacheVC::unmarshal_http_info(doc.doc(), buf));
    REQUIRE(doc.alt()->m_magic == CacheAltMagic::ALIVE);

    doc.alt()->m_response_hdr.m_mime->m_presence_bits = 0;
    REQUIRE(CacheVC::unmarshal_http_info(doc.doc(), buf));
    CHECK(doc.alt()->m_response_hdr.m_mime->m_presence_bits == 0);
  }

  info.destroy();
}
