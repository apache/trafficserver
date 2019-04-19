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

#include "tscore/ink_assert.h"
#include "tscore/ink_apidefs.h"
#include "tscore/ink_string++.h"
#include "tscore/ParseRules.h"
#include "HdrHeap.h"
#include "HdrToken.h"

/***********************************************************************
 *                                                                     *
 *                              Defines                                *
 *                                                                     *
 ***********************************************************************/

enum ParseResult {
  PARSE_RESULT_ERROR = -1,
  PARSE_RESULT_DONE  = 0,
  PARSE_RESULT_CONT  = 1,
  PARSE_RESULT_OK    = 3, // This is only used internally in mime_parser_parse and not returned to the user
};

enum {
  UNDEFINED_COUNT = -1,
};

/// Parsing state.
enum MimeParseState {
  MIME_PARSE_BEFORE,   ///< Before a field.
  MIME_PARSE_FOUND_CR, ///< Before a field, found a CR.
  MIME_PARSE_INSIDE,   ///< Inside a field.
  MIME_PARSE_AFTER,    ///< After a field.
};

#define MIME_SCANNER_TYPE_LINE 0
#define MIME_SCANNER_TYPE_FIELD 1

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

#define MIME_FIELD_SLOT_READINESS_EMPTY 0
#define MIME_FIELD_SLOT_READINESS_DETACHED 1
#define MIME_FIELD_SLOT_READINESS_LIVE 2
#define MIME_FIELD_SLOT_READINESS_DELETED 3

#define MIME_FIELD_SLOT_FLAGS_DUP_HEAD (1 << 0)
#define MIME_FIELD_SLOT_FLAGS_COOKED (1 << 1)

#define MIME_FIELD_BLOCK_SLOTS 16

#define MIME_FIELD_SLOTNUM_BITS 4
#define MIME_FIELD_SLOTNUM_MASK ((1 << MIME_FIELD_SLOTNUM_BITS) - 1)
#define MIME_FIELD_SLOTNUM_MAX (MIME_FIELD_SLOTNUM_MASK - 1)
#define MIME_FIELD_SLOTNUM_UNKNOWN MIME_FIELD_SLOTNUM_MAX

/***********************************************************************
 *                                                                     *
 *                    MIMEField & MIMEFieldBlockImpl                   *
 *                                                                     *
 ***********************************************************************/

struct MIMEHdrImpl;

struct MIMEField {
  const char *m_ptr_name;              // 4
  const char *m_ptr_value;             // 4
  MIMEField *m_next_dup;               // 4
  int16_t m_wks_idx;                   // 2
  uint16_t m_len_name;                 // 2
  uint32_t m_len_value : 24;           // 3
  uint8_t m_n_v_raw_printable : 1;     // 1/8
  uint8_t m_n_v_raw_printable_pad : 3; // 3/8
  uint8_t m_readiness : 2;             // 2/8
  uint8_t m_flags : 2;                 // 2/8

  bool
  is_dup_head() const
  {
    return (m_flags & MIME_FIELD_SLOT_FLAGS_DUP_HEAD);
  }

  bool
  is_cooked()
  {
    return (m_flags & MIME_FIELD_SLOT_FLAGS_COOKED);
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
    if (m_wks_idx >= 0)
      return (hdrtoken_index_to_flags(m_wks_idx) & MIME_FLAGS_COMMAS);
    else
      return true; // by default, assume supports commas
  }

  const char *name_get(int *length) const;

  /** Find the index of the value in the multi-value field.

     If @a value is one of the values in this field return the
     0 based index of it in the list of values. If the field is
     not multivalued the index will always be zero if found.
     Otherwise return -1 if the @a value is not found.

     @note The most common use of this is to check for the presence of a specific
     value in a comma enabled field.

     @return The index of @a value.
  */
  int value_get_index(const char *value, int length) const;

  const char *value_get(int *length) const;
  int32_t value_get_int() const;
  uint32_t value_get_uint() const;
  int64_t value_get_int64() const;
  time_t value_get_date() const;
  int value_get_comma_list(StrList *list) const;

  void name_set(HdrHeap *heap, MIMEHdrImpl *mh, const char *name, int length);
  bool name_is_valid() const;

  void value_set(HdrHeap *heap, MIMEHdrImpl *mh, const char *value, int length);
  void value_set_int(HdrHeap *heap, MIMEHdrImpl *mh, int32_t value);
  void value_set_uint(HdrHeap *heap, MIMEHdrImpl *mh, uint32_t value);
  void value_set_int64(HdrHeap *heap, MIMEHdrImpl *mh, int64_t value);
  void value_set_date(HdrHeap *heap, MIMEHdrImpl *mh, time_t value);
  void value_clear(HdrHeap *heap, MIMEHdrImpl *mh);
  // MIME standard separator ',' is used as the default value
  // Other separators (e.g. ';' in Set-cookie/Cookie) are also possible
  void value_append(HdrHeap *heap, MIMEHdrImpl *mh, const char *value, int length, bool prepend_comma = false,
                    const char separator = ',');
  bool value_is_valid() const;
  int has_dups() const;
};

struct MIMEFieldBlockImpl : public HdrHeapObjImpl {
  // HdrHeapObjImpl is 4 bytes
  uint32_t m_freetop;
  MIMEFieldBlockImpl *m_next;
  MIMEField m_field_slots[MIME_FIELD_BLOCK_SLOTS];
  // mime_hdr_copy_onto assumes that m_field_slots is last --
  // don't add any new fields after it.

  // Marshaling Functions
  int marshal(MarshalXlate *ptr_xlate, int num_ptr, MarshalXlate *str_xlate, int num_str);
  void unmarshal(intptr_t offset);
  void move_strings(HdrStrHeap *new_heap);
  size_t strings_length();
  bool contains(const MIMEField *field);

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
  int32_t m_secs_max_age;
  int32_t m_secs_s_maxage;
  int32_t m_secs_max_stale;
  int32_t m_secs_min_fresh;
};

struct MIMECookedPragma {
  bool m_no_cache;
};

struct MIMECooked {
  MIMECookedCacheControl m_cache_control;
  MIMECookedPragma m_pragma;
};

/***********************************************************************
 *                                                                     *
 *                                MIMEHdr                              *
 *                                                                     *
 ***********************************************************************/

struct MIMEHdrImpl : public HdrHeapObjImpl {
  // HdrHeapObjImpl is 4 bytes, so this will result in 4 bytes padding
  uint64_t m_presence_bits;
  uint32_t m_slot_accelerators[4];

  MIMECooked m_cooked_stuff;

  MIMEFieldBlockImpl *m_fblock_list_tail;
  MIMEFieldBlockImpl m_first_fblock; // 1 block inline
  // mime_hdr_copy_onto assumes that m_first_fblock is last --
  // don't add any new fields after it.

  // Marshaling Functions
  int marshal(MarshalXlate *ptr_xlate, int num_ptr, MarshalXlate *str_xlate, int num_str);
  void unmarshal(intptr_t offset);
  void move_strings(HdrStrHeap *new_heap);
  size_t strings_length();

  // Sanity Check Functions
  void check_strings(HeapCheck *heaps, int num_heaps);

  // Cooked values
  void recompute_cooked_stuff(MIMEField *changing_field_or_null = nullptr);
  void recompute_accelerators_and_presence_bits();
};

/***********************************************************************
 *                                                                     *
 *                                Parser                               *
 *                                                                     *
 ***********************************************************************/

struct MIMEScanner {
  char *m_line;           // buffered line being built up
  int m_line_length;      // size of real live data in buffer
  int m_line_size;        // total allocated size of buffer
  MimeParseState m_state; ///< Parsing machine state.
};

struct MIMEParser {
  MIMEScanner m_scanner;
  int32_t m_field;
  int m_field_flags;
  int m_value;
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
  MIMEField *field_ptr;
};

/***********************************************************************
 *                                                                     *
 *                     Well-Known Field Name Tokens                    *
 *                                                                     *
 ***********************************************************************/

extern const char *MIME_FIELD_ACCEPT;
extern const char *MIME_FIELD_ACCEPT_CHARSET;
extern const char *MIME_FIELD_ACCEPT_ENCODING;
extern const char *MIME_FIELD_ACCEPT_LANGUAGE;
extern const char *MIME_FIELD_ACCEPT_RANGES;
extern const char *MIME_FIELD_AGE;
extern const char *MIME_FIELD_ALLOW;
extern const char *MIME_FIELD_APPROVED;
extern const char *MIME_FIELD_AUTHORIZATION;
extern const char *MIME_FIELD_BYTES;
extern const char *MIME_FIELD_CACHE_CONTROL;
extern const char *MIME_FIELD_CLIENT_IP;
extern const char *MIME_FIELD_CONNECTION;
extern const char *MIME_FIELD_CONTENT_BASE;
extern const char *MIME_FIELD_CONTENT_ENCODING;
extern const char *MIME_FIELD_CONTENT_LANGUAGE;
extern const char *MIME_FIELD_CONTENT_LENGTH;
extern const char *MIME_FIELD_CONTENT_LOCATION;
extern const char *MIME_FIELD_CONTENT_MD5;
extern const char *MIME_FIELD_CONTENT_RANGE;
extern const char *MIME_FIELD_CONTENT_TYPE;
extern const char *MIME_FIELD_CONTROL;
extern const char *MIME_FIELD_COOKIE;
extern const char *MIME_FIELD_DATE;
extern const char *MIME_FIELD_DISTRIBUTION;
extern const char *MIME_FIELD_ETAG;
extern const char *MIME_FIELD_EXPECT;
extern const char *MIME_FIELD_EXPIRES;
extern const char *MIME_FIELD_FOLLOWUP_TO;
extern const char *MIME_FIELD_FROM;
extern const char *MIME_FIELD_HOST;
extern const char *MIME_FIELD_IF_MATCH;
extern const char *MIME_FIELD_IF_MODIFIED_SINCE;
extern const char *MIME_FIELD_IF_NONE_MATCH;
extern const char *MIME_FIELD_IF_RANGE;
extern const char *MIME_FIELD_IF_UNMODIFIED_SINCE;
extern const char *MIME_FIELD_KEEP_ALIVE;
extern const char *MIME_FIELD_KEYWORDS;
extern const char *MIME_FIELD_LAST_MODIFIED;
extern const char *MIME_FIELD_LINES;
inkcoreapi extern const char *MIME_FIELD_LOCATION;
extern const char *MIME_FIELD_MAX_FORWARDS;
extern const char *MIME_FIELD_MESSAGE_ID;
extern const char *MIME_FIELD_NEWSGROUPS;
extern const char *MIME_FIELD_ORGANIZATION;
extern const char *MIME_FIELD_PATH;
extern const char *MIME_FIELD_PRAGMA;
extern const char *MIME_FIELD_PROXY_AUTHENTICATE;
extern const char *MIME_FIELD_PROXY_AUTHORIZATION;
extern const char *MIME_FIELD_PROXY_CONNECTION;
extern const char *MIME_FIELD_PUBLIC;
extern const char *MIME_FIELD_RANGE;
extern const char *MIME_FIELD_REFERENCES;
extern const char *MIME_FIELD_REFERER;
extern const char *MIME_FIELD_REPLY_TO;
extern const char *MIME_FIELD_RETRY_AFTER;
extern const char *MIME_FIELD_SENDER;
extern const char *MIME_FIELD_SERVER;
extern const char *MIME_FIELD_SET_COOKIE;
extern const char *MIME_FIELD_STRICT_TRANSPORT_SECURITY;
extern const char *MIME_FIELD_SUBJECT;
extern const char *MIME_FIELD_SUMMARY;
extern const char *MIME_FIELD_TE;
extern const char *MIME_FIELD_TRANSFER_ENCODING;
extern const char *MIME_FIELD_UPGRADE;
extern const char *MIME_FIELD_USER_AGENT;
extern const char *MIME_FIELD_VARY;
extern const char *MIME_FIELD_VIA;
extern const char *MIME_FIELD_WARNING;
extern const char *MIME_FIELD_WWW_AUTHENTICATE;
extern const char *MIME_FIELD_XREF;
extern const char *MIME_FIELD_ATS_INTERNAL;
extern const char *MIME_FIELD_X_ID;
extern const char *MIME_FIELD_X_FORWARDED_FOR;
extern const char *MIME_FIELD_FORWARDED;
extern const char *MIME_FIELD_SEC_WEBSOCKET_KEY;
extern const char *MIME_FIELD_SEC_WEBSOCKET_VERSION;
extern const char *MIME_FIELD_HTTP2_SETTINGS;

extern const char *MIME_VALUE_BYTES;
extern const char *MIME_VALUE_CHUNKED;
extern const char *MIME_VALUE_CLOSE;
extern const char *MIME_VALUE_COMPRESS;
extern const char *MIME_VALUE_DEFLATE;
extern const char *MIME_VALUE_GZIP;
extern const char *MIME_VALUE_IDENTITY;
extern const char *MIME_VALUE_KEEP_ALIVE;
extern const char *MIME_VALUE_MAX_AGE;
extern const char *MIME_VALUE_MAX_STALE;
extern const char *MIME_VALUE_MIN_FRESH;
extern const char *MIME_VALUE_MUST_REVALIDATE;
extern const char *MIME_VALUE_NONE;
extern const char *MIME_VALUE_NO_CACHE;
extern const char *MIME_VALUE_NO_STORE;
extern const char *MIME_VALUE_NO_TRANSFORM;
extern const char *MIME_VALUE_ONLY_IF_CACHED;
extern const char *MIME_VALUE_PRIVATE;
extern const char *MIME_VALUE_PROXY_REVALIDATE;
extern const char *MIME_VALUE_PUBLIC;
extern const char *MIME_VALUE_S_MAXAGE;
extern const char *MIME_VALUE_NEED_REVALIDATE_ONCE;
extern const char *MIME_VALUE_WEBSOCKET;
extern const char *MIME_VALUE_H2C;

extern int MIME_LEN_ACCEPT;
extern int MIME_LEN_ACCEPT_CHARSET;
extern int MIME_LEN_ACCEPT_ENCODING;
extern int MIME_LEN_ACCEPT_LANGUAGE;
extern int MIME_LEN_ACCEPT_RANGES;
extern int MIME_LEN_AGE;
extern int MIME_LEN_ALLOW;
extern int MIME_LEN_APPROVED;
extern int MIME_LEN_AUTHORIZATION;
extern int MIME_LEN_BYTES;
extern int MIME_LEN_CACHE_CONTROL;
extern int MIME_LEN_CLIENT_IP;
extern int MIME_LEN_CONNECTION;
extern int MIME_LEN_CONTENT_BASE;
extern int MIME_LEN_CONTENT_ENCODING;
extern int MIME_LEN_CONTENT_LANGUAGE;
extern int MIME_LEN_CONTENT_LENGTH;
extern int MIME_LEN_CONTENT_LOCATION;
extern int MIME_LEN_CONTENT_MD5;
extern int MIME_LEN_CONTENT_RANGE;
extern int MIME_LEN_CONTENT_TYPE;
extern int MIME_LEN_CONTROL;
extern int MIME_LEN_COOKIE;
extern int MIME_LEN_DATE;
extern int MIME_LEN_DISTRIBUTION;
extern int MIME_LEN_ETAG;
extern int MIME_LEN_EXPECT;
extern int MIME_LEN_EXPIRES;
extern int MIME_LEN_FOLLOWUP_TO;
extern int MIME_LEN_FROM;
extern int MIME_LEN_HOST;
extern int MIME_LEN_IF_MATCH;
extern int MIME_LEN_IF_MODIFIED_SINCE;
extern int MIME_LEN_IF_NONE_MATCH;
extern int MIME_LEN_IF_RANGE;
extern int MIME_LEN_IF_UNMODIFIED_SINCE;
extern int MIME_LEN_KEEP_ALIVE;
extern int MIME_LEN_KEYWORDS;
extern int MIME_LEN_LAST_MODIFIED;
extern int MIME_LEN_LINES;
inkcoreapi extern int MIME_LEN_LOCATION;
extern int MIME_LEN_MAX_FORWARDS;
extern int MIME_LEN_MESSAGE_ID;
extern int MIME_LEN_NEWSGROUPS;
extern int MIME_LEN_ORGANIZATION;
extern int MIME_LEN_PATH;
extern int MIME_LEN_PRAGMA;
extern int MIME_LEN_PROXY_AUTHENTICATE;
extern int MIME_LEN_PROXY_AUTHORIZATION;
extern int MIME_LEN_PROXY_CONNECTION;
extern int MIME_LEN_PUBLIC;
extern int MIME_LEN_RANGE;
extern int MIME_LEN_REFERENCES;
extern int MIME_LEN_REFERER;
extern int MIME_LEN_REPLY_TO;
extern int MIME_LEN_RETRY_AFTER;
extern int MIME_LEN_SENDER;
extern int MIME_LEN_SERVER;
extern int MIME_LEN_SET_COOKIE;
extern int MIME_LEN_STRICT_TRANSPORT_SECURITY;
extern int MIME_LEN_SUBJECT;
extern int MIME_LEN_SUMMARY;
extern int MIME_LEN_TE;
extern int MIME_LEN_TRANSFER_ENCODING;
extern int MIME_LEN_UPGRADE;
extern int MIME_LEN_USER_AGENT;
extern int MIME_LEN_VARY;
extern int MIME_LEN_VIA;
extern int MIME_LEN_WARNING;
extern int MIME_LEN_WWW_AUTHENTICATE;
extern int MIME_LEN_XREF;
extern int MIME_LEN_ATS_INTERNAL;
extern int MIME_LEN_X_ID;
extern int MIME_LEN_X_FORWARDED_FOR;
extern int MIME_LEN_FORWARDED;

extern int MIME_LEN_BYTES;
extern int MIME_LEN_CHUNKED;
extern int MIME_LEN_CLOSE;
extern int MIME_LEN_COMPRESS;
extern int MIME_LEN_DEFLATE;
extern int MIME_LEN_GZIP;
extern int MIME_LEN_IDENTITY;
extern int MIME_LEN_KEEP_ALIVE;
extern int MIME_LEN_MAX_AGE;
extern int MIME_LEN_MAX_STALE;
extern int MIME_LEN_MIN_FRESH;
extern int MIME_LEN_MUST_REVALIDATE;
extern int MIME_LEN_NONE;
extern int MIME_LEN_NO_CACHE;
extern int MIME_LEN_NO_STORE;
extern int MIME_LEN_NO_TRANSFORM;
extern int MIME_LEN_ONLY_IF_CACHED;
extern int MIME_LEN_PRIVATE;
extern int MIME_LEN_PROXY_REVALIDATE;
extern int MIME_LEN_PUBLIC;
extern int MIME_LEN_S_MAXAGE;
extern int MIME_LEN_NEED_REVALIDATE_ONCE;

extern int MIME_LEN_SEC_WEBSOCKET_KEY;
extern int MIME_LEN_SEC_WEBSOCKET_VERSION;

extern int MIME_LEN_HTTP2_SETTINGS;

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

/***********************************************************************
 *                                                                     *
 *                           Internal C API                            *
 *                                                                     *
 ***********************************************************************/

uint64_t mime_field_presence_mask(const char *well_known_str);
uint64_t mime_field_presence_mask(int well_known_str_index);
int mime_field_presence_get(MIMEHdrImpl *h, const char *well_known_str);
int mime_field_presence_get(MIMEHdrImpl *h, int well_known_str_index);
void mime_hdr_presence_set(MIMEHdrImpl *h, const char *well_known_str);
void mime_hdr_presence_set(MIMEHdrImpl *h, int well_known_str_index);
void mime_hdr_presence_unset(MIMEHdrImpl *h, const char *well_known_str);
void mime_hdr_presence_unset(MIMEHdrImpl *h, int well_known_str_index);

void mime_hdr_sanity_check(MIMEHdrImpl *mh);

void mime_init();
void mime_init_cache_control_cooking_masks();
void mime_init_date_format_table();

MIMEHdrImpl *mime_hdr_create(HdrHeap *heap);
void _mime_hdr_field_block_init(MIMEFieldBlockImpl *fblock);
void mime_hdr_cooked_stuff_init(MIMEHdrImpl *mh, MIMEField *changing_field_or_null = nullptr);
void mime_hdr_init(MIMEHdrImpl *mh);
MIMEFieldBlockImpl *_mime_field_block_copy(MIMEFieldBlockImpl *s_fblock, HdrHeap *s_heap, HdrHeap *d_heap);
void _mime_field_block_destroy(HdrHeap *heap, MIMEFieldBlockImpl *fblock);
void mime_hdr_destroy_field_block_list(HdrHeap *heap, MIMEFieldBlockImpl *head);
void mime_hdr_destroy(HdrHeap *heap, MIMEHdrImpl *mh);
void mime_hdr_copy_onto(MIMEHdrImpl *s_mh, HdrHeap *s_heap, MIMEHdrImpl *d_mh, HdrHeap *d_heap, bool inherit_strs = true);
MIMEHdrImpl *mime_hdr_clone(MIMEHdrImpl *s_mh, HdrHeap *s_heap, HdrHeap *d_heap, bool inherit_strs = true);
void mime_hdr_field_block_list_adjust(int block_count, MIMEFieldBlockImpl *old_list, MIMEFieldBlockImpl *new_list);
int mime_hdr_length_get(MIMEHdrImpl *mh);

void mime_hdr_fields_clear(HdrHeap *heap, MIMEHdrImpl *mh);

MIMEField *_mime_hdr_field_list_search_by_wks(MIMEHdrImpl *mh, int wks_idx);
MIMEField *_mime_hdr_field_list_search_by_string(MIMEHdrImpl *mh, const char *field_name_str, int field_name_len);
MIMEField *_mime_hdr_field_list_search_by_slotnum(MIMEHdrImpl *mh, int slotnum);
inkcoreapi MIMEField *mime_hdr_field_find(MIMEHdrImpl *mh, const char *field_name_str, int field_name_len);

MIMEField *mime_hdr_field_get(MIMEHdrImpl *mh, int idx);
MIMEField *mime_hdr_field_get_slotnum(MIMEHdrImpl *mh, int slotnum);
int mime_hdr_fields_count(MIMEHdrImpl *mh);

void mime_field_init(MIMEField *field);
MIMEField *mime_field_create(HdrHeap *heap, MIMEHdrImpl *mh);
MIMEField *mime_field_create_named(HdrHeap *heap, MIMEHdrImpl *mh, const char *name, int length);

void mime_hdr_field_attach(MIMEHdrImpl *mh, MIMEField *field, int check_for_dups, MIMEField *prev_dup);
void mime_hdr_field_detach(MIMEHdrImpl *mh, MIMEField *field, bool detach_all_dups = false);
void mime_hdr_field_delete(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, bool delete_all_dups = false);

/**
 * Returned slotnum is not a persistent value. A slotnum may refer a different field after making changes to a mime header.
 */
int mime_hdr_field_slotnum(MIMEHdrImpl *mh, MIMEField *field);
inkcoreapi MIMEField *mime_hdr_prepare_for_value_set(HdrHeap *heap, MIMEHdrImpl *mh, const char *name, int name_length);

void mime_field_destroy(MIMEHdrImpl *mh, MIMEField *field);

const char *mime_field_name_get(const MIMEField *field, int *length);
void mime_field_name_set(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int16_t name_wks_idx_or_neg1, const char *name,
                         int length, bool must_copy_string);

inkcoreapi const char *mime_field_value_get(const MIMEField *field, int *length);
int32_t mime_field_value_get_int(const MIMEField *field);
uint32_t mime_field_value_get_uint(const MIMEField *field);
int64_t mime_field_value_get_int64(const MIMEField *field);
time_t mime_field_value_get_date(const MIMEField *field);
const char *mime_field_value_get_comma_val(const MIMEField *field, int *length, int idx);
int mime_field_value_get_comma_val_count(const MIMEField *field);
int mime_field_value_get_comma_list(const MIMEField *field, StrList *list);

void mime_field_value_set_comma_val(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int idx, const char *new_piece_str,
                                    int new_piece_len);
void mime_field_value_delete_comma_val(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int idx);
void mime_field_value_extend_comma_val(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int idx, const char *new_piece_str,
                                       int new_piece_len);
void mime_field_value_insert_comma_val(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int idx, const char *new_piece_str,
                                       int new_piece_len);

inkcoreapi void mime_field_value_set(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, const char *value, int length,
                                     bool must_copy_string);
void mime_field_value_set_int(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int32_t value);
void mime_field_value_set_uint(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, uint32_t value);
void mime_field_value_set_int64(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int64_t value);
void mime_field_value_set_date(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, time_t value);
void mime_field_name_value_set(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, int16_t name_wks_idx_or_neg1, const char *name,
                               int name_length, const char *value, int value_length, int n_v_raw_printable, int n_v_raw_length,
                               bool must_copy_strings);

void mime_field_value_append(HdrHeap *heap, MIMEHdrImpl *mh, MIMEField *field, const char *value, int length, bool prepend_comma,
                             const char separator);

void mime_scanner_init(MIMEScanner *scanner);
void mime_scanner_clear(MIMEScanner *scanner);
void mime_scanner_append(MIMEScanner *scanner, const char *data, int data_size);
ParseResult mime_scanner_get(MIMEScanner *S, const char **raw_input_s, const char *raw_input_e, const char **output_s,
                             const char **output_e, bool *output_shares_raw_input, bool raw_input_eof, int raw_input_scan_type);

void mime_parser_init(MIMEParser *parser);
void mime_parser_clear(MIMEParser *parser);
ParseResult mime_parser_parse(MIMEParser *parser, HdrHeap *heap, MIMEHdrImpl *mh, const char **real_s, const char *real_e,
                              bool must_copy_strings, bool eof);

void mime_hdr_describe(HdrHeapObjImpl *raw, bool recurse);
void mime_field_block_describe(HdrHeapObjImpl *raw, bool recurse);

int mime_hdr_print(HdrHeap *heap, MIMEHdrImpl *mh, char *buf_start, int buf_length, int *buf_index_inout,
                   int *buf_chars_to_skip_inout);
int mime_mem_print(const char *src_d, int src_l, char *buf_start, int buf_length, int *buf_index_inout,
                   int *buf_chars_to_skip_inout);
int mime_field_print(MIMEField *field, char *buf_start, int buf_length, int *buf_index_inout, int *buf_chars_to_skip_inout);

const char *mime_str_u16_set(HdrHeap *heap, const char *s_str, int s_len, const char **d_str, uint16_t *d_len, bool must_copy);

int mime_field_length_get(MIMEField *field);
int mime_format_int(char *buf, int32_t val, size_t buf_len);
int mime_format_uint(char *buf, uint32_t val, size_t buf_len);
int mime_format_int64(char *buf, int64_t val, size_t buf_len);

void mime_days_since_epoch_to_mdy_slowcase(unsigned int days_since_jan_1_1970, int *m_return, int *d_return, int *y_return);
void mime_days_since_epoch_to_mdy(unsigned int days_since_jan_1_1970, int *m_return, int *d_return, int *y_return);
int mime_format_date(char *buffer, time_t value);

int32_t mime_parse_int(const char *buf, const char *end = nullptr);
uint32_t mime_parse_uint(const char *buf, const char *end = nullptr);
int64_t mime_parse_int64(const char *buf, const char *end = nullptr);
int mime_parse_rfc822_date_fastcase(const char *buf, int length, struct tm *tp);
time_t mime_parse_date(const char *buf, const char *end = nullptr);
int mime_parse_day(const char *&buf, const char *end, int *day);
int mime_parse_month(const char *&buf, const char *end, int *month);
int mime_parse_mday(const char *&buf, const char *end, int *mday);
int mime_parse_year(const char *&buf, const char *end, int *year);
int mime_parse_time(const char *&buf, const char *end, int *hour, int *min, int *sec);
int mime_parse_integer(const char *&buf, const char *end, int *integer);

/***********************************************************************
 *                                                                     *
 *                          MIMEField Methods                          *
 *                                                                     *
 ***********************************************************************/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
MIMEField::name_get(int *length) const
{
  return (mime_field_name_get(this, length));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEField::name_set(HdrHeap *heap, MIMEHdrImpl *mh, const char *name, int length)
{
  int16_t name_wks_idx;
  const char *name_wks;

  if (hdrtoken_is_wks(name)) {
    name_wks_idx = hdrtoken_wks_to_index(name);
    mime_field_name_set(heap, mh, this, name_wks_idx, name, length, true);
  } else {
    int field_name_wks_idx = hdrtoken_tokenize(name, length, &name_wks);
    mime_field_name_set(heap, mh, this, field_name_wks_idx, (field_name_wks_idx == -1 ? name : name_wks), length, true);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
MIMEField::name_is_valid() const
{
  const char *name;
  int length;

  for (name = name_get(&length); length > 0; length--) {
    if (ParseRules::is_control(name[length - 1])) {
      return false;
    }
  }
  return true;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
MIMEField::value_get(int *length) const
{
  return (mime_field_value_get(this, length));
}

inline int32_t
MIMEField::value_get_int() const
{
  return (mime_field_value_get_int(this));
}

inline uint32_t
MIMEField::value_get_uint() const
{
  return (mime_field_value_get_uint(this));
}

inline int64_t
MIMEField::value_get_int64() const
{
  return (mime_field_value_get_int64(this));
}

inline time_t
MIMEField::value_get_date() const
{
  return (mime_field_value_get_date(this));
}

inline int
MIMEField::value_get_comma_list(StrList *list) const
{
  return (mime_field_value_get_comma_list(this, list));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEField::value_set(HdrHeap *heap, MIMEHdrImpl *mh, const char *value, int length)
{
  mime_field_value_set(heap, mh, this, value, length, true);
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
  value_set(heap, mh, "", 0);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEField::value_append(HdrHeap *heap, MIMEHdrImpl *mh, const char *value, int length, bool prepend_comma, const char separator)
{
  mime_field_value_append(heap, mh, this, value, length, prepend_comma, separator);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
MIMEField::value_is_valid() const
{
  const char *value;
  int length;

  for (value = value_get(&length); length > 0; length--) {
    if (ParseRules::is_control(value[length - 1])) {
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
  MIMEFieldIter() : m_slot(0), m_block(nullptr) {}
  uint32_t m_slot;
  MIMEFieldBlockImpl *m_block;
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
  MIMEHdrImpl *m_mime = nullptr;

  MIMEHdr() = default; // Force the creation of the default constructor

  int valid() const;

  void create(HdrHeap *heap = nullptr);
  void copy(const MIMEHdr *hdr);

  int length_get();

  void fields_clear();
  int fields_count();

  MIMEField *field_create(const char *name = nullptr, int length = -1);
  MIMEField *field_find(const char *name, int length);
  const MIMEField *field_find(const char *name, int length) const;
  void field_attach(MIMEField *field);
  void field_detach(MIMEField *field, bool detach_all_dups = true);
  void field_delete(MIMEField *field, bool delete_all_dups = true);
  void field_delete(const char *name, int name_length);

  MIMEField *iter_get_first(MIMEFieldIter *iter);
  MIMEField *iter_get(MIMEFieldIter *iter);
  MIMEField *iter_get_next(MIMEFieldIter *iter);

  uint64_t presence(uint64_t mask);

  int print(char *buf, int bufsize, int *bufindex, int *chars_to_skip);

  int parse(MIMEParser *parser, const char **start, const char *end, bool must_copy_strs, bool eof);

  int value_get_index(const char *name, int name_length, const char *value, int value_length) const;
  const char *value_get(const char *name, int name_length, int *value_length) const;
  int32_t value_get_int(const char *name, int name_length) const;
  uint32_t value_get_uint(const char *name, int name_length) const;
  int64_t value_get_int64(const char *name, int name_length) const;
  time_t value_get_date(const char *name, int name_length) const;
  int value_get_comma_list(const char *name, int name_length, StrList *list) const;

  void value_set(const char *name, int name_length, const char *value, int value_length);
  void value_set_int(const char *name, int name_length, int32_t value);
  void value_set_uint(const char *name, int name_length, uint32_t value);
  void value_set_int64(const char *name, int name_length, int64_t value);
  void value_set_date(const char *name, int name_length, time_t value);
  // MIME standard separator ',' is used as the default value
  // Other separators (e.g. ';' in Set-cookie/Cookie) are also possible
  void value_append(const char *name, int name_length, const char *value, int value_length, bool prepend_comma = false,
                    const char separator = ',');

  void field_value_set(MIMEField *field, const char *value, int value_length);
  void field_value_set_int(MIMEField *field, int32_t value);
  void field_value_set_uint(MIMEField *field, uint32_t value);
  void field_value_set_int64(MIMEField *field, int64_t value);
  void field_value_set_date(MIMEField *field, time_t value);
  // MIME standard separator ',' is used as the default value
  // Other separators (e.g. ';' in Set-cookie/Cookie) are also possible
  void field_value_append(MIMEField *field, const char *value, int value_length, bool prepend_comma = false,
                          const char separator = ',');
  void value_append_or_set(const char *name, const int name_length, char *value, int value_length);
  void field_combine_dups(MIMEField *field, bool prepend_comma = false, const char separator = ',');
  time_t get_age();
  int64_t get_content_length() const;
  time_t get_date();
  time_t get_expires();
  time_t get_if_modified_since();
  time_t get_if_unmodified_since();
  time_t get_last_modified();
  time_t get_if_range_date();
  int32_t get_max_forwards();
  int32_t get_warning(int idx = 0);

  uint32_t get_cooked_cc_mask();
  int32_t get_cooked_cc_max_age();
  int32_t get_cooked_cc_s_maxage();
  int32_t get_cooked_cc_max_stale();
  int32_t get_cooked_cc_min_fresh();
  bool get_cooked_pragma_no_cache();

  /** Get the value of the host field.
      This parses the host field for brackets and port value.
      @return The mime HOST field if it has a value, @c NULL otherwise.
  */
  MIMEField *get_host_port_values(const char **host_ptr, ///< [out] Pointer to host.
                                  int *host_len,         ///< [out] Length of host.
                                  const char **port_ptr, ///< [out] Pointer to port.
                                  int *port_len          ///< [out] Length of port.
  );

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
  void set_server(const char *server_id_tag, int server_id_tag_size);

  // No gratuitous copies & refcounts!
  MIMEHdr(const MIMEHdr &m) = delete;
  MIMEHdr &operator=(const MIMEHdr &m) = delete;
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
MIMEHdr::length_get()
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
MIMEHdr::fields_count()
{
  return mime_hdr_fields_count(m_mime);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline MIMEField *
MIMEHdr::field_create(const char *name, int length)
{
  MIMEField *field = mime_field_create(m_heap, m_mime);

  if (name) {
    int field_name_wks_idx = hdrtoken_tokenize(name, length);
    mime_field_name_set(m_heap, m_mime, field, field_name_wks_idx, name, length, true);
  }

  return (field);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline MIMEField *
MIMEHdr::field_find(const char *name, int length)
{
  //    ink_assert(valid());
  return mime_hdr_field_find(m_mime, name, length);
}

inline const MIMEField *
MIMEHdr::field_find(const char *name, int length) const
{
  //    ink_assert(valid());
  MIMEField *retval = mime_hdr_field_find(const_cast<MIMEHdr *>(this)->m_mime, name, length);
  return retval;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::field_attach(MIMEField *field)
{
  mime_hdr_field_attach(m_mime, field, 1, nullptr);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::field_detach(MIMEField *field, bool detach_all_dups)
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

inline void
MIMEHdr::field_delete(const char *name, int name_length)
{
  MIMEField *field = field_find(name, name_length);
  if (field)
    field_delete(field);
}

inline MIMEField *
MIMEHdr::iter_get_first(MIMEFieldIter *iter)
{
  iter->m_block = &m_mime->m_first_fblock;
  iter->m_slot  = 0;
  return iter_get(iter);
}

inline MIMEField *
MIMEHdr::iter_get(MIMEFieldIter *iter)
{
  MIMEField *f;
  MIMEFieldBlockImpl *b = iter->m_block;

  int slot = iter->m_slot;

  while (b) {
    for (; slot < (int)b->m_freetop; slot++) {
      f = &(b->m_field_slots[slot]);
      if (f->is_live()) {
        iter->m_slot  = slot;
        iter->m_block = b;
        return f;
      }
    }
    b    = b->m_next;
    slot = 0;
  }

  iter->m_block = nullptr;
  return nullptr;
}

inline MIMEField *
MIMEHdr::iter_get_next(MIMEFieldIter *iter)
{
  iter->m_slot++;
  return iter_get(iter);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline uint64_t
MIMEHdr::presence(uint64_t mask)
{
  return (m_mime->m_presence_bits & mask);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
MIMEHdr::print(char *buf, int bufsize, int *bufindex, int *chars_to_skip)
{
  return mime_hdr_print(m_heap, m_mime, buf, bufsize, bufindex, chars_to_skip);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
MIMEHdr::parse(MIMEParser *parser, const char **start, const char *end, bool must_copy_strs, bool eof)
{
  if (!m_heap)
    m_heap = new_HdrHeap();

  if (!m_mime)
    m_mime = mime_hdr_create(m_heap);

  return mime_parser_parse(parser, m_heap, m_mime, start, end, must_copy_strs, eof);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
inline int
MIMEHdr::value_get_index(const char *name, int name_length, const char *value, int value_length) const
{
  const MIMEField *field = field_find(name, name_length);
  if (field)
    return field->value_get_index(value, value_length);
  else
    return -1;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
MIMEHdr::value_get(const char *name, int name_length, int *value_length_return) const
{
  //    ink_assert(valid());
  const MIMEField *field = field_find(name, name_length);

  if (field)
    return (mime_field_value_get(field, value_length_return));
  else
    return (nullptr);
}

inline int32_t
MIMEHdr::value_get_int(const char *name, int name_length) const
{
  const MIMEField *field = field_find(name, name_length);

  if (field)
    return (mime_field_value_get_int(field));
  else
    return (0);
}

inline uint32_t
MIMEHdr::value_get_uint(const char *name, int name_length) const
{
  const MIMEField *field = field_find(name, name_length);

  if (field)
    return (mime_field_value_get_uint(field));
  else
    return (0);
}

inline int64_t
MIMEHdr::value_get_int64(const char *name, int name_length) const
{
  const MIMEField *field = field_find(name, name_length);

  if (field)
    return (mime_field_value_get_int64(field));
  else
    return (0);
}

inline time_t
MIMEHdr::value_get_date(const char *name, int name_length) const
{
  const MIMEField *field = field_find(name, name_length);

  if (field)
    return (mime_field_value_get_date(field));
  else
    return (0);
}

inline int
MIMEHdr::value_get_comma_list(const char *name, int name_length, StrList *list) const
{
  const MIMEField *field = field_find(name, name_length);

  if (field)
    return (field->value_get_comma_list(list));
  else
    return (0);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::field_value_set(MIMEField *field, const char *value, int value_length)
{
  field->value_set(m_heap, m_mime, value, value_length);
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
MIMEHdr::field_value_append(MIMEField *field, const char *value_str, int value_len, bool prepend_comma, const char separator)
{
  field->value_append(m_heap, m_mime, value_str, value_len, prepend_comma, separator);
}

inline void
MIMEHdr::field_combine_dups(MIMEField *field, bool prepend_comma, const char separator)
{
  MIMEField *current = field->m_next_dup;

  while (current) {
    int value_len         = 0;
    const char *value_str = current->value_get(&value_len);

    if (value_len > 0) {
      HdrHeap::HeapGuard guard(m_heap, value_str); // reference count the source string so it doesn't get moved
      field->value_append(m_heap, m_mime, value_str, value_len, prepend_comma, separator);
    }
    field_delete(current, false); // don't delete duplicates
    current = field->m_next_dup;
  }
}

inline void
MIMEHdr::value_append_or_set(const char *name, const int name_length, char *value, int value_length)
{
  MIMEField *field = nullptr;

  if ((field = field_find(name, name_length)) != nullptr) {
    while (field->m_next_dup) {
      field = field->m_next_dup;
    }
    field_value_append(field, value, value_length, true);
  } else {
    value_set(name, name_length, value, value_length);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::value_set(const char *name, int name_length, const char *value, int value_length)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name, name_length);
  field->value_set(m_heap, m_mime, value, value_length);
}

inline void
MIMEHdr::value_set_int(const char *name, int name_length, int32_t value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name, name_length);
  field->value_set_int(m_heap, m_mime, value);
}

inline void
MIMEHdr::value_set_uint(const char *name, int name_length, uint32_t value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name, name_length);
  field->value_set_uint(m_heap, m_mime, value);
}

inline void
MIMEHdr::value_set_int64(const char *name, int name_length, int64_t value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name, name_length);
  field->value_set_int64(m_heap, m_mime, value);
}

inline void
MIMEHdr::value_set_date(const char *name, int name_length, time_t value)
{
  MIMEField *field;
  field = mime_hdr_prepare_for_value_set(m_heap, m_mime, name, name_length);
  field->value_set_date(m_heap, m_mime, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::value_append(const char *name, int name_length, const char *value, int value_length, bool prepend_comma,
                      const char separator)
{
  MIMEField *field;

  field = field_find(name, name_length);
  if (field) {
    while (field->m_next_dup)
      field = field->m_next_dup;
    field->value_append(m_heap, m_mime, value, value_length, prepend_comma, separator);
  } else {
    field = field_create(name, name_length);
    field_attach(field);
    field->value_set(m_heap, m_mime, value, value_length);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
inline time_t
MIMEHdr::get_age()
{
  int64_t age = value_get_int64(MIME_FIELD_AGE, MIME_LEN_AGE);

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
  return (value_get_int64(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_date()
{
  return (value_get_date(MIME_FIELD_DATE, MIME_LEN_DATE));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_expires()
{
  return (value_get_date(MIME_FIELD_EXPIRES, MIME_LEN_EXPIRES));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_if_modified_since()
{
  return (value_get_date(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_if_unmodified_since()
{
  return (value_get_date(MIME_FIELD_IF_UNMODIFIED_SINCE, MIME_LEN_IF_UNMODIFIED_SINCE));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_last_modified()
{
  return (value_get_date(MIME_FIELD_LAST_MODIFIED, MIME_LEN_LAST_MODIFIED));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline time_t
MIMEHdr::get_if_range_date()
{
  return (value_get_date(MIME_FIELD_IF_RANGE, MIME_LEN_IF_RANGE));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_max_forwards()
{
  return (value_get_int(MIME_FIELD_MAX_FORWARDS, MIME_LEN_MAX_FORWARDS));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_warning(int idx)
{
  (void)idx;
  // FIXME: what do we do here?
  ink_release_assert(!"unimplemented");
  return (0);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline uint32_t
MIMEHdr::get_cooked_cc_mask()
{
  return (m_mime->m_cooked_stuff.m_cache_control.m_mask);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_cooked_cc_max_age()
{
  return (m_mime->m_cooked_stuff.m_cache_control.m_secs_max_age);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_cooked_cc_s_maxage()
{
  return (m_mime->m_cooked_stuff.m_cache_control.m_secs_s_maxage);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_cooked_cc_max_stale()
{
  return (m_mime->m_cooked_stuff.m_cache_control.m_secs_max_stale);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int32_t
MIMEHdr::get_cooked_cc_min_fresh()
{
  return (m_mime->m_cooked_stuff.m_cache_control.m_secs_min_fresh);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
MIMEHdr::get_cooked_pragma_no_cache()
{
  return (m_mime->m_cooked_stuff.m_pragma.m_no_cache);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_cooked_cc_need_revalidate_once()
{
  m_mime->m_cooked_stuff.m_cache_control.m_mask |= MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::unset_cooked_cc_need_revalidate_once()
{
  m_mime->m_cooked_stuff.m_cache_control.m_mask &= ~((uint32_t)MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_age(time_t value)
{
  if (value < 0)
    value_set_uint(MIME_FIELD_AGE, MIME_LEN_AGE, (uint32_t)INT_MAX + 1);
  else {
    if (sizeof(time_t) > 4) {
      value_set_int64(MIME_FIELD_AGE, MIME_LEN_AGE, value);
    } else {
      value_set_uint(MIME_FIELD_AGE, MIME_LEN_AGE, value);
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_content_length(int64_t value)
{
  value_set_int64(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_date(time_t value)
{
  value_set_date(MIME_FIELD_DATE, MIME_LEN_DATE, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_expires(time_t value)
{
  value_set_date(MIME_FIELD_EXPIRES, MIME_LEN_EXPIRES, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_if_modified_since(time_t value)
{
  value_set_date(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_if_unmodified_since(time_t value)
{
  value_set_date(MIME_FIELD_IF_UNMODIFIED_SINCE, MIME_LEN_IF_UNMODIFIED_SINCE, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_last_modified(time_t value)
{
  value_set_date(MIME_FIELD_LAST_MODIFIED, MIME_LEN_LAST_MODIFIED, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_max_forwards(int32_t value)
{
  value_set_int(MIME_FIELD_MAX_FORWARDS, MIME_LEN_MAX_FORWARDS, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_warning(int32_t value)
{
  value_set_int(MIME_FIELD_WARNING, MIME_LEN_WARNING, value);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
MIMEHdr::set_server(const char *server_id_tag, int server_id_tag_size)
{
  value_set(MIME_FIELD_SERVER, MIME_LEN_SERVER, server_id_tag, server_id_tag_size);
}
