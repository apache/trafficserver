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

#pragma once

#include <sys/time.h>
#include <string_view>
#include <string>

using namespace std::literals;

#include "tscore/ink_assert.h"
#include "tscore/ink_apidefs.h"
#include "tscore/ink_string++.h"
#include "tscore/ParseRules.h"
#include "proxy/hdrs/HdrHeap.h"
#include "proxy/hdrs/HdrToken.h"

#include "swoc/TextView.h"

/***********************************************************************
 *                                                                     *
 *                              Defines                                *
 *                                                                     *
 ***********************************************************************/

enum class ParseResult {
  ERROR = -1,
  DONE  = 0,
  CONT  = 1,
  OK    = 3, // This is only used internally in mime_parser_parse and not returned to the user
};

enum {
  UNDEFINED_COUNT = -1,
};

/// Parsing state.
enum class MimeParseState {
  BEFORE,   ///< Before a field.
  FOUND_CR, ///< Before a field, found a CR.
  INSIDE,   ///< Inside a field.
  AFTER,    ///< After a field.
};

/***********************************************************************
 *                                                                     *
 *                              Assertions                             *
 *                                                                     *
 ***********************************************************************/

#ifdef ENABLE_MIME_SANITY_CHECK
#define MIME_HDR_SANITY_CHECK mime_hdr_sanity_check
#else
#define MIME_HDR_SANITY_CHECK (void)
#endif

#define MIME_FIELD_SLOT_READINESS_EMPTY    0
#define MIME_FIELD_SLOT_READINESS_DETACHED 1
#define MIME_FIELD_SLOT_READINESS_LIVE     2
#define MIME_FIELD_SLOT_READINESS_DELETED  3

#define MIME_FIELD_SLOT_FLAGS_DUP_HEAD (1 << 0)
#define MIME_FIELD_SLOT_FLAGS_COOKED   (1 << 1)

#define MIME_FIELD_BLOCK_SLOTS 16

#define MIME_FIELD_SLOTNUM_BITS    4
#define MIME_FIELD_SLOTNUM_MASK    ((1 << MIME_FIELD_SLOTNUM_BITS) - 1)
#define MIME_FIELD_SLOTNUM_MAX     (MIME_FIELD_SLOTNUM_MASK - 1)
#define MIME_FIELD_SLOTNUM_UNKNOWN MIME_FIELD_SLOTNUM_MAX

/***********************************************************************
 *                                                                     *
 *                    MIMEField & MIMEFieldBlockImpl                   *
 *                                                                     *
 ***********************************************************************/

struct MIMEHdrImpl;

struct MIMEField {
  const char *m_ptr_name;                   // 4
  const char *m_ptr_value;                  // 4
  MIMEField  *m_next_dup;                   // 4
  int16_t     m_wks_idx;                    // 2
  uint16_t    m_len_name;                   // 2
  uint32_t    m_len_value             : 24; // 3
  uint8_t     m_n_v_raw_printable     : 1;  // 1/8
  uint8_t     m_n_v_raw_printable_pad : 3;  // 3/8
  uint8_t     m_readiness             : 2;  // 2/8
  uint8_t     m_flags                 : 2;  // 2/8

  bool
  is_dup_head() const
  {
    return (m_flags & MIME_FIELD_SLOT_FLAGS_DUP_HEAD);
  }

  bool
  is_cooked() const
  {
    return (m_flags & MIME_FIELD_SLOT_FLAGS_COOKED) ? true : false;
  }

  bool
  is_live() const
  {
    return (m_readiness == MIME_FIELD_SLOT_READINESS_LIVE);
  }

  bool
  is_detached() const
  {
    return (m_readiness == MIME_FIELD_SLOT_READINESS_DETACHED);
  }

  bool
  supports_commas() const
  {
    if (m_wks_idx >= 0) {
      return (hdrtoken_index_to_flags(m_wks_idx) & HdrTokenInfoFlags::COMMAS) != HdrTokenInfoFlags::NONE;
    }
    return true; // by default, assume supports commas
  }

  /// @return The name of @a this field.
  std::string_view name_get() const;

  /** Find the index of the value in the multi-value field.

     If @a value is one of the values in this field return the
     0 based index of it in the list of values. If the field is
     not multivalued the index will always be zero if found.
     Otherwise return -1 if the @a value is not found.

     @note The most common use of this is to check for the presence of a specific
     value in a comma enabled field.

     @return The index of @a value.
  */
  int value_get_index(std::string_view value) const;

  /// @return The value of @a this field.
  std::string_view value_get() const;

  int32_t  value_get_int() const;
  uint32_t value_get_uint() const;
  int64_t  value_get_int64() const;
  time_t   value_get_date() const;
  int      value_get_comma_list(StrList *list) const;

  void name_set(HdrHeap *heap, MIMEHdrImpl *mh, std::string_view name);
  bool name_is_valid(uint32_t invalid_char_bits = is_control_BIT) const;

  void value_set(HdrHeap *heap, MIMEHdrImpl *mh, std::string_view value);
  void value_set_int(HdrHeap *heap, MIMEHdrImpl *mh, int32_t value);
  void value_set_uint(HdrHeap *heap, MIMEHdrImpl *mh, uint32_t value);
  void value_set_int64(HdrHeap *heap, MIMEHdrImpl *mh, int64_t value);
  void value_set_date(HdrHeap *heap, MIMEHdrImpl *mh, time_t value);
  void value_clear(HdrHeap *heap, MIMEHdrImpl *mh);
  // MIME standard separator ',' is used as the default value
  // Other separators (e.g. ';' in Set-cookie/Cookie) are also possible
  void value_append(HdrHeap *heap, MIMEHdrImpl *mh, std::string_view value, bool prepend_comma = false, const char separator = ',');
  bool value_is_valid(uint32_t invalid_char_bits = is_control_BIT) const;
  int  has_dups() const;
};

struct MIMEFieldBlockImpl : public HdrHeapObjImpl {
  // HdrHeapObjImpl is 4 bytes
  uint32_t            m_freetop;
  MIMEFieldBlockImpl *m_next;
  MIMEField           m_field_slots[MIME_FIELD_BLOCK_SLOTS];
  // mime_hdr_copy_onto assumes that m_field_slots is last --
  // don't add any new fields after it.

  // Marshaling Functions
  int    marshal(MarshalXlate *ptr_xlate, int num_ptr, MarshalXlate *str_xlate, int num_str);
  void   unmarshal(intptr_t offset);
  void   move_strings(HdrStrHeap *new_heap);
  size_t strings_length();
  bool   contains(const MIMEField *field);

  // Sanity Check Functions
  void check_strings(HeapCheck *heaps, int num_heaps);
};

/***********************************************************************
 *                                                                     *
 *                              MIMECooked                             *
 *                                                                     *
 ***********************************************************************/

enum MIMECookedMask {
  MIME_COOKED_MASK_CC_MAX_AGE              = (1 << 0),
  MIME_COOKED_MASK_CC_NO_CACHE             = (1 << 1),
  MIME_COOKED_MASK_CC_NO_STORE             = (1 << 2),
  MIME_COOKED_MASK_CC_NO_TRANSFORM         = (1 << 3),
  MIME_COOKED_MASK_CC_MAX_STALE            = (1 << 4),
  MIME_COOKED_MASK_CC_MIN_FRESH            = (1 << 5),
  MIME_COOKED_MASK_CC_ONLY_IF_CACHED       = (1 << 6),
  MIME_COOKED_MASK_CC_PUBLIC               = (1 << 7),
  MIME_COOKED_MASK_CC_PRIVATE              = (1 << 8),
  MIME_COOKED_MASK_CC_MUST_REVALIDATE      = (1 << 9),
  MIME_COOKED_MASK_CC_PROXY_REVALIDATE     = (1 << 10),
  MIME_COOKED_MASK_CC_S_MAXAGE             = (1 << 11),
  MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE = (1 << 12),
  MIME_COOKED_MASK_CC_EXTENSION            = (1 << 13)
};

struct MIMECookedCacheControl {
  uint32_t m_mask;
  int32_t  m_secs_max_age;
  int32_t  m_secs_s_maxage;
  int32_t  m_secs_max_stale;
  int32_t  m_secs_min_fresh;
};

struct MIMECookedPragma {
  bool m_no_cache;
};

struct MIMECooked {
  MIMECookedCacheControl m_cache_control;
  MIMECookedPragma       m_pragma;
};

/***********************************************************************
 *                                                                     *
 *                                MIMEHdr                              *
 *                                                                     *
 ***********************************************************************/

struct MIMEHdrImpl : public HdrHeapObjImpl {
  /** Iterator over fields in the header.
   * This iterator should be stable over field deletes, but not insertions.
   */
  class iterator
  {
    using self_type = iterator; ///< Self reference types.

  public:
    iterator() = default;

    // STL iterator compliance types.
    using difference_type   = void;
    using value_type        = MIMEField;
    using pointer           = value_type *;
    using reference         = value_type &;
    using iterator_category = std::forward_iterator_tag;

    pointer   operator->();
    reference operator*();

    self_type &operator++();
    self_type  operator++(int);

    bool operator==(self_type const &that) const;
    bool operator!=(self_type const &that) const;

  protected:
    MIMEFieldBlockImpl *_block = nullptr; ///< Current block.
    unsigned            _slot  = 0;       ///< Slot in @a _block

    /** Internal constructor.
     *
     * @param block Block containing current field.
     * @param slot Index of current field.
     */
    iterator(MIMEFieldBlockImpl *block, unsigned slot) : _block(block), _slot(slot) { this->step(); }

    /** Move to a valid (live) slot.
     *
     * This enforces the invariant that the iterator is exactly one of
     * 1. referencing a valid slot
     * 2. equal to the @c end iterator
     *
     * Therefore if called when the iterator is in state (1) the iterator is unchanged.
     *
     * @return @a this
     */
    self_type &step();

    friend class MIMEHdr;
    friend struct MIMEHdrImpl;
  };

  // HdrHeapObjImpl is 4 bytes, so this will result in 4 bytes padding
  uint64_t m_presence_bits;
  uint32_t m_slot_accelerators[4];

  MIMECooked m_cooked_stuff;

  MIMEFieldBlockImpl *m_fblock_list_tail;
  MIMEFieldBlockImpl  m_first_fblock; // 1 block inline
  // mime_hdr_copy_onto assumes that m_first_fblock is last --
  // don't add any new fields after it.

  // Marshaling Functions
  int    marshal(MarshalXlate *ptr_xlate, int num_ptr, MarshalXlate *str_xlate, int num_str);
  void   unmarshal(intptr_t offset);
  void   move_strings(HdrStrHeap *new_heap);
  size_t strings_length();

  // Sanity Check Functions
  void check_strings(HeapCheck *heaps, int num_heaps);

  // Cooked values
  void recompute_cooked_stuff(MIMEField *changing_field_or_null = nullptr);
  void recompute_accelerators_and_presence_bits();

  // Utility
  /// Iterator for first field.
  iterator begin();
  /// Iterator past last field.
  iterator end();

  /** Find a field by address.
   *
   * @param field Target to find.
   * @return An iterator referencing @a field if it is in the header, an empty iterator
   * if not.
   */
  iterator find(MIMEField const *field);
};

inline auto
MIMEHdrImpl::begin() -> iterator
{
  return iterator(&m_first_fblock, 0);
}

inline auto
MIMEHdrImpl::end() -> iterator
{
  return {}; // default constructed iterator.
}

inline auto
MIMEHdrImpl::iterator::step() -> self_type &
{
  while (_block) {
    for (auto limit = _block->m_freetop; _slot < limit; ++_slot) {
      if (_block->m_field_slots[_slot].is_live()) {
        return *this;
      }
    }
    _block = _block->m_next;
    _slot  = 0;
  }
  return *this;
}

inline auto
MIMEHdrImpl::iterator::operator*() -> reference
{
  return _block->m_field_slots[_slot];
}

inline auto
MIMEHdrImpl::iterator::operator->() -> pointer
{
  return &(_block->m_field_slots[_slot]);
}

inline bool
MIMEHdrImpl::iterator::operator==(const self_type &that) const
{
  return _block == that._block && _slot == that._slot;
}

inline bool
MIMEHdrImpl::iterator::operator!=(const self_type &that) const
{
  return _block != that._block || _slot != that._slot;
}

inline auto
MIMEHdrImpl::iterator::operator++() -> self_type &
{
  if (_block) {
    ++_slot;
    this->step();
  }
  return *this;
}

inline auto
MIMEHdrImpl::iterator::operator++(int) -> self_type
{
  self_type zret{*this};
  ++*this;
  return zret;
}

/***********************************************************************
 *                                                                     *
 *                                Parser                               *
 *                                                                     *
 ***********************************************************************/

/** A pre-parser used to extract MIME "lines" from raw input for further parsing.
 *
 * This maintains an internal line buffer which is used to keeping content between calls
 * when the parse has not yet completed.
 *
 */
struct MIMEScanner {
  using self_type = MIMEScanner; ///< Self reference type.
public:
  /// Type of input scanning.
  enum class ScanType {
    LINE  = 0, ///< Scan a single line.
    FIELD = 1, ///< Scan with line folding enabled.
  };

  void init();  ///< Pseudo-constructor required by proxy allocation.
  void clear(); ///< Pseudo-destructor required by proxy allocation.

  /// @return The size of the internal line buffer.
  size_t get_buffered_line_size() const;

  /** Scan @a input for MIME data delimited by CR/LF end of line markers.
   *
   * @param input [in,out] Text to scan.
   * @param output [out] Parsed text from @a input, if any.
   * @param output_shares_input [out] Whether @a output is in @a input.
   * @param eof_p [in] The source for @a input is done, no more data will ever be available.
   * @param scan_type [in] Whether to check for line folding.
   * @return The result of scanning.
   *
   * @a input is updated to remove text that was scanned. @a output is updated to be a view of the
   * scanned @a input. This is separate because @a output may be a view of @a input or a view of the
   * internal line buffer. Which of these cases obtains is returned in @a output_shares_input. This
   * is @c true if @a output is a view of @a input, and @c false if @a output is a view of the
   * internal buffer, but is only set if the result is not @c ParseResult::CONT (that is, it is not
   * set until scanning has completed). If @a scan_type is @c FIELD then folded lines are
   * accumulated in to a single line stored in the internal buffer. Otherwise the scanning
   * terminates at the first CR/LF.
   */
  ParseResult get(swoc::TextView &input, swoc::TextView &output, bool &output_shares_input, bool eof_p, ScanType scan_type);

protected:
  /** Append @a text to the internal buffer.
   *
   * @param text Text to append.
   * @return @a this
   *
   * A copy of @a text is appended to the internal line buffer.
   */
  self_type &append(swoc::TextView text);

  static constexpr MimeParseState INITIAL_PARSE_STATE = MimeParseState::BEFORE;
  std::string                     m_line;                       ///< Internally buffered line data for field coalescence.
  MimeParseState                  m_state{INITIAL_PARSE_STATE}; ///< Parsing machine state.
};

inline size_t
MIMEScanner::get_buffered_line_size() const
{
  return m_line.size();
}

inline void
MIMEScanner::clear()
{
  std::string empty;        // GAH! @c swap isn't defined to take r-value reference!
  std::swap(m_line, empty); // make sure the memory is released.
  m_state = INITIAL_PARSE_STATE;
}

struct MIMEParser {
  MIMEScanner m_scanner;
  int32_t     m_field;
  int         m_field_flags;
  int         m_value;
};

/***********************************************************************
 *                                                                     *
 *                                SDK                                  *
 *                                                                     *
 ***********************************************************************/

/********************************************/
/* SDK Handles to Fields are special structures */
/********************************************/
struct MIMEFieldSDKHandle : public HdrHeapObjImpl {
  MIMEHdrImpl *mh;
  MIMEField   *field_ptr;
};

/***********************************************************************
 *                                                                     *
 *                     Well-Known Field Name Tokens                    *
 *                                                                     *
 ***********************************************************************/

extern c_str_view MIME_FIELD_ACCEPT;
extern c_str_view MIME_FIELD_ACCEPT_CHARSET;
extern c_str_view MIME_FIELD_ACCEPT_ENCODING;
extern c_str_view MIME_FIELD_ACCEPT_LANGUAGE;
extern c_str_view MIME_FIELD_ACCEPT_RANGES;
extern c_str_view MIME_FIELD_AGE;
extern c_str_view MIME_FIELD_ALLOW;
extern c_str_view MIME_FIELD_APPROVED;
extern c_str_view MIME_FIELD_AUTHORIZATION;
extern c_str_view MIME_FIELD_BYTES;
extern c_str_view MIME_FIELD_CACHE_CONTROL;
extern c_str_view MIME_FIELD_CLIENT_IP;
extern c_str_view MIME_FIELD_CONNECTION;
extern c_str_view MIME_FIELD_CONTENT_BASE;
extern c_str_view MIME_FIELD_CONTENT_ENCODING;
extern c_str_view MIME_FIELD_CONTENT_LANGUAGE;
extern c_str_view MIME_FIELD_CONTENT_LENGTH;
extern c_str_view MIME_FIELD_CONTENT_LOCATION;
extern c_str_view MIME_FIELD_CONTENT_MD5;
extern c_str_view MIME_FIELD_CONTENT_RANGE;
extern c_str_view MIME_FIELD_CONTENT_TYPE;
extern c_str_view MIME_FIELD_CONTROL;
extern c_str_view MIME_FIELD_COOKIE;
extern c_str_view MIME_FIELD_DATE;
extern c_str_view MIME_FIELD_DISTRIBUTION;
extern c_str_view MIME_FIELD_ETAG;
extern c_str_view MIME_FIELD_EXPECT;
extern c_str_view MIME_FIELD_EXPIRES;
extern c_str_view MIME_FIELD_FOLLOWUP_TO;
extern c_str_view MIME_FIELD_FROM;
extern c_str_view MIME_FIELD_HOST;
extern c_str_view MIME_FIELD_IF_MATCH;
extern c_str_view MIME_FIELD_IF_MODIFIED_SINCE;
extern c_str_view MIME_FIELD_IF_NONE_MATCH;
extern c_str_view MIME_FIELD_IF_RANGE;
extern c_str_view MIME_FIELD_IF_UNMODIFIED_SINCE;
extern c_str_view MIME_FIELD_KEEP_ALIVE;
extern c_str_view MIME_FIELD_KEYWORDS;
extern c_str_view MIME_FIELD_LAST_MODIFIED;
extern c_str_view MIME_FIELD_LINES;
extern c_str_view MIME_FIELD_LOCATION;
extern c_str_view MIME_FIELD_MAX_FORWARDS;
extern c_str_view MIME_FIELD_MESSAGE_ID;
extern c_str_view MIME_FIELD_NEWSGROUPS;
extern c_str_view MIME_FIELD_ORGANIZATION;
extern c_str_view MIME_FIELD_PATH;
extern c_str_view MIME_FIELD_PRAGMA;
extern c_str_view MIME_FIELD_PROXY_AUTHENTICATE;
extern c_str_view MIME_FIELD_PROXY_AUTHORIZATION;
extern c_str_view MIME_FIELD_PROXY_CONNECTION;
extern c_str_view MIME_FIELD_PUBLIC;
extern c_str_view MIME_FIELD_RANGE;
extern c_str_view MIME_FIELD_REFERENCES;
extern c_str_view MIME_FIELD_REFERER;
extern c_str_view MIME_FIELD_REPLY_TO;
extern c_str_view MIME_FIELD_RETRY_AFTER;
extern c_str_view MIME_FIELD_SENDER;
extern c_str_view MIME_FIELD_SERVER;
extern c_str_view MIME_FIELD_SET_COOKIE;
extern c_str_view MIME_FIELD_STRICT_TRANSPORT_SECURITY;
extern c_str_view MIME_FIELD_SUBJECT;
extern c_str_view MIME_FIELD_SUMMARY;
extern c_str_view MIME_FIELD_TE;
extern c_str_view MIME_FIELD_TRANSFER_ENCODING;
extern c_str_view MIME_FIELD_UPGRADE;
extern c_str_view MIME_FIELD_USER_AGENT;
extern c_str_view MIME_FIELD_VARY;
extern c_str_view MIME_FIELD_VIA;
extern c_str_view MIME_FIELD_WARNING;
extern c_str_view MIME_FIELD_WWW_AUTHENTICATE;
extern c_str_view MIME_FIELD_XREF;
extern c_str_view MIME_FIELD_ATS_INTERNAL;
extern c_str_view MIME_FIELD_X_ID;
extern c_str_view MIME_FIELD_X_FORWARDED_FOR;
extern c_str_view MIME_FIELD_FORWARDED;
extern c_str_view MIME_FIELD_SEC_WEBSOCKET_KEY;
extern c_str_view MIME_FIELD_SEC_WEBSOCKET_VERSION;
extern c_str_view MIME_FIELD_HTTP2_SETTINGS;
extern c_str_view MIME_FIELD_EARLY_DATA;

extern c_str_view MIME_VALUE_BYTES;
extern c_str_view MIME_VALUE_CHUNKED;
extern c_str_view MIME_VALUE_CLOSE;
extern c_str_view MIME_VALUE_COMPRESS;
extern c_str_view MIME_VALUE_DEFLATE;
extern c_str_view MIME_VALUE_GZIP;
extern c_str_view MIME_VALUE_BROTLI;
extern c_str_view MIME_VALUE_IDENTITY;
extern c_str_view MIME_VALUE_KEEP_ALIVE;
extern c_str_view MIME_VALUE_MAX_AGE;
extern c_str_view MIME_VALUE_MAX_STALE;
extern c_str_view MIME_VALUE_MIN_FRESH;
extern c_str_view MIME_VALUE_MUST_REVALIDATE;
extern c_str_view MIME_VALUE_NONE;
extern c_str_view MIME_VALUE_NO_CACHE;
extern c_str_view MIME_VALUE_NO_STORE;
extern c_str_view MIME_VALUE_NO_TRANSFORM;
extern c_str_view MIME_VALUE_ONLY_IF_CACHED;
extern c_str_view MIME_VALUE_PRIVATE;
extern c_str_view MIME_VALUE_PROXY_REVALIDATE;
extern c_str_view MIME_VALUE_PUBLIC;
extern c_str_view MIME_VALUE_S_MAXAGE;
extern c_str_view MIME_VALUE_NEED_REVALIDATE_ONCE;
extern c_str_view MIME_VALUE_WEBSOCKET;
extern c_str_view MIME_VALUE_H2C;

extern int MIME_WKSIDX_ACCEPT;
extern int MIME_WKSIDX_ACCEPT_CHARSET;
extern int MIME_WKSIDX_ACCEPT_ENCODING;
extern int MIME_WKSIDX_ACCEPT_LANGUAGE;
extern int MIME_WKSIDX_ACCEPT_RANGES;
extern int MIME_WKSIDX_AGE;
extern int MIME_WKSIDX_ALLOW;
extern int MIME_WKSIDX_APPROVED;
extern int MIME_WKSIDX_AUTHORIZATION;
extern int MIME_WKSIDX_BYTES;
extern int MIME_WKSIDX_CACHE_CONTROL;
extern int MIME_WKSIDX_CLIENT_IP;
extern int MIME_WKSIDX_CONNECTION;
extern int MIME_WKSIDX_CONTENT_BASE;
extern int MIME_WKSIDX_CONTENT_ENCODING;
extern int MIME_WKSIDX_CONTENT_LANGUAGE;
extern int MIME_WKSIDX_CONTENT_LENGTH;
extern int MIME_WKSIDX_CONTENT_LOCATION;
extern int MIME_WKSIDX_CONTENT_MD5;
extern int MIME_WKSIDX_CONTENT_RANGE;
extern int MIME_WKSIDX_CONTENT_TYPE;
extern int MIME_WKSIDX_CONTROL;
extern int MIME_WKSIDX_COOKIE;
extern int MIME_WKSIDX_DATE;
extern int MIME_WKSIDX_DISTRIBUTION;
extern int MIME_WKSIDX_ETAG;
extern int MIME_WKSIDX_EXPECT;
extern int MIME_WKSIDX_EXPIRES;
extern int MIME_WKSIDX_FOLLOWUP_TO;
extern int MIME_WKSIDX_FROM;
extern int MIME_WKSIDX_HOST;
extern int MIME_WKSIDX_IF_MATCH;
extern int MIME_WKSIDX_IF_MODIFIED_SINCE;
extern int MIME_WKSIDX_IF_NONE_MATCH;
extern int MIME_WKSIDX_IF_RANGE;
extern int MIME_WKSIDX_IF_UNMODIFIED_SINCE;
extern int MIME_WKSIDX_KEEP_ALIVE;
extern int MIME_WKSIDX_KEYWORDS;
extern int MIME_WKSIDX_LAST_MODIFIED;
extern int MIME_WKSIDX_LINES;
extern int MIME_WKSIDX_LOCATION;
extern int MIME_WKSIDX_MAX_FORWARDS;
extern int MIME_WKSIDX_MESSAGE_ID;
extern int MIME_WKSIDX_NEWSGROUPS;
extern int MIME_WKSIDX_ORGANIZATION;
extern int MIME_WKSIDX_PATH;
extern int MIME_WKSIDX_PRAGMA;
extern int MIME_WKSIDX_PROXY_AUTHENTICATE;
extern int MIME_WKSIDX_PROXY_AUTHORIZATION;
extern int MIME_WKSIDX_PROXY_CONNECTION;
extern int MIME_WKSIDX_PUBLIC;
extern int MIME_WKSIDX_RANGE;
extern int MIME_WKSIDX_REFERENCES;
extern int MIME_WKSIDX_REFERER;
extern int MIME_WKSIDX_REPLY_TO;
extern int MIME_WKSIDX_RETRY_AFTER;
extern int MIME_WKSIDX_SENDER;
extern int MIME_WKSIDX_SERVER;
extern int MIME_WKSIDX_SET_COOKIE;
extern int MIME_WKSIDX_STRICT_TRANSPORT_SECURITY;
extern int MIME_WKSIDX_SUBJECT;
extern int MIME_WKSIDX_SUMMARY;
extern int MIME_WKSIDX_TE;
extern int MIME_WKSIDX_TRANSFER_ENCODING;
extern int MIME_WKSIDX_UPGRADE;
extern int MIME_WKSIDX_USER_AGENT;
extern int MIME_WKSIDX_VARY;
extern int MIME_WKSIDX_VIA;
extern int MIME_WKSIDX_WARNING;
extern int MIME_WKSIDX_WWW_AUTHENTICATE;
extern int MIME_WKSIDX_XREF;
extern int MIME_WKSIDX_ATS_INTERNAL;
extern int MIME_WKSIDX_X_ID;
extern int MIME_WKSIDX_SEC_WEBSOCKET_KEY;
extern int MIME_WKSIDX_SEC_WEBSOCKET_VERSION;
extern int MIME_WKSIDX_HTTP2_SETTINGS;
extern int MIME_WKSIDX_EARLY_DATA;

/***********************************************************************
 *                                                                     *
 *                           Internal C API                            *
 *                                                                     *
 ***********************************************************************/

uint64_t mime_field_presence_mask(const char *well_known_str);
uint64_t mime_field_presence_mask(int well_known_str_index);
int      mime_field_presence_get(MIMEHdrImpl *h, const char *well_known_str);
int      mime_field_presence_get(MIMEHdrImpl *h, int well_known_str_index);
void     mime_hdr_presence_set(MIMEHdrImpl *h, const char *well_known_str);
void     mime_hdr_presence_set(MIMEHdrImpl *h, int well_known_str_index);
void     mime_hdr_presence_unset(MIMEHdrImpl *h, const char *well_known_str);
void     mime_hdr_presence_unset(MIMEHdrImpl *h, int well_known_str_index);

void mime_hdr_sanity_check(MIMEHdrImpl *mh);

void mime_init();
void mime_init_cache_control_cooking_masks();
void mime_init_date_format_table();

MIMEHdrImpl        *mime_hdr_create(HdrHeap *heap);
void                _mime_hdr_field_block_init(MIMEFieldBlockImpl *fblock);
void                mime_hdr_cooked_stuff_init(MIMEHdrImpl *mh, MIMEField *changing_field_or_null = nullptr);
void                mime_hdr_init(MIMEHdrImpl *mh);
MIMEFieldBlockImpl *_mime_field_block_copy(MIMEFieldBlockImpl *s_fblock, HdrHeap *s_heap, HdrHeap *d_heap);
void                _mime_field_block_destroy(HdrHeap *heap, MIMEFieldBlockImpl *fblock);
void                mime_hdr_destroy_field_block_list(HdrHeap *heap, MIMEFieldBlockImpl *head);
void                mime_hdr_destroy(HdrHeap *heap, MIMEHdrImpl *mh);
void         mime_hdr_copy_onto(MIMEHdrImpl *s_mh, HdrHeap *s_heap, MIMEHdrImpl *d_mh, HdrHeap *d_heap, bool inherit_strs = true);
MIMEHdrImpl *mime_hdr_clone(MIMEHdrImpl *s_mh, HdrHeap *s_heap, HdrHeap *d_heap, bool inherit_strs = true);
void         mime_hdr_field_block_list_adjust(int block_count, MIMEFieldBlockImpl *old_list, MIMEFieldBlockImpl *new_list);
int          mime_hdr_length_get(MIMEHdrImpl *mh);

void mime_hdr_fields_clear(HdrHeap *heap, MIMEHdrImpl *mh);

MIMEField *_mime_hdr_field_list_search_by_wks(MIMEHdrImpl *mh, int wks_idx);
MIMEField *_mime_hdr_field_list_search_by_string(MIMEHdrImpl *mh, std::string_view field_name);
MIMEField *_mime_hdr_field_list_search_by_slotnum(MIMEHdrImpl *mh, int slotnum);
MIMEField *mime_hdr_field_find(MIMEHdrImpl *mh, std::string_view field_name);

MIMEField *mime_hdr_field_get(MIMEHdrImpl *mh, int idx);
MIMEField *mime_hdr_field_get_slotnum(MIMEHdrImpl *mh, int slotnum);
int        mime_hdr_fields_count(MIMEHdrImpl *mh);

void       mime_field_init(MIMEField *field);
MIMEField *mime_field_create(HdrHeap *heap, MIMEHdrImpl *mh);
MIMEField *mime_field_create_named(HdrHeap *heap, MIMEHdrImpl *mh, std::string_view name);

void mime_hdr_field_attach(MIMEHdrImpl *mh, MIMEField *field, int check_for_dups, MIMEField *prev_dup);
void mime_hdr_field_detach(MIMEHdrImpl *mh, MIMEField *field, bool detach_all_dups = false);
void mime_hdr_field_delete(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, bool delete_all_dups = false);

/**
 * Returned slotnum is not a persistent value. A slotnum may refer a different field after making changes to a mime header.
 */
int        mime_hdr_field_slotnum(MIMEHdrImpl *mh, MIMEField *field);
MIMEField *mime_hdr_prepare_for_value_set(HdrHeap *heap, MIMEHdrImpl *mh, std::string_view name);

void mime_field_destroy(MIMEHdrImpl *mh, MIMEField *field);

void mime_field_name_set(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int16_t name_wks_idx_or_neg1, std::string_view name,
                         bool must_copy_string);

int32_t     mime_field_value_get_int(const MIMEField *field);
uint32_t    mime_field_value_get_uint(const MIMEField *field);
int64_t     mime_field_value_get_int64(const MIMEField *field);
time_t      mime_field_value_get_date(const MIMEField *field);
const char *mime_field_value_get_comma_val(const MIMEField *field, int *length, int idx);
int         mime_field_value_get_comma_val_count(const MIMEField *field);
int         mime_field_value_get_comma_list(const MIMEField *field, StrList *list);

void mime_field_value_set_comma_val(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int idx, std::string_view new_piece);
void mime_field_value_delete_comma_val(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int idx);
void mime_field_value_extend_comma_val(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int idx, std::string_view new_piece);
void mime_field_value_insert_comma_val(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int idx, std::string_view new_piece);

void mime_field_value_set(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, std::string_view value, bool must_copy_string);
void mime_field_value_set_int(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int32_t value);
void mime_field_value_set_uint(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, uint32_t value);
void mime_field_value_set_int64(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int64_t value);
void mime_field_value_set_date(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, time_t value);
void mime_field_name_value_set(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int16_t name_wks_idx_or_neg1,
                               std::string_view name, std::string_view value, int n_v_raw_printable, int n_v_raw_length,
                               bool must_copy_strings);

void mime_field_value_append(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, std::string_view value, bool prepend_comma,
                             const char separator);

void        mime_parser_init(MIMEParser *parser);
void        mime_parser_clear(MIMEParser *parser);
ParseResult mime_parser_parse(MIMEParser *parser, HdrHeap *heap, MIMEHdrImpl *mh, const char **real_s, const char *real_e,
                              bool must_copy_strings, bool eof, bool remove_ws_from_field_name, size_t max_hdr_field_size = 131070);

void mime_hdr_describe(HdrHeapObjImpl *raw, bool recurse);
void mime_field_block_describe(HdrHeapObjImpl *raw, bool recurse);

int mime_hdr_print(MIMEHdrImpl const *mh, char *buf_start, int buf_length, int *buf_index_inout, int *buf_chars_to_skip_inout);
int mime_mem_print(std::string_view src, char *buf_start, int buf_length, int *buf_index_inout, int *buf_chars_to_skip_inout);
int mime_mem_print_lc(std::string_view src, char *buf_start, int buf_length, int *buf_index_inout, int *buf_chars_to_skip_inout);
int mime_field_print(MIMEField const *field, char *buf_start, int buf_length, int *buf_index_inout, int *buf_chars_to_skip_inout);

const char *mime_str_u16_set(HdrHeap *heap, std::string_view src, const char **d_str, uint16_t *d_len, bool must_copy);

int mime_field_length_get(MIMEField *field);
int mime_format_int(char *buf, int32_t val, size_t buf_len);
int mime_format_uint(char *buf, uint32_t val, size_t buf_len);
int mime_format_int64(char *buf, int64_t val, size_t buf_len);
int mime_format_uint64(char *buf, uint64_t val, size_t buf_len);

void mime_days_since_epoch_to_mdy_slowcase(time_t days_since_jan_1_1970, int *m_return, int *d_return, int *y_return);
void mime_days_since_epoch_to_mdy(time_t days_since_jan_1_1970, int *m_return, int *d_return, int *y_return);
int  mime_format_date(char *buffer, time_t value);

int32_t  mime_parse_int(const char *buf, const char *end = nullptr);
uint32_t mime_parse_uint(const char *buf, const char *end = nullptr);
int64_t  mime_parse_int64(const char *buf, const char *end = nullptr);
int      mime_parse_rfc822_date_fastcase(const char *buf, int length, struct tm *tp);
time_t   mime_parse_date(const char *buf, const char *end = nullptr);
bool     mime_parse_day(const char *&buf, const char *end, int *day);
bool     mime_parse_month(const char *&buf, const char *end, int *month);
bool     mime_parse_mday(const char *&buf, const char *end, int *mday);
bool     mime_parse_year(const char *&buf, const char *end, int *year);
bool     mime_parse_time(const char *&buf, const char *end, int *hour, int *min, int *sec);
bool     mime_parse_integer(const char *&buf, const char *end, int *integer);

/***********************************************************************
 *                                                                     *
 *                          MIMEField Methods                          *
 *                                                                     *
 ***********************************************************************/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEField::name_set(HdrHeap *heap, MIMEHdrImpl *mh, std::string_view name)
{
  const char *name_wks;

  if (hdrtoken_is_wks(name.data())) {
    int16_t name_wks_idx = hdrtoken_wks_to_index(name.data());
    mime_field_name_set(heap, mh, this, name_wks_idx, name, true);
  } else {
    int field_name_wks_idx = hdrtoken_tokenize(name.data(), static_cast<int>(name.length()), &name_wks);
    mime_field_name_set(heap, mh, this, field_name_wks_idx,
                        field_name_wks_idx == -1 ? name : std::string_view{name_wks, name.length()}, true);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
MIMEField::name_is_valid(uint32_t invalid_char_bits) const
{
  auto name{name_get()};
  for (auto c : name) {
    if (ParseRules::is_type(c, invalid_char_bits)) {
      return false;
    }
  }
  return true;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEField::value_get_int() const
{
  return mime_field_value_get_int(this);
}

inline uint32_t
MIMEField::value_get_uint() const
{
  return mime_field_value_get_uint(this);
}

inline int64_t
MIMEField::value_get_int64() const
{
  return mime_field_value_get_int64(this);
}

inline time_t
MIMEField::value_get_date() const
{
  return mime_field_value_get_date(this);
}

inline int
MIMEField::value_get_comma_list(StrList *list) const
{
  return mime_field_value_get_comma_list(this, list);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEField::value_set(HdrHeap *heap, MIMEHdrImpl *mh, std::string_view value)
{
  mime_field_value_set(heap, mh, this, value, true);
}

inline void
MIMEField::value_set_int(HdrHeap *heap, MIMEHdrImpl *mh, int32_t value)
{
  mime_field_value_set_int(heap, mh, this, value);
}

inline void
MIMEField::value_set_uint(HdrHeap *heap, MIMEHdrImpl *mh, uint32_t value)
{
  mime_field_value_set_uint(heap, mh, this, value);
}

inline void
MIMEField::value_set_int64(HdrHeap *heap, MIMEHdrImpl *mh, int64_t value)
{
  mime_field_value_set_int64(heap, mh, this, value);
}

inline void
MIMEField::value_set_date(HdrHeap *heap, MIMEHdrImpl *mh, time_t value)
{
  mime_field_value_set_date(heap, mh, this, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEField::value_clear(HdrHeap *heap, MIMEHdrImpl *mh)
{
  value_set(heap, mh, ""sv);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEField::value_append(HdrHeap *heap, MIMEHdrImpl *mh, std::string_view value, bool prepend_comma, const char separator)
{
  mime_field_value_append(heap, mh, this, value, prepend_comma, separator);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
MIMEField::value_is_valid(uint32_t invalid_char_bits) const
{
  auto value{value_get()};
  for (auto c : value) {
    if (ParseRules::is_type(c, invalid_char_bits)) {
      return false;
    }
  }
  return true;
}

inline int
MIMEField::has_dups() const
{
  return (m_next_dup != nullptr);
}

/***********************************************************************
 *                                                                     *
 *                           MIMEFieldIter                             *
 *                                                                     *
 ***********************************************************************/

struct MIMEFieldIter {
  MIMEFieldIter() {}
  uint32_t            m_slot  = 0;
  MIMEFieldBlockImpl *m_block = nullptr;
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/***********************************************************************
 *                                                                     *
 *                            MIMEHdr Class                            *
 *                                                                     *
 ***********************************************************************/

class MIMEHdr : public HdrHeapSDKHandle
{
public:
  using iterator = MIMEHdrImpl::iterator;

  MIMEHdrImpl *m_mime = nullptr;

  MIMEHdr() = default; // Force the creation of the default constructor

  int valid() const;

  void create(HdrHeap *heap = nullptr);
  void copy(const MIMEHdr *hdr);

  int length_get() const;

  void fields_clear();
  int  fields_count() const;

  MIMEField       *field_create(std::string_view name = ""sv);
  MIMEField       *field_find(std::string_view name);
  const MIMEField *field_find(std::string_view name) const;
  void             field_attach(MIMEField *field);
  void             field_detach(MIMEField *field, bool detach_all_dups = true);
  void             field_delete(MIMEField *field, bool delete_all_dups = true);
  void             field_delete(std::string_view name);

  iterator begin() const;
  iterator end() const;

  /*
  MIMEField *iter_get_first(MIMEFieldIter *iter);
  MIMEField *iter_get(MIMEFieldIter *iter);
  MIMEField *iter_get_next(MIMEFieldIter *iter);
   */

  uint64_t presence(uint64_t mask) const;

  int print(char *buf, int bufsize, int *bufindex, int *chars_to_skip);

  ParseResult parse(MIMEParser *parser, const char **start, const char *end, bool must_copy_strs, bool eof,
                    bool remove_ws_from_field_name, size_t max_hdr_field_size = UINT16_MAX);

  int              value_get_index(std::string_view name, std::string_view value) const;
  std::string_view value_get(std::string_view name) const;
  int32_t          value_get_int(std::string_view name) const;
  uint32_t         value_get_uint(std::string_view name) const;
  int64_t          value_get_int64(std::string_view name) const;
  time_t           value_get_date(std::string_view name) const;
  int              value_get_comma_list(std::string_view name, StrList *list) const;

  void value_set(std::string_view name, std::string_view value);
  void value_set_int(std::string_view name, int32_t value);
  void value_set_uint(std::string_view name, uint32_t value);
  void value_set_int64(std::string_view name, int64_t value);
  void value_set_date(std::string_view name, time_t value);
  // MIME standard separator ',' is used as the default value
  // Other separators (e.g. ';' in Set-cookie/Cookie) are also possible
  void value_append(std::string_view name, std::string_view value, bool prepend_comma = false, const char separator = ',');

  void field_value_set(MIMEField *field, std::string_view value, bool reuse_heaps = false);
  void field_value_set_int(MIMEField *field, int32_t value);
  void field_value_set_uint(MIMEField *field, uint32_t value);
  void field_value_set_int64(MIMEField *field, int64_t value);
  void field_value_set_date(MIMEField *field, time_t value);
  // MIME standard separator ',' is used as the default value
  // Other separators (e.g. ';' in Set-cookie/Cookie) are also possible
  void    field_value_append(MIMEField *field, std::string_view value, bool prepend_comma = false, const char separator = ',');
  void    value_append_or_set(std::string_view name, std::string_view value);
  void    field_combine_dups(MIMEField *field, bool prepend_comma = false, const char separator = ',');
  time_t  get_age() const;
  int64_t get_content_length() const;
  time_t  get_date() const;
  time_t  get_expires() const;
  time_t  get_if_modified_since() const;
  time_t  get_if_unmodified_since() const;
  time_t  get_last_modified() const;
  time_t  get_if_range_date() const;
  int32_t get_max_forwards() const;
  int32_t get_warning(int idx = 0);

  uint32_t get_cooked_cc_mask() const;
  int32_t  get_cooked_cc_max_age() const;
  int32_t  get_cooked_cc_s_maxage() const;
  int32_t  get_cooked_cc_max_stale() const;
  int32_t  get_cooked_cc_min_fresh() const;
  bool     get_cooked_pragma_no_cache() const;

  /** Get the value of the host field.
      This parses the host field for brackets and port value.
      @return The tuple of mime HOST field, host, and port if it has a value,
              or the tuple of @c NULL and two empty string views. otherwise.
  */
  std::tuple<MIMEField *, std::string_view, std::string_view> get_host_port_values();

  void set_cooked_cc_need_revalidate_once();
  void unset_cooked_cc_need_revalidate_once();

  void set_age(time_t value);
  void set_content_length(int64_t value);
  void set_date(time_t value);
  void set_expires(time_t value);
  void set_if_modified_since(time_t value);
  void set_if_unmodified_since(time_t value);
  void set_last_modified(time_t value);
  void set_max_forwards(int32_t value);
  void set_warning(int32_t value);
  void set_server(std::string_view server_id_tag);

  // No gratuitous copies & refcounts!
  MIMEHdr(const MIMEHdr &m)            = delete;
  MIMEHdr &operator=(const MIMEHdr &m) = delete;

private:
  // Interface to replace (overwrite) field value without
  // changing the heap as long as the new value is not longer
  // than the current value
  bool field_value_replace(MIMEField *field, std::string_view value);
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
MIMEHdr::valid() const
{
  return (m_mime && m_heap);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::create(HdrHeap *heap)
{
  if (heap) {
    m_heap = heap;
  } else if (!m_heap) {
    m_heap = new_HdrHeap();
  }

  m_mime = mime_hdr_create(m_heap);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::copy(const MIMEHdr *src_hdr)
{
  if (valid()) {
    mime_hdr_copy_onto(src_hdr->m_mime, src_hdr->m_heap, m_mime, m_heap, (m_heap != src_hdr->m_heap) ? true : false);
  } else {
    m_heap = new_HdrHeap();
    m_mime = mime_hdr_clone(src_hdr->m_mime, src_hdr->m_heap, m_heap);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
MIMEHdr::length_get() const
{
  return mime_hdr_length_get(m_mime);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::fields_clear()
{
  mime_hdr_fields_clear(m_heap, m_mime);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
MIMEHdr::fields_count() const
{
  return mime_hdr_fields_count(m_mime);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline MIMEField *
MIMEHdr::field_create(std::string_view name)
{
  MIMEField *field = mime_field_create(m_heap, m_mime);

  if (!name.empty()) {
    auto length{static_cast<int>(name.length())};
    int  field_name_wks_idx = hdrtoken_tokenize(name.data(), length);
    mime_field_name_set(m_heap, m_mime, field, field_name_wks_idx, name, true);
  }

  return field;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline MIMEField *
MIMEHdr::field_find(std::string_view name) // NOLINT(readability-make-member-function-const)
{
  //    ink_assert(valid());
  return mime_hdr_field_find(m_mime, name);
}

inline const MIMEField *
MIMEHdr::field_find(std::string_view name) const
{
  //    ink_assert(valid());
  MIMEField *retval = mime_hdr_field_find(const_cast<MIMEHdr *>(this)->m_mime, name);
  return retval;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::field_attach(MIMEField *field) // NOLINT(readability-make-member-function-const)
{
  mime_hdr_field_attach(m_mime, field, 1, nullptr);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::field_detach(MIMEField *field, bool detach_all_dups) // NOLINT(readability-make-member-function-const)
{
  mime_hdr_field_detach(m_mime, field, detach_all_dups);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::field_delete(MIMEField *field, bool delete_all_dups)
{
  mime_hdr_field_delete(m_heap, m_mime, field, delete_all_dups);
}

inline auto
MIMEHdr::begin() const -> iterator
{
  return m_mime ? m_mime->begin() : iterator();
}

inline auto
MIMEHdr::end() const -> iterator
{
  return {}; // default constructed iterator.
}

inline void
MIMEHdr::field_delete(std::string_view name)
{
  MIMEField *field = field_find(name);
  if (field)
    field_delete(field);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline uint64_t
MIMEHdr::presence(uint64_t mask) const
{
  return (m_mime->m_presence_bits & mask);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
MIMEHdr::print(char *buf, int bufsize, int *bufindex, int *chars_to_skip)
{
  return mime_hdr_print(m_mime, buf, bufsize, bufindex, chars_to_skip);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline ParseResult
MIMEHdr::parse(MIMEParser *parser, const char **start, const char *end, bool must_copy_strs, bool eof,
               bool remove_ws_from_field_name, size_t max_hdr_field_size)
{
  if (!m_heap)
    m_heap = new_HdrHeap();

  if (!m_mime)
    m_mime = mime_hdr_create(m_heap);

  return mime_parser_parse(parser, m_heap, m_mime, start, end, must_copy_strs, eof, remove_ws_from_field_name, max_hdr_field_size);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
inline int
MIMEHdr::value_get_index(std::string_view name, std::string_view value) const
{
  const MIMEField *field = field_find(name);

  if (field) {
    return field->value_get_index(value);
  }
  return -1;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline std::string_view
MIMEHdr::value_get(std::string_view name) const
{
  MIMEField const *field = field_find(name);

  if (field) {
    return field->value_get();
  }
  return {};
}

inline int32_t
MIMEHdr::value_get_int(std::string_view name) const
{
  const MIMEField *field = field_find(name);

  if (field) {
    return mime_field_value_get_int(field);
  }
  return 0;
}

inline uint32_t
MIMEHdr::value_get_uint(std::string_view name) const
{
  const MIMEField *field = field_find(name);

  if (field) {
    return mime_field_value_get_uint(field);
  }
  return 0;
}

inline int64_t
MIMEHdr::value_get_int64(std::string_view name) const
{
  const MIMEField *field = field_find(name);

  if (field) {
    return mime_field_value_get_int64(field);
  }
  return 0;
}

inline time_t
MIMEHdr::value_get_date(std::string_view name) const
{
  const MIMEField *field = field_find(name);

  if (field) {
    return mime_field_value_get_date(field);
  }
  return 0;
}

inline int
MIMEHdr::value_get_comma_list(std::string_view name, StrList *list) const
{
  const MIMEField *field = field_find(name);

  if (field) {
    return field->value_get_comma_list(list);
  }
  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
MIMEHdr::field_value_replace(MIMEField *field, std::string_view value)
{
  auto value_length{static_cast<uint32_t>(value.length())};
  if (field->m_len_value >= value_length) {
    memcpy((char *)field->m_ptr_value, value.data(), value_length);
    field->m_len_value = value_length;
    return true;
  }
  return false;
}

inline void
MIMEHdr::field_value_set(MIMEField *field, std::string_view value, bool reuse_heaps)
{
  if (!reuse_heaps || !field_value_replace(field, value)) {
    field->value_set(m_heap, m_mime, value);
  }
}

inline void
MIMEHdr::field_value_set_int(MIMEField *field, int32_t value)
{
  field->value_set_int(m_heap, m_mime, value);
}

inline void
MIMEHdr::field_value_set_uint(MIMEField *field, uint32_t value)
{
  field->value_set_uint(m_heap, m_mime, value);
}

inline void
MIMEHdr::field_value_set_int64(MIMEField *field, int64_t value)
{
  field->value_set_int64(m_heap, m_mime, value);
}

inline void
MIMEHdr::field_value_set_date(MIMEField *field, time_t value)
{
  field->value_set_date(m_heap, m_mime, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::field_value_append(MIMEField *field, std::string_view value, bool prepend_comma, const char separator)
{
  field->value_append(m_heap, m_mime, value, prepend_comma, separator);
}

inline void
MIMEHdr::field_combine_dups(MIMEField *field, bool prepend_comma, const char separator)
{
  MIMEField *current = field->m_next_dup;

  while (current) {
    auto value{current->value_get()};
    if (value.length() > 0) {
      HdrHeap::HeapGuard guard(m_heap, value.data()); // reference count the source string so it doesn't get moved
      field->value_append(m_heap, m_mime, value, prepend_comma, separator);
    }
    field_delete(current, false); // don't delete duplicates
    current = field->m_next_dup;
  }
}

inline void
MIMEHdr::value_append_or_set(std::string_view name, std::string_view value)
{
  MIMEField *field = nullptr;

  if ((field = field_find(name)) != nullptr) {
    while (field->m_next_dup) {
      field = field->m_next_dup;
    }
    field_value_append(field, value, true);
  } else {
    value_set(name, value);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::value_set(std::string_view name, std::string_view value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name);
  field->value_set(m_heap, m_mime, value);
}

inline void
MIMEHdr::value_set_int(std::string_view name, int32_t value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name);
  field->value_set_int(m_heap, m_mime, value);
}

inline void
MIMEHdr::value_set_uint(std::string_view name, uint32_t value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name);
  field->value_set_uint(m_heap, m_mime, value);
}

inline void
MIMEHdr::value_set_int64(std::string_view name, int64_t value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name);
  field->value_set_int64(m_heap, m_mime, value);
}

inline void
MIMEHdr::value_set_date(std::string_view name, time_t value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name);
  field->value_set_date(m_heap, m_mime, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::value_append(std::string_view name, std::string_view value, bool prepend_comma, const char separator)
{
  MIMEField *field;

  field = field_find(name);
  if (field) {
    while (field->m_next_dup)
      field = field->m_next_dup;
    field->value_append(m_heap, m_mime, value, prepend_comma, separator);
  } else {
    field = field_create(name.empty() ? ""sv : name);
    field_attach(field);
    field->value_set(m_heap, m_mime, value);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
inline time_t
MIMEHdr::get_age() const
{
  int64_t age = value_get_int64(static_cast<std::string_view>(MIME_FIELD_AGE));

  if (age < 0) // We should ignore negative Age: values
    return 0;

  if ((4 == sizeof(time_t)) && (age > INT_MAX)) // Overflow
    return -1;

  return age;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int64_t
MIMEHdr::get_content_length() const
{
  return value_get_int64(static_cast<std::string_view>(MIME_FIELD_CONTENT_LENGTH));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_date() const
{
  return value_get_date(static_cast<std::string_view>(MIME_FIELD_DATE));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_expires() const
{
  return value_get_date(static_cast<std::string_view>(MIME_FIELD_EXPIRES));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_if_modified_since() const
{
  return value_get_date(static_cast<std::string_view>(MIME_FIELD_IF_MODIFIED_SINCE));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_if_unmodified_since() const
{
  return value_get_date(static_cast<std::string_view>(MIME_FIELD_IF_UNMODIFIED_SINCE));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_last_modified() const
{
  return value_get_date(static_cast<std::string_view>(MIME_FIELD_LAST_MODIFIED));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_if_range_date() const
{
  return value_get_date(static_cast<std::string_view>(MIME_FIELD_IF_RANGE));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_max_forwards() const
{
  return value_get_int(static_cast<std::string_view>(MIME_FIELD_MAX_FORWARDS));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_warning(int idx)
{
  (void)idx;
  // FIXME: what do we do here?
  ink_release_assert(!"unimplemented");
  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline uint32_t
MIMEHdr::get_cooked_cc_mask() const
{
  return m_mime->m_cooked_stuff.m_cache_control.m_mask;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_cooked_cc_max_age() const
{
  return m_mime->m_cooked_stuff.m_cache_control.m_secs_max_age;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_cooked_cc_s_maxage() const
{
  return m_mime->m_cooked_stuff.m_cache_control.m_secs_s_maxage;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_cooked_cc_max_stale() const
{
  return m_mime->m_cooked_stuff.m_cache_control.m_secs_max_stale;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_cooked_cc_min_fresh() const
{
  return m_mime->m_cooked_stuff.m_cache_control.m_secs_min_fresh;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
MIMEHdr::get_cooked_pragma_no_cache() const
{
  return m_mime->m_cooked_stuff.m_pragma.m_no_cache;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_cooked_cc_need_revalidate_once() // NOLINT(readability-make-member-function-const)
{
  m_mime->m_cooked_stuff.m_cache_control.m_mask |= MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::unset_cooked_cc_need_revalidate_once() // NOLINT(readability-make-member-function-const)
{
  m_mime->m_cooked_stuff.m_cache_control.m_mask &= ~((uint32_t)MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_age(time_t value)
{
  if (value < 0)
    value_set_uint(static_cast<std::string_view>(MIME_FIELD_AGE), (uint32_t)INT_MAX + 1);
  else {
    if (sizeof(time_t) > 4) {
      value_set_int64(static_cast<std::string_view>(MIME_FIELD_AGE), value);
    } else {
      // Only on systems where time_t is 32 bits
      // coverity[Y2K38_SAFETY]
      value_set_uint(static_cast<std::string_view>(MIME_FIELD_AGE), value);
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_content_length(int64_t value)
{
  value_set_int64(static_cast<std::string_view>(MIME_FIELD_CONTENT_LENGTH), value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_date(time_t value)
{
  value_set_date(static_cast<std::string_view>(MIME_FIELD_DATE), value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_expires(time_t value)
{
  value_set_date(static_cast<std::string_view>(MIME_FIELD_EXPIRES), value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_if_modified_since(time_t value)
{
  value_set_date(static_cast<std::string_view>(MIME_FIELD_IF_MODIFIED_SINCE), value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_if_unmodified_since(time_t value)
{
  value_set_date(static_cast<std::string_view>(MIME_FIELD_IF_UNMODIFIED_SINCE), value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_last_modified(time_t value)
{
  value_set_date(static_cast<std::string_view>(MIME_FIELD_LAST_MODIFIED), value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_max_forwards(int32_t value)
{
  value_set_int(static_cast<std::string_view>(MIME_FIELD_MAX_FORWARDS), value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_warning(int32_t value)
{
  value_set_int(static_cast<std::string_view>(MIME_FIELD_WARNING), value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_server(std::string_view server_id_tag)
{
  value_set(static_cast<std::string_view>(MIME_FIELD_SERVER), server_id_tag);
}
