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

#include "tscore/ink_platform.h"
#include "tscore/HashFNV.h"
#include "tscore/Diags.h"
#include "tscore/ink_memory.h"
#include <cstdio>
#include "tscore/Allocator.h"
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HdrToken.h"
#include "proxy/hdrs/MIME.h"
#include "tsutil/Regex.h"
#include "proxy/hdrs/URL.h"

namespace
{
DbgCtl dbg_ctl_hdr_token{"hdr_token"};

/*
 WARNING:  Indexes into this array are stored on disk for cached objects.  New strings must be added at the end of the array to
 avoid changing the indexes of pre-existing entries, unless the cache format version number is increased.

 You want a regexp like 'Accept' after "greedier" choices so it doesn't match 'Accept-Ranges' earlier than
 it should. The regexp are anchored (^Accept), but I dont see a way with the current system to
 match the word ONLY without making _hdrtoken_strs a real PCRE, but then that breaks the hashing
 hdrtoken_hash("^Accept$") != hdrtoken_hash("Accept")

 So, the current hack is to have "Accept" follow "Accept-.*", lame, I know

  /ericb


*/

const char *const _hdrtoken_strs[] = {
  // MIME Field names
  "Accept-Charset", "Accept-Encoding", "Accept-Language", "Accept-Ranges", "Accept", "Age", "Allow",
  "Approved", // NNTP
  "Authorization",
  "Bytes", // NNTP
  "Cache-Control", "Client-ip", "Connection", "Content-Base", "Content-Encoding", "Content-Language", "Content-Length",
  "Content-Location", "Content-MD5", "Content-Range", "Content-Type",
  "Control", // NNTP
  "Cookie", "Date",
  "Distribution", // NNTP
  "Etag", "Expect", "Expires",
  "Followup-To", // NNTP
  "From", "Host", "If-Match", "If-Modified-Since", "If-None-Match", "If-Range", "If-Unmodified-Since", "Keep-Alive",
  "Keywords", // NNTP
  "Last-Modified",
  "Lines", // NNTP
  "Location", "Max-Forwards",
  "Message-ID", // NNTP
  "MIME-Version",
  "Newsgroups",   // NNTP
  "Organization", // NNTP
  "Path",         // NNTP
  "Pragma", "Proxy-Authenticate", "Proxy-Authorization", "Proxy-Connection", "Public", "Range",
  "References", // NNTP
  "Referer",
  "Reply-To", // NNTP
  "Retry-After",
  "Sender", // NNTP
  "Server", "Set-Cookie",
  "Subject", // NNTP
  "Summary", // NNTP
  "Transfer-Encoding", "Upgrade", "User-Agent", "Vary", "Via", "Warning", "Www-Authenticate",
  "Xref",          // NNTP
  "@Ats-Internal", // Internal Hack

  // Accept-Encoding
  "compress", "deflate", "gzip", "identity",

  // Cache-Control flags
  "max-age", "max-stale", "min-fresh", "must-revalidate", "no-cache", "no-store", "no-transform", "only-if-cached", "private",
  "proxy-revalidate", "s-maxage", "need-revalidate-once",

  // HTTP miscellaneous
  "none", "chunked", "close",

  // WS
  "websocket", "Sec-WebSocket-Key", "Sec-WebSocket-Version",

  // HTTP/2 cleartext
  MIME_UPGRADE_H2C_TOKEN, "HTTP2-Settings",

  // URL schemes
  "file", "ftp", "gopher", "https", "http", "mailto", "news", "nntp", "prospero", "telnet", "tunnel", "wais", "pnm", "rtspu",
  "rtsp", "mmsu", "mmst", "mms", "wss", "ws",

  // HTTP methods
  "CONNECT", "DELETE", "GET", "POST", "HEAD", "OPTIONS", "PURGE", "PUT", "TRACE", "PUSH",

  // Header extensions
  "X-ID", "X-Forwarded-For", "TE", "Strict-Transport-Security", "100-continue",

  // RFC-2739
  "Forwarded",

  // RFC-8470
  "Early-Data",

  // RFC-7932
  "br"};

HdrTokenTypeBinding _hdrtoken_strs_type_initializers[] = {
  {"file",                 HdrTokenType::SCHEME        },
  {"ftp",                  HdrTokenType::SCHEME        },
  {"gopher",               HdrTokenType::SCHEME        },
  {"http",                 HdrTokenType::SCHEME        },
  {"https",                HdrTokenType::SCHEME        },
  {"mailto",               HdrTokenType::SCHEME        },
  {"news",                 HdrTokenType::SCHEME        },
  {"nntp",                 HdrTokenType::SCHEME        },
  {"prospero",             HdrTokenType::SCHEME        },
  {"telnet",               HdrTokenType::SCHEME        },
  {"tunnel",               HdrTokenType::SCHEME        },
  {"wais",                 HdrTokenType::SCHEME        },
  {"pnm",                  HdrTokenType::SCHEME        },
  {"rtsp",                 HdrTokenType::SCHEME        },
  {"rtspu",                HdrTokenType::SCHEME        },
  {"mms",                  HdrTokenType::SCHEME        },
  {"mmsu",                 HdrTokenType::SCHEME        },
  {"mmst",                 HdrTokenType::SCHEME        },
  {"wss",                  HdrTokenType::SCHEME        },
  {"ws",                   HdrTokenType::SCHEME        },

  {"CONNECT",              HdrTokenType::METHOD        },
  {"DELETE",               HdrTokenType::METHOD        },
  {"GET",                  HdrTokenType::METHOD        },
  {"HEAD",                 HdrTokenType::METHOD        },
  {"OPTIONS",              HdrTokenType::METHOD        },
  {"POST",                 HdrTokenType::METHOD        },
  {"PURGE",                HdrTokenType::METHOD        },
  {"PUT",                  HdrTokenType::METHOD        },
  {"TRACE",                HdrTokenType::METHOD        },
  {"PUSH",                 HdrTokenType::METHOD        },

  {"max-age",              HdrTokenType::CACHE_CONTROL },
  {"max-stale",            HdrTokenType::CACHE_CONTROL },
  {"min-fresh",            HdrTokenType::CACHE_CONTROL },
  {"must-revalidate",      HdrTokenType::CACHE_CONTROL },
  {"no-cache",             HdrTokenType::CACHE_CONTROL },
  {"no-store",             HdrTokenType::CACHE_CONTROL },
  {"no-transform",         HdrTokenType::CACHE_CONTROL },
  {"only-if-cached",       HdrTokenType::CACHE_CONTROL },
  {"private",              HdrTokenType::CACHE_CONTROL },
  {"proxy-revalidate",     HdrTokenType::CACHE_CONTROL },
  {"public",               HdrTokenType::CACHE_CONTROL },
  {"s-maxage",             HdrTokenType::CACHE_CONTROL },
  {"need-revalidate-once", HdrTokenType::CACHE_CONTROL },

  {(char *)nullptr,        static_cast<HdrTokenType>(0)},
};

HdrTokenFieldInfo _hdrtoken_strs_field_initializers[] = {
  {"Accept",                    MIME_SLOTID_ACCEPT,              MIME_PRESENCE_ACCEPT,              (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Accept-Charset",            MIME_SLOTID_ACCEPT_CHARSET,      MIME_PRESENCE_ACCEPT_CHARSET,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                                                                                                                               },
  {"Accept-Encoding",           MIME_SLOTID_ACCEPT_ENCODING,     MIME_PRESENCE_ACCEPT_ENCODING,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                                                                                                                               },
  {"Accept-Language",           MIME_SLOTID_ACCEPT_LANGUAGE,     MIME_PRESENCE_ACCEPT_LANGUAGE,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                                                                                                                               },
  {"Accept-Ranges",             MIME_SLOTID_NONE,                MIME_PRESENCE_ACCEPT_RANGES,       (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Age",                       MIME_SLOTID_AGE,                 MIME_PRESENCE_AGE,                 HdrTokenInfoFlags::NONE                                                                },
  {"Allow",                     MIME_SLOTID_NONE,                MIME_PRESENCE_ALLOW,               (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Approved",                  MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Authorization",             MIME_SLOTID_AUTHORIZATION,       MIME_PRESENCE_AUTHORIZATION,       HdrTokenInfoFlags::NONE                                                                },
  {"Bytes",                     MIME_SLOTID_NONE,                MIME_PRESENCE_BYTES,               HdrTokenInfoFlags::NONE                                                                },
  {"Cache-Control",             MIME_SLOTID_CACHE_CONTROL,       MIME_PRESENCE_CACHE_CONTROL,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                                                                                                                               },
  {"Client-ip",                 MIME_SLOTID_CLIENT_IP,           MIME_PRESENCE_CLIENT_IP,           HdrTokenInfoFlags::NONE                                                                },
  {"Connection",                MIME_SLOTID_CONNECTION,          MIME_PRESENCE_CONNECTION,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS | HdrTokenInfoFlags::HOPBYHOP)                                                                                                 },
  {"Content-Base",              MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Content-Encoding",          MIME_SLOTID_CONTENT_ENCODING,    MIME_PRESENCE_CONTENT_ENCODING,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                                                                                                                               },
  {"Content-Language",          MIME_SLOTID_CONTENT_LANGUAGE,    MIME_PRESENCE_CONTENT_LANGUAGE,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                                                                                                                               },
  {"Content-Length",            MIME_SLOTID_CONTENT_LENGTH,      MIME_PRESENCE_CONTENT_LENGTH,      HdrTokenInfoFlags::NONE                                                                },
  {"Content-Location",          MIME_SLOTID_NONE,                MIME_PRESENCE_CONTENT_LOCATION,    HdrTokenInfoFlags::NONE                                                                },
  {"Content-MD5",               MIME_SLOTID_NONE,                MIME_PRESENCE_CONTENT_MD5,         HdrTokenInfoFlags::NONE                                                                },
  {"Content-Range",             MIME_SLOTID_NONE,                MIME_PRESENCE_CONTENT_RANGE,       HdrTokenInfoFlags::NONE                                                                },
  {"Content-Type",              MIME_SLOTID_CONTENT_TYPE,        MIME_PRESENCE_CONTENT_TYPE,        HdrTokenInfoFlags::NONE                                                                },
  {"Control",                   MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Cookie",                    MIME_SLOTID_COOKIE,              MIME_PRESENCE_COOKIE,              (HdrTokenInfoFlags::MULTVALS)                                                          },
  {"Date",                      MIME_SLOTID_DATE,                MIME_PRESENCE_DATE,                HdrTokenInfoFlags::NONE                                                                },
  {"Distribution",              MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Etag",                      MIME_SLOTID_NONE,                MIME_PRESENCE_ETAG,                HdrTokenInfoFlags::NONE                                                                },
  {"Expires",                   MIME_SLOTID_EXPIRES,             MIME_PRESENCE_EXPIRES,             HdrTokenInfoFlags::NONE                                                                },
  {"Followup-To",               MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"From",                      MIME_SLOTID_NONE,                MIME_PRESENCE_FROM,                HdrTokenInfoFlags::NONE                                                                },
  {"Host",                      MIME_SLOTID_NONE,                MIME_PRESENCE_HOST,                HdrTokenInfoFlags::NONE                                                                },
  {"If-Match",                  MIME_SLOTID_IF_MATCH,            MIME_PRESENCE_IF_MATCH,            (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"If-Modified-Since",         MIME_SLOTID_IF_MODIFIED_SINCE,   MIME_PRESENCE_IF_MODIFIED_SINCE,   HdrTokenInfoFlags::NONE                                                                },
  {"If-None-Match",             MIME_SLOTID_IF_NONE_MATCH,       MIME_PRESENCE_IF_NONE_MATCH,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                                                                                                                               },
  {"If-Range",                  MIME_SLOTID_IF_RANGE,            MIME_PRESENCE_IF_RANGE,            HdrTokenInfoFlags::NONE                                                                },
  {"If-Unmodified-Since",       MIME_SLOTID_IF_UNMODIFIED_SINCE, MIME_PRESENCE_IF_UNMODIFIED_SINCE, HdrTokenInfoFlags::NONE                                                                },
  {"Keep-Alive",                MIME_SLOTID_NONE,                MIME_PRESENCE_KEEP_ALIVE,          (HdrTokenInfoFlags::HOPBYHOP)                                                          },
  {"Keywords",                  MIME_SLOTID_NONE,                MIME_PRESENCE_KEYWORDS,            HdrTokenInfoFlags::NONE                                                                },
  {"Last-Modified",             MIME_SLOTID_LAST_MODIFIED,       MIME_PRESENCE_LAST_MODIFIED,       HdrTokenInfoFlags::NONE                                                                },
  {"Lines",                     MIME_SLOTID_NONE,                MIME_PRESENCE_LINES,               HdrTokenInfoFlags::NONE                                                                },
  {"Location",                  MIME_SLOTID_NONE,                MIME_PRESENCE_LOCATION,            (HdrTokenInfoFlags::MULTVALS)                                                          },
  {"Max-Forwards",              MIME_SLOTID_NONE,                MIME_PRESENCE_MAX_FORWARDS,        HdrTokenInfoFlags::NONE                                                                },
  {"Message-ID",                MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Newsgroups",                MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Organization",              MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Path",                      MIME_SLOTID_NONE,                MIME_PRESENCE_PATH,                HdrTokenInfoFlags::NONE                                                                },
  {"Pragma",                    MIME_SLOTID_PRAGMA,              MIME_PRESENCE_PRAGMA,              (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Proxy-Authenticate",        MIME_SLOTID_NONE,                MIME_PRESENCE_PROXY_AUTHENTICATE,
   (HdrTokenInfoFlags::HOPBYHOP | HdrTokenInfoFlags::PROXYAUTH)                                                                                                                            },
  {"Proxy-Authorization",       MIME_SLOTID_NONE,                MIME_PRESENCE_PROXY_AUTHORIZATION,
   (HdrTokenInfoFlags::HOPBYHOP | HdrTokenInfoFlags::PROXYAUTH)                                                                                                                            },
  {"Proxy-Connection",          MIME_SLOTID_PROXY_CONNECTION,    MIME_PRESENCE_PROXY_CONNECTION,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS | HdrTokenInfoFlags::HOPBYHOP)                                                                                                 },
  {"Public",                    MIME_SLOTID_NONE,                MIME_PRESENCE_PUBLIC,              (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Range",                     MIME_SLOTID_RANGE,               MIME_PRESENCE_RANGE,               (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"References",                MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Referer",                   MIME_SLOTID_NONE,                MIME_PRESENCE_REFERER,             HdrTokenInfoFlags::NONE                                                                },
  {"Reply-To",                  MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Retry-After",               MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Sender",                    MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Server",                    MIME_SLOTID_NONE,                MIME_PRESENCE_SERVER,              HdrTokenInfoFlags::NONE                                                                },
  {"Set-Cookie",                MIME_SLOTID_SET_COOKIE,          MIME_PRESENCE_SET_COOKIE,          (HdrTokenInfoFlags::MULTVALS)                                                          },
  {"Strict-Transport-Security", MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                (HdrTokenInfoFlags::MULTVALS)                                                          },
  {"Subject",                   MIME_SLOTID_NONE,                MIME_PRESENCE_SUBJECT,             HdrTokenInfoFlags::NONE                                                                },
  {"Summary",                   MIME_SLOTID_NONE,                MIME_PRESENCE_SUMMARY,             HdrTokenInfoFlags::NONE                                                                },
  {"TE",                        MIME_SLOTID_TE,                  MIME_PRESENCE_TE,                  (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS | HdrTokenInfoFlags::HOPBYHOP)},
  {"Transfer-Encoding",         MIME_SLOTID_TRANSFER_ENCODING,   MIME_PRESENCE_TRANSFER_ENCODING,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS | HdrTokenInfoFlags::HOPBYHOP)                                                                                                 },
  {"Upgrade",                   MIME_SLOTID_NONE,                MIME_PRESENCE_UPGRADE,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS | HdrTokenInfoFlags::HOPBYHOP)                                                                                                 },
  {"User-Agent",                MIME_SLOTID_USER_AGENT,          MIME_PRESENCE_USER_AGENT,          HdrTokenInfoFlags::NONE                                                                },
  {"Vary",                      MIME_SLOTID_VARY,                MIME_PRESENCE_VARY,                (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Via",                       MIME_SLOTID_VIA,                 MIME_PRESENCE_VIA,                 (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Warning",                   MIME_SLOTID_NONE,                MIME_PRESENCE_WARNING,             (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Www-Authenticate",          MIME_SLOTID_WWW_AUTHENTICATE,    MIME_PRESENCE_WWW_AUTHENTICATE,    HdrTokenInfoFlags::NONE                                                                },
  {"Xref",                      MIME_SLOTID_NONE,                MIME_PRESENCE_XREF,                HdrTokenInfoFlags::NONE                                                                },
  {"X-ID",                      MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,
   (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS | HdrTokenInfoFlags::HOPBYHOP)                                                                                                 },
  {"X-Forwarded-For",           MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Forwarded",                 MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {"Sec-WebSocket-Key",         MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {"Sec-WebSocket-Version",     MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                HdrTokenInfoFlags::NONE                                                                },
  {nullptr,                     0,                               0,                                 HdrTokenInfoFlags::NONE                                                                },
};

} // end anonymous namespace

const char *_hdrtoken_strs_heap_f = nullptr; // storage first byte
const char *_hdrtoken_strs_heap_l = nullptr; // storage last byte

int hdrtoken_num_wks = SIZEOF(_hdrtoken_strs); // # of well-known strings

const char       *hdrtoken_strs[SIZEOF(_hdrtoken_strs)];            // wks_idx -> heap ptr
int               hdrtoken_str_lengths[SIZEOF(_hdrtoken_strs)];     // wks_idx -> length
HdrTokenType      hdrtoken_str_token_types[SIZEOF(_hdrtoken_strs)]; // wks_idx -> token type
int32_t           hdrtoken_str_slotids[SIZEOF(_hdrtoken_strs)];     // wks_idx -> slot id
uint64_t          hdrtoken_str_masks[SIZEOF(_hdrtoken_strs)];       // wks_idx -> presence mask
HdrTokenInfoFlags hdrtoken_str_flags[SIZEOF(_hdrtoken_strs)];       // wks_idx -> flags

DFA *hdrtoken_strs_dfa = nullptr;

/***********************************************************************
 *                                                                     *
 *                        H A S H    T A B L E                         *
 *                                                                     *
 ***********************************************************************/

#define HDRTOKEN_HASH_TABLE_SIZE 65536

struct HdrTokenHashBucket {
  const char *wks;
  uint32_t    hash;
};

HdrTokenHashBucket hdrtoken_hash_table[HDRTOKEN_HASH_TABLE_SIZE];

/**
  basic FNV hash
**/
#define TINY_MASK(x) (((uint32_t)1 << (x)) - 1)

inline uint32_t
hash_to_slot(uint32_t hash)
{
  return ((hash >> 15) ^ hash) & TINY_MASK(15);
}

inline uint32_t
hdrtoken_hash(const unsigned char *string, unsigned int length)
{
  ATSHash32FNV1a fnv;
  fnv.update(string, length, ATSHash::nocase());
  fnv.final();
  return fnv.get();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
hdrtoken_hash_init()
{
  uint32_t i;
  int      num_collisions;

  memset(hdrtoken_hash_table, 0, sizeof(hdrtoken_hash_table));
  num_collisions = 0;

  for (i = 0; i < static_cast<int> SIZEOF(_hdrtoken_strs); i++) {
    // convert the common string to the well-known token
    unsigned const char *wks;
    int                  wks_idx =
      hdrtoken_tokenize_dfa(_hdrtoken_strs[i], static_cast<int>(strlen(_hdrtoken_strs[i])), reinterpret_cast<const char **>(&wks));
    ink_release_assert(wks_idx >= 0);

    uint32_t hash = hdrtoken_hash(wks, hdrtoken_str_lengths[wks_idx]);
    uint32_t slot = hash_to_slot(hash);

    if (hdrtoken_hash_table[slot].wks) {
      printf("ERROR: hdrtoken_hash_table[%u] collision: '%s' replacing '%s'\n", slot, reinterpret_cast<const char *>(wks),
             hdrtoken_hash_table[slot].wks);
      ++num_collisions;
    }
    hdrtoken_hash_table[slot].wks  = reinterpret_cast<const char *>(wks);
    hdrtoken_hash_table[slot].hash = hash;
  }

  if (num_collisions > 0) {
    abort();
  }
}

/***********************************************************************
 *                                                                     *
 *                 M A I N    H D R T O K E N    C O D E               *
 *                                                                     *
 ***********************************************************************/

/**
  @return returns 0 for n=0, unit*n for n <= unit
*/

static inline unsigned int
snap_up_to_multiple(unsigned int n, unsigned int unit)
{
  return ((n + (unit - 1)) / unit) * unit;
}

/**
 */
void
hdrtoken_init()
{
  static int inited = 0;

  int i;

  if (!inited) {
    inited = 1;

    hdrtoken_strs_dfa = new DFA;
    hdrtoken_strs_dfa->compile(_hdrtoken_strs, SIZEOF(_hdrtoken_strs), (RE_CASE_INSENSITIVE));

    // all the tokenized hdrtoken strings are placed in a special heap,
    // and each string is prepended with a HdrTokenHeapPrefix ---
    // this makes it easy to tell that a string is a tokenized
    // string (because its address is within the heap), and
    // makes it easy to find the length, index, flags, mask, and
    // other info from the prefix.

    int heap_size = 0;
    for (i = 0; i < static_cast<int> SIZEOF(_hdrtoken_strs); i++) {
      hdrtoken_str_lengths[i]    = static_cast<int>(strlen(_hdrtoken_strs[i]));
      int sstr_len               = snap_up_to_multiple(hdrtoken_str_lengths[i] + 1, sizeof(HdrTokenHeapPrefix));
      int packed_prefix_str_len  = sizeof(HdrTokenHeapPrefix) + sstr_len;
      heap_size                 += packed_prefix_str_len;
    }

    _hdrtoken_strs_heap_f = static_cast<const char *>(ats_malloc(heap_size));
    _hdrtoken_strs_heap_l = _hdrtoken_strs_heap_f + heap_size - 1;

    char *heap_ptr = const_cast<char *>(_hdrtoken_strs_heap_f);

    for (i = 0; i < static_cast<int> SIZEOF(_hdrtoken_strs); i++) {
      HdrTokenHeapPrefix prefix;

      memset(&prefix, 0, sizeof(HdrTokenHeapPrefix));

      prefix.wks_idx         = i;
      prefix.wks_length      = hdrtoken_str_lengths[i];
      prefix.wks_token_type  = HdrTokenType::OTHER;         // default, can override later
      prefix.wks_info.name   = nullptr;                     // default, can override later
      prefix.wks_info.slotid = MIME_SLOTID_NONE;            // default, can override later
      prefix.wks_info.mask   = TOK_64_CONST(0);             // default, can override later
      prefix.wks_info.flags  = HdrTokenInfoFlags::MULTVALS; // default, can override later

      int sstr_len = snap_up_to_multiple(hdrtoken_str_lengths[i] + 1, sizeof(HdrTokenHeapPrefix));

      *reinterpret_cast<HdrTokenHeapPrefix *>(heap_ptr)  = prefix;                     // set string prefix
      heap_ptr                                          += sizeof(HdrTokenHeapPrefix); // advance heap ptr past index
      hdrtoken_strs[i]                                   = heap_ptr;                   // record string pointer
      // coverity[secure_coding]
      ink_strlcpy(const_cast<char *>(hdrtoken_strs[i]), _hdrtoken_strs[i],
                  heap_size - sizeof(HdrTokenHeapPrefix)); // copy string into heap
      heap_ptr  += sstr_len;                               // advance heap ptr past string
      heap_size -= sstr_len;
    }

    // Set the token types for certain tokens
    for (i = 0; _hdrtoken_strs_type_initializers[i].name != nullptr; i++) {
      int                 wks_idx;
      HdrTokenHeapPrefix *prefix;

      wks_idx = hdrtoken_tokenize_dfa(_hdrtoken_strs_type_initializers[i].name,
                                      static_cast<int>(strlen(_hdrtoken_strs_type_initializers[i].name)));

      ink_assert((wks_idx >= 0) && (wks_idx < (int)SIZEOF(hdrtoken_strs)));
      // coverity[negative_returns]
      prefix                 = hdrtoken_index_to_prefix(wks_idx);
      prefix->wks_token_type = _hdrtoken_strs_type_initializers[i].type;
    }

    // Set special data for field names
    for (i = 0; _hdrtoken_strs_field_initializers[i].name != nullptr; i++) {
      int                 wks_idx;
      HdrTokenHeapPrefix *prefix;

      wks_idx = hdrtoken_tokenize_dfa(_hdrtoken_strs_field_initializers[i].name,
                                      static_cast<int>(strlen(_hdrtoken_strs_field_initializers[i].name)));

      ink_assert((wks_idx >= 0) && (wks_idx < (int)SIZEOF(hdrtoken_strs)));
      prefix                  = hdrtoken_index_to_prefix(wks_idx);
      prefix->wks_info.slotid = _hdrtoken_strs_field_initializers[i].slotid;
      prefix->wks_info.flags  = _hdrtoken_strs_field_initializers[i].flags;
      prefix->wks_info.mask   = _hdrtoken_strs_field_initializers[i].mask;
    }

    for (i = 0; i < static_cast<int> SIZEOF(_hdrtoken_strs); i++) {
      HdrTokenHeapPrefix *prefix  = hdrtoken_index_to_prefix(i);
      prefix->wks_info.name       = hdrtoken_strs[i];
      hdrtoken_str_token_types[i] = prefix->wks_token_type;  // parallel array for speed
      hdrtoken_str_slotids[i]     = prefix->wks_info.slotid; // parallel array for speed
      hdrtoken_str_masks[i]       = prefix->wks_info.mask;   // parallel array for speed
      hdrtoken_str_flags[i]       = prefix->wks_info.flags;  // parallel array for speed
    }

    hdrtoken_hash_init();
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
hdrtoken_tokenize_dfa(const char *string, int string_len, const char **wks_string_out)
{
  int wks_idx;

  wks_idx = hdrtoken_strs_dfa->match({string, static_cast<size_t>(string_len)});

  if (wks_idx < 0) {
    wks_idx = -1;
  }
  if (wks_string_out) {
    if (wks_idx >= 0) {
      *wks_string_out = hdrtoken_index_to_wks(wks_idx);
    } else {
      *wks_string_out = nullptr;
    }
  }
  // printf("hdrtoken_tokenize_dfa(%d,*s) - return %d\n",string_len,string,wks_idx);

  return wks_idx;
}

/*-------------------------------------------------------------------------
  Have to work around that methods are case insensitive while the DFA is
  case insensitive.
  -------------------------------------------------------------------------*/

int
hdrtoken_method_tokenize(const char *string, int string_len)
{
  const char *string_out;
  int         retval = -1;
  if (hdrtoken_is_wks(string)) {
    retval = hdrtoken_wks_to_index(string);
    return retval;
  }
  retval = hdrtoken_tokenize(string, string_len, &string_out);
  if (retval >= 0) {
    if (strncmp(string, string_out, string_len) != 0) {
      // Not a case match
      retval = -1;
    }
  }
  return retval;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
hdrtoken_tokenize(const char *string, int string_len, const char **wks_string_out)
{
  int                 wks_idx;
  HdrTokenHashBucket *bucket;

  ink_assert(string != nullptr);

  if (hdrtoken_is_wks(string)) {
    wks_idx = hdrtoken_wks_to_index(string);
    if (wks_string_out) {
      *wks_string_out = string;
    }
    return wks_idx;
  }

  uint32_t hash = hdrtoken_hash(reinterpret_cast<const unsigned char *>(string), static_cast<unsigned int>(string_len));
  uint32_t slot = hash_to_slot(hash);

  bucket = &(hdrtoken_hash_table[slot]);
  if ((bucket->wks != nullptr) && (bucket->hash == hash) && (hdrtoken_wks_to_length(bucket->wks) == string_len)) {
    wks_idx = hdrtoken_wks_to_index(bucket->wks);
    if (wks_string_out) {
      *wks_string_out = bucket->wks;
    }
    return wks_idx;
  }

  Dbg(dbg_ctl_hdr_token, "Did not find a WKS for '%.*s'", string_len, string);
  return -1;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
hdrtoken_string_to_wks(const char *string)
{
  const char *wks = nullptr;
  hdrtoken_tokenize(string, static_cast<int>(strlen(string)), &wks);
  return wks;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
hdrtoken_string_to_wks(const char *string, int length)
{
  const char *wks = nullptr;
  hdrtoken_tokenize(string, length, &wks);
  return wks;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

c_str_view
hdrtoken_string_to_wks_sv(const char *string)
{
  const char *wks = nullptr;
  auto        length{strlen(string)};
  hdrtoken_tokenize(string, static_cast<int>(length), &wks);
  return c_str_view{wks, length};
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

c_str_view
hdrtoken_string_to_wks_sv(const char *string, int length)
{
  const char *wks = nullptr;
  hdrtoken_tokenize(string, length, &wks);
  return c_str_view{wks, static_cast<c_str_view::size_type>(length)};
}
