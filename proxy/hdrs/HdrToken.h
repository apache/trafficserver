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

#include <cassert>
#include <sys/types.h>
#include "ts/ink_assert.h"
#include "ts/ink_atomic.h"
#include "ts/ink_defs.h"
#include "ts/ink_string.h"
#include "ts/Allocator.h"
#include "ts/Regex.h"
#include "ts/ink_apidefs.h"

////////////////////////////////////////////////////////////////////////////
//
//      tokenized string data
//
////////////////////////////////////////////////////////////////////////////

#define SIZEOF(x) (sizeof(x) / sizeof(x[0]))

enum HdrTokenType {
  HDRTOKEN_TYPE_OTHER         = 0,
  HDRTOKEN_TYPE_FIELD         = 1,
  HDRTOKEN_TYPE_METHOD        = 2,
  HDRTOKEN_TYPE_SCHEME        = 3,
  HDRTOKEN_TYPE_CACHE_CONTROL = 4
};

struct HdrTokenTypeBinding {
  const char *name;
  HdrTokenType type;
};

struct HdrTokenFieldInfo {
  const char *name;
  int32_t slotid;
  uint64_t mask;
  uint32_t flags;
};

struct HdrTokenTypeSpecific {
  union {
    struct {
      uint32_t cc_mask;
    } cache_control;
  } u;
};

struct HdrTokenHeapPrefix {
  int wks_idx;
  int wks_length;
  HdrTokenType wks_token_type;
  HdrTokenFieldInfo wks_info;
  HdrTokenTypeSpecific wks_type_specific;
};

enum HdrTokenInfoFlags {
  HTIF_NONE      = 0,
  HTIF_COMMAS    = 1 << 0,
  HTIF_MULTVALS  = 1 << 1,
  HTIF_HOPBYHOP  = 1 << 2,
  HTIF_PROXYAUTH = 1 << 3
};

#define MIME_FLAGS_NONE HTIF_NONE
#define MIME_FLAGS_COMMAS HTIF_COMMAS
#define MIME_FLAGS_MULTVALS HTIF_MULTVALS
#define MIME_FLAGS_HOPBYHOP HTIF_HOPBYHOP
#define MIME_FLAGS_PROXYAUTH HTIF_PROXYAUTH

extern DFA *hdrtoken_strs_dfa;
extern int hdrtoken_num_wks;

extern const char *hdrtoken_strs[];
extern int hdrtoken_str_lengths[];
extern HdrTokenType hdrtoken_str_token_types[];
extern int32_t hdrtoken_str_slotids[];
extern uint64_t hdrtoken_str_masks[];
extern uint32_t hdrtoken_str_flags[];

////////////////////////////////////////////////////////////////////////////
//
//      tokenized string functions
//
////////////////////////////////////////////////////////////////////////////

extern void hdrtoken_init();
extern int hdrtoken_tokenize_dfa(const char *string, int string_len, const char **wks_string_out = nullptr);
inkcoreapi extern int hdrtoken_tokenize(const char *string, int string_len, const char **wks_string_out = nullptr);
extern const char *hdrtoken_string_to_wks(const char *string);
extern const char *hdrtoken_string_to_wks(const char *string, int length);

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
hdrtoken_is_wks(const char *str)
{
  extern const char *_hdrtoken_strs_heap_f;
  extern const char *_hdrtoken_strs_heap_l;

  return ((str >= _hdrtoken_strs_heap_f) && (str <= _hdrtoken_strs_heap_l));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline bool
hdrtoken_is_valid_wks_idx(int wks_idx)
{
  return ((wks_idx >= 0) && (wks_idx < hdrtoken_num_wks));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline HdrTokenHeapPrefix *
hdrtoken_wks_to_prefix(const char *wks)
{
  ink_assert(hdrtoken_is_wks(wks));
  return ((HdrTokenHeapPrefix *)(wks - sizeof(HdrTokenHeapPrefix)));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline const char *
hdrtoken_index_to_wks(int wks_idx)
{
  ink_assert(hdrtoken_is_valid_wks_idx(wks_idx));
  return hdrtoken_strs[wks_idx];
}

inline int
hdrtoken_index_to_length(int wks_idx)
{
  ink_assert(hdrtoken_is_valid_wks_idx(wks_idx));
  return hdrtoken_str_lengths[wks_idx];
}

inline HdrTokenType
hdrtoken_index_to_token_type(int wks_idx)
{
  ink_assert(hdrtoken_is_valid_wks_idx(wks_idx));
  return hdrtoken_str_token_types[wks_idx];
}

inline int
hdrtoken_index_to_slotid(int wks_idx)
{
  ink_assert(hdrtoken_is_valid_wks_idx(wks_idx));
  return hdrtoken_str_slotids[wks_idx];
}

inline uint64_t
hdrtoken_index_to_mask(int wks_idx)
{
  ink_assert(hdrtoken_is_valid_wks_idx(wks_idx));
  return hdrtoken_str_masks[wks_idx];
}

inline int
hdrtoken_index_to_flags(int wks_idx)
{
  ink_assert(hdrtoken_is_valid_wks_idx(wks_idx));
  return hdrtoken_str_flags[wks_idx];
}

inline HdrTokenHeapPrefix *
hdrtoken_index_to_prefix(int wks_idx)
{
  ink_assert(hdrtoken_is_valid_wks_idx(wks_idx));
  return hdrtoken_wks_to_prefix(hdrtoken_index_to_wks(wks_idx));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
hdrtoken_wks_to_index(const char *wks)
{
  ink_assert(hdrtoken_is_wks(wks));
  return hdrtoken_wks_to_prefix(wks)->wks_idx;
}

inline int
hdrtoken_wks_to_length(const char *wks)
{
  ink_assert(hdrtoken_is_wks(wks));
  return hdrtoken_wks_to_prefix(wks)->wks_length;
}

inline int
hdrtoken_wks_to_token_type(const char *wks)
{
  ink_assert(hdrtoken_is_wks(wks));
  return hdrtoken_wks_to_prefix(wks)->wks_token_type;
}

inline int
hdrtoken_wks_to_slotid(const char *wks)
{
  ink_assert(hdrtoken_is_wks(wks));
  return hdrtoken_wks_to_prefix(wks)->wks_info.slotid;
}

inline uint64_t
hdrtoken_wks_to_mask(const char *wks)
{
  ink_assert(hdrtoken_is_wks(wks));
  HdrTokenHeapPrefix *prefix = hdrtoken_wks_to_prefix(wks);
  return prefix->wks_info.mask;
}

inline int
hdrtoken_wks_to_flags(const char *wks)
{
  ink_assert(hdrtoken_is_wks(wks));
  return hdrtoken_wks_to_prefix(wks)->wks_info.flags;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

////////////////////////////////////////////////////////////////////////////
//
//      tokenized string mime slot ids
//
//      (up to 32 of the most common headers are allowed to be placed in
//       special fast slots and contain presence bits and other info)
//
////////////////////////////////////////////////////////////////////////////

#define MIME_SLOTID_ACCEPT 0
#define MIME_SLOTID_ACCEPT_CHARSET 1
#define MIME_SLOTID_ACCEPT_ENCODING 2
#define MIME_SLOTID_ACCEPT_LANGUAGE 3
#define MIME_SLOTID_AGE 4
#define MIME_SLOTID_AUTHORIZATION 5
#define MIME_SLOTID_CACHE_CONTROL 6
#define MIME_SLOTID_CLIENT_IP 7
#define MIME_SLOTID_CONNECTION 8
#define MIME_SLOTID_CONTENT_ENCODING 9
#define MIME_SLOTID_CONTENT_LANGUAGE 10
#define MIME_SLOTID_CONTENT_LENGTH 11
#define MIME_SLOTID_CONTENT_TYPE 12
#define MIME_SLOTID_COOKIE 13
#define MIME_SLOTID_DATE 14
#define MIME_SLOTID_EXPIRES 15
#define MIME_SLOTID_IF_MATCH 16
#define MIME_SLOTID_IF_MODIFIED_SINCE 17
#define MIME_SLOTID_IF_NONE_MATCH 18
#define MIME_SLOTID_IF_RANGE 19
#define MIME_SLOTID_IF_UNMODIFIED_SINCE 20
#define MIME_SLOTID_LAST_MODIFIED 21
#define MIME_SLOTID_PRAGMA 22
#define MIME_SLOTID_PROXY_CONNECTION 23
#define MIME_SLOTID_RANGE 24
#define MIME_SLOTID_SET_COOKIE 25
#define MIME_SLOTID_TE 26
#define MIME_SLOTID_TRANSFER_ENCODING 27
#define MIME_SLOTID_USER_AGENT 28
#define MIME_SLOTID_VARY 29
#define MIME_SLOTID_VIA 30
#define MIME_SLOTID_WWW_AUTHENTICATE 31

#define MIME_SLOTID_NONE -1

////////////////////////////////////////////////////////////////////////////
//
//      tokenized string mime presence masks
//
//      (up to 64 headers get bitmasks for presence calculations)
//
////////////////////////////////////////////////////////////////////////////

// Windows insists on doing everything it's own completely
//   inmcompatible way, including integer constant subscripts.
//   It's too easy to match a subscript to a type since everything
//   won't break if the type is a different size.  Oh no, we
//   need to define the number of bits in our constants to make
//   life hard
#define TOK_64_CONST(x) x##LL

#define MIME_PRESENCE_ACCEPT (TOK_64_CONST(1) << 0)
#define MIME_PRESENCE_ACCEPT_CHARSET (TOK_64_CONST(1) << 1)
#define MIME_PRESENCE_ACCEPT_ENCODING (TOK_64_CONST(1) << 2)
#define MIME_PRESENCE_ACCEPT_LANGUAGE (TOK_64_CONST(1) << 3)
#define MIME_PRESENCE_ACCEPT_RANGES (TOK_64_CONST(1) << 4)
#define MIME_PRESENCE_AGE (TOK_64_CONST(1) << 5)
#define MIME_PRESENCE_ALLOW (TOK_64_CONST(1) << 6)
#define MIME_PRESENCE_AUTHORIZATION (TOK_64_CONST(1) << 7)
#define MIME_PRESENCE_BYTES (TOK_64_CONST(1) << 8)
#define MIME_PRESENCE_CACHE_CONTROL (TOK_64_CONST(1) << 9)
#define MIME_PRESENCE_CLIENT_IP (TOK_64_CONST(1) << 10)
#define MIME_PRESENCE_CONNECTION (TOK_64_CONST(1) << 11)
#define MIME_PRESENCE_CONTENT_ENCODING (TOK_64_CONST(1) << 12)
#define MIME_PRESENCE_CONTENT_LANGUAGE (TOK_64_CONST(1) << 13)
#define MIME_PRESENCE_CONTENT_LENGTH (TOK_64_CONST(1) << 14)
#define MIME_PRESENCE_CONTENT_LOCATION (TOK_64_CONST(1) << 15)
#define MIME_PRESENCE_CONTENT_MD5 (TOK_64_CONST(1) << 16)
#define MIME_PRESENCE_CONTENT_RANGE (TOK_64_CONST(1) << 17)
#define MIME_PRESENCE_CONTENT_TYPE (TOK_64_CONST(1) << 18)
#define MIME_PRESENCE_COOKIE (TOK_64_CONST(1) << 19)
#define MIME_PRESENCE_DATE (TOK_64_CONST(1) << 20)
#define MIME_PRESENCE_ETAG (TOK_64_CONST(1) << 21)
#define MIME_PRESENCE_EXPIRES (TOK_64_CONST(1) << 22)
#define MIME_PRESENCE_FROM (TOK_64_CONST(1) << 23)
#define MIME_PRESENCE_HOST (TOK_64_CONST(1) << 24)
#define MIME_PRESENCE_IF_MATCH (TOK_64_CONST(1) << 25)
#define MIME_PRESENCE_IF_MODIFIED_SINCE (TOK_64_CONST(1) << 26)
#define MIME_PRESENCE_IF_NONE_MATCH (TOK_64_CONST(1) << 27)
#define MIME_PRESENCE_IF_RANGE (TOK_64_CONST(1) << 28)
#define MIME_PRESENCE_IF_UNMODIFIED_SINCE (TOK_64_CONST(1) << 29)
#define MIME_PRESENCE_KEEP_ALIVE (TOK_64_CONST(1) << 30)
#define MIME_PRESENCE_KEYWORDS (TOK_64_CONST(1) << 31)
#define MIME_PRESENCE_LAST_MODIFIED (TOK_64_CONST(1) << 32)
#define MIME_PRESENCE_LINES (TOK_64_CONST(1) << 33)
#define MIME_PRESENCE_LOCATION (TOK_64_CONST(1) << 34)
#define MIME_PRESENCE_MAX_FORWARDS (TOK_64_CONST(1) << 35)
#define MIME_PRESENCE_PATH (TOK_64_CONST(1) << 36)
#define MIME_PRESENCE_PRAGMA (TOK_64_CONST(1) << 37)
#define MIME_PRESENCE_PROXY_AUTHENTICATE (TOK_64_CONST(1) << 38)
#define MIME_PRESENCE_PROXY_AUTHORIZATION (TOK_64_CONST(1) << 39)
#define MIME_PRESENCE_PROXY_CONNECTION (TOK_64_CONST(1) << 40)
#define MIME_PRESENCE_PUBLIC (TOK_64_CONST(1) << 41)
#define MIME_PRESENCE_RANGE (TOK_64_CONST(1) << 42)
#define MIME_PRESENCE_REFERER (TOK_64_CONST(1) << 43)
#define MIME_PRESENCE_SERVER (TOK_64_CONST(1) << 44)
#define MIME_PRESENCE_SET_COOKIE (TOK_64_CONST(1) << 45)
#define MIME_PRESENCE_SUBJECT (TOK_64_CONST(1) << 46)
#define MIME_PRESENCE_SUMMARY (TOK_64_CONST(1) << 47)
#define MIME_PRESENCE_TE (TOK_64_CONST(1) << 48)
#define MIME_PRESENCE_TRANSFER_ENCODING (TOK_64_CONST(1) << 49)
#define MIME_PRESENCE_UPGRADE (TOK_64_CONST(1) << 50)
#define MIME_PRESENCE_USER_AGENT (TOK_64_CONST(1) << 51)
#define MIME_PRESENCE_VARY (TOK_64_CONST(1) << 52)
#define MIME_PRESENCE_VIA (TOK_64_CONST(1) << 53)
#define MIME_PRESENCE_WARNING (TOK_64_CONST(1) << 54)
#define MIME_PRESENCE_WWW_AUTHENTICATE (TOK_64_CONST(1) << 55)

// bits 56-60 were used for a benchmark hack, but are now free to be used
// for something else
#define MIME_PRESENCE_UNUSED_1 (TOK_64_CONST(1) << 56)
#define MIME_PRESENCE_UNUSED_2 (TOK_64_CONST(1) << 57)
#define MIME_PRESENCE_UNUSED_3 (TOK_64_CONST(1) << 58)
#define MIME_PRESENCE_UNUSED_4 (TOK_64_CONST(1) << 59)
#define MIME_PRESENCE_UNUSED_5 (TOK_64_CONST(1) << 60)

#define MIME_PRESENCE_XREF (TOK_64_CONST(1) << 61)

#define MIME_PRESENCE_NONE TOK_64_CONST(0)
#define MIME_PRESENCE_ALL ~(TOK_64_CONST(0))

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

// HTTP/2 Upgrade token
#define MIME_UPGRADE_H2C_TOKEN "h2c"

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
