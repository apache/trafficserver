/** @file

  Backwards compatibility support for the cache.

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

#ifndef _P_CACHE_BC_H__
#define _P_CACHE_BC_H__

namespace cache_bc
{
/* This looks kind of dumb, but I think it's useful. We import external structure
   dependencies in to this namespace so we can at least (1) notice them and
   (2) change them if the current structure changes.
*/

typedef HTTPHdr HTTPHdr_v21;
typedef HdrHeap HdrHeap_v23;
typedef CryptoHash CryptoHash_v23;
typedef HTTPCacheAlt HTTPCacheAlt_v23;

/** Cache backwards compatibility structure - the fragment table.
    This is copied from @c HTTPCacheAlt in @c HTTP.h.
*/
struct HTTPCacheFragmentTable {
  /// # of fragment offsets in this alternate.
  /// @note This is one less than the number of fragments.
  int m_frag_offset_count;
  /// Type of offset for a fragment.
  typedef uint64_t FragOffset;
  /// Table of fragment offsets.
  /// @note The offsets are forward looking so that frag[0] is the
  /// first byte past the end of fragment 0 which is also the first
  /// byte of fragment 1. For this reason there is no fragment offset
  /// for the last fragment.
  FragOffset *m_frag_offsets;
  /// # of fragment offsets built in to object.
  static int const N_INTEGRAL_FRAG_OFFSETS = 4;
  /// Integral fragment offset table.
  FragOffset m_integral_frag_offsets[N_INTEGRAL_FRAG_OFFSETS];
};

// From before moving the fragment table to the alternate.
struct HTTPCacheAlt_v21 {
  uint32_t m_magic;

  int32_t m_writeable;
  int32_t m_unmarshal_len;

  int32_t m_id;
  int32_t m_rid;

  int32_t m_object_key[4];
  int32_t m_object_size[2];

  HTTPHdr_v21 m_request_hdr;
  HTTPHdr_v21 m_response_hdr;

  time_t m_request_sent_time;
  time_t m_response_received_time;

  RefCountObj *m_ext_buffer;

  // The following methods were added for BC support.
  // Checks itself to verify that it is unmarshalled and v21 format.
  bool
  is_unmarshalled_format() const
  {
    return CACHE_ALT_MAGIC_MARSHALED == m_magic && reinterpret_cast<intptr_t>(m_request_hdr.m_heap) == sizeof(*this);
  }
};

/// Really just a namespace, doesn't depend on any of the members.
struct HTTPInfo_v21 {
  typedef uint64_t FragOffset;
  /// Version upgrade methods
  /// @a src , @a dst , and @a n are updated upon return.
  /// @a n is the space in @a dst remaining.
  /// @return @c false if something went wrong.
  static bool copy_and_upgrade_unmarshalled_to_v23(char *&dst, char *&src, size_t &length, int n_frags, FragOffset *frag_offsets);
  /// The size of the marshalled data of a marshalled alternate header.
  static size_t marshalled_length(void *data);
};

/// Pre version 24.
struct Doc_v23 {
  uint32_t magic;           // DOC_MAGIC
  uint32_t len;             // length of this segment (including hlen, flen & sizeof(Doc), unrounded)
  uint64_t total_len;       // total length of document
  CryptoHash_v23 first_key; ///< first key in object.
  CryptoHash_v23 key;       ///< Key for this doc.
  uint32_t hlen;            ///< Length of this header.
  uint32_t doc_type : 8;    ///< Doc type - indicates the format of this structure and its content.
  uint32_t _flen : 24;      ///< Fragment table length.
  uint32_t sync_serial;
  uint32_t write_serial;
  uint32_t pinned; // pinned until
  uint32_t checksum;

  char *hdr();
  char *data();
  size_t data_len();
};

char *
Doc_v23::data()
{
  return reinterpret_cast<char *>(this) + sizeof(Doc_v23) + _flen + hlen;
}
size_t
Doc_v23::data_len()
{
  return len - sizeof(Doc_v23) - hlen;
}
char *
Doc_v23::hdr()
{
  return reinterpret_cast<char *>(this) + sizeof(Doc_v23);
}

} // namespace cache_bc

#endif /* _P_CACHE_BC_H__ */
