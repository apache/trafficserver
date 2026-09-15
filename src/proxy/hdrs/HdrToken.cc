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
#include "tscore/Diags.h"
#include "tscore/ink_memory.h"
#include "proxy/hdrs/HdrToken.h"
#include "proxy/hdrs/MIME.h"

#include <array>
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string_view>

namespace
{
DbgCtl dbg_ctl_hdr_token{"hdr_token"};

/*
 Indexes into this array are stored inside cached objects, but they are not a format commitment:
 every reader rebuilds them from the header strings the object also stores, in
 HTTPHdrImpl::recompute_wks_indices(). Strings may therefore be added, removed, reordered or
 edited here without invalidating anyone's cache.

 What that does require is that no ATS predating the rebuild ever read an object written against a
 different table, since it would resolve the stored indexes against its own. Cache version 24.3 is
 where the rebuild landed, and an older ATS rejects anything newer than its own version, so the
 bump to 24.3 settles that for good. See CACHE_DB_MINOR_VERSION in iocore/cache/CacheDefs.h.
*/
constexpr std::string_view _hdrtoken_strs[] = {
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
  "br",

  // RFC-8878
  "zstd",

  // RFC-9213 Targeted Cache Control
  "CDN-Cache-Control"};

// MIMEField::m_wks_idx, HTTPHdrImpl's method index and URLImpl's scheme index are all int16_t.
static_assert(std::size(_hdrtoken_strs) <= INT16_MAX, "the well-known string table outgrew the type that indexes it");

constexpr HdrTokenTypeBinding _hdrtoken_strs_type_initializers[] = {
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

constexpr HdrTokenFieldInit _hdrtoken_strs_field_initializers[] = {
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
  {"CDN-Cache-Control",         MIME_SLOTID_NONE,                MIME_PRESENCE_NONE,                (HdrTokenInfoFlags::COMMAS | HdrTokenInfoFlags::MULTVALS)                              },
  {nullptr,                     0,                               0,                                 HdrTokenInfoFlags::NONE                                                                },
};

struct HdrTokenCacheControlBinding {
  const char *name;
  uint32_t    mask;
};

// The cooked mask for each Cache-Control directive. This is baked into the well-known-string
// table, so it belongs beside the other initializers rather than in MIME.cc.
constexpr HdrTokenCacheControlBinding _hdrtoken_strs_cc_initializers[] = {
  {"max-age",              MIME_COOKED_MASK_CC_MAX_AGE             },
  {"no-cache",             MIME_COOKED_MASK_CC_NO_CACHE            },
  {"no-store",             MIME_COOKED_MASK_CC_NO_STORE            },
  {"no-transform",         MIME_COOKED_MASK_CC_NO_TRANSFORM        },
  {"max-stale",            MIME_COOKED_MASK_CC_MAX_STALE           },
  {"min-fresh",            MIME_COOKED_MASK_CC_MIN_FRESH           },
  {"only-if-cached",       MIME_COOKED_MASK_CC_ONLY_IF_CACHED      },
  {"public",               MIME_COOKED_MASK_CC_PUBLIC              },
  {"private",              MIME_COOKED_MASK_CC_PRIVATE             },
  {"must-revalidate",      MIME_COOKED_MASK_CC_MUST_REVALIDATE     },
  {"proxy-revalidate",     MIME_COOKED_MASK_CC_PROXY_REVALIDATE    },
  {"s-maxage",             MIME_COOKED_MASK_CC_S_MAXAGE            },
  {"need-revalidate-once", MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE},
  {nullptr,                0                                       },
};

/***********************************************************************
 *                                                                     *
 *              C O M P I L E - T I M E    W K S    T A B L E          *
 *                                                                     *
 ***********************************************************************/

// hash_to_slot() folds a hash down to this many bits, so the table needs exactly one bucket per
// value those bits can take.
constexpr uint32_t HDRTOKEN_HASH_SLOT_BITS  = 15;
constexpr uint32_t HDRTOKEN_HASH_SLOT_MASK  = (1 << HDRTOKEN_HASH_SLOT_BITS) - 1;
constexpr size_t   HDRTOKEN_HASH_TABLE_SIZE = static_cast<size_t>(HDRTOKEN_HASH_SLOT_MASK) + 1;

constexpr uint32_t
hash_to_slot(uint32_t hash)
{
  return ((hash >> HDRTOKEN_HASH_SLOT_BITS) ^ hash) & HDRTOKEN_HASH_SLOT_MASK;
}

constexpr unsigned char
hdrtoken_ascii_toupper(unsigned char c)
{
  return (c >= 'a' && c <= 'z') ? static_cast<unsigned char>(c - ('a' - 'A')) : c;
}

constexpr uint32_t HDRTOKEN_HASH_SEED = 0x811c9dc5u; // FNV-1a 32-bit offset basis

// The one hash function, shared by compile-time table construction and hdrtoken_tokenize(), so the
// two can never disagree.
constexpr uint32_t
hdrtoken_hash(std::string_view s)
{
  uint32_t hval = HDRTOKEN_HASH_SEED;

  for (char const c : s) {
    hval = (hval ^ hdrtoken_ascii_toupper(static_cast<unsigned char>(c))) * 0x01000193u;
  }
  return hval;
}

constexpr size_t
hdrtoken_max_literal_length()
{
  const auto longest = std::max_element(std::cbegin(_hdrtoken_strs), std::cend(_hdrtoken_strs),
                                        [](std::string_view a, std::string_view b) { return a.length() < b.length(); });
  return longest->length();
}

static_assert(hdrtoken_max_literal_length() + 1 <= HDRTOKEN_WKS_STORAGE,
              "a well-known string does not fit its entry; raise HDRTOKEN_WKS_STORAGE");

constexpr bool
hdrtoken_literals_equal_nocase(std::string_view a, std::string_view b)
{
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (hdrtoken_ascii_toupper(static_cast<unsigned char>(a[i])) != hdrtoken_ascii_toupper(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

constexpr int
hdrtoken_index_of_literal(std::string_view name)
{
  for (size_t i = 0; i < std::size(_hdrtoken_strs); ++i) {
    if (hdrtoken_literals_equal_nocase(_hdrtoken_strs[i], name)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Each initializer name must be an entry in _hdrtoken_strs, and no two rows of one table may name
// the same entry.
template <typename Table>
constexpr bool
hdrtoken_names_resolve_uniquely(Table const &table)
{
  std::array<bool, std::size(_hdrtoken_strs)> seen{};

  for (auto const &row : table) {
    if (row.name == nullptr) {
      continue;
    }
    int const idx = hdrtoken_index_of_literal(row.name);

    if (idx < 0 || seen[idx]) {
      return false;
    }
    seen[idx] = true;
  }
  return true;
}

static_assert(hdrtoken_names_resolve_uniquely(_hdrtoken_strs_type_initializers),
              "a token-type initializer names a string that is missing from _hdrtoken_strs or already claimed");
static_assert(hdrtoken_names_resolve_uniquely(_hdrtoken_strs_field_initializers),
              "a field initializer names a string that is missing from _hdrtoken_strs or already claimed");
static_assert(hdrtoken_names_resolve_uniquely(_hdrtoken_strs_cc_initializers),
              "a Cache-Control initializer names a string that is missing from _hdrtoken_strs or already claimed");

// Resolution folds ASCII case and matches whole strings only. Exact-length matching is what allows
// a string to be appended to the table even when an existing entry is its prefix.
static_assert(hdrtoken_index_of_literal("cache-control") == hdrtoken_index_of_literal("Cache-Control"),
              "resolution must be ASCII case-insensitive");
static_assert(hdrtoken_index_of_literal("Content-Len") == -1, "a prefix of a well-known string must not resolve");
static_assert(hdrtoken_index_of_literal("Accept") >= 0 &&
                hdrtoken_index_of_literal("Accept") != hdrtoken_index_of_literal("Accept-Encoding"),
              "a well-known string that is a prefix of another must resolve to its own entry");

// The field rows feed the fast MIME slot and presence-bit machinery, so each non-NONE slot id must
// be a valid, unclaimed slot and each nonzero presence mask must be a distinct single bit.
constexpr bool
hdrtoken_field_init_semantics_ok()
{
  std::array<bool, 32> slot_seen{}; // MIME_SLOTID_* values are 0..31
  uint64_t             mask_seen = 0;

  for (auto const &f : _hdrtoken_strs_field_initializers) {
    if (f.name == nullptr) {
      continue;
    }
    if (f.slotid != MIME_SLOTID_NONE) {
      if (f.slotid < 0 || f.slotid >= static_cast<int32_t>(slot_seen.size()) || slot_seen[f.slotid]) {
        return false;
      }
      slot_seen[f.slotid] = true;
    }
    if (f.mask != 0) {
      if ((f.mask & (f.mask - 1)) != 0 || (mask_seen & f.mask) != 0) {
        return false;
      }
      mask_seen |= f.mask;
    }
  }
  return true;
}

static_assert(hdrtoken_field_init_semantics_ok(),
              "a field initializer has an out-of-range or duplicate slot id, or a multi-bit or duplicate presence mask");

// Every Cache-Control row must carry a distinct single-bit cooked mask, and its string must be
// typed CACHE_CONTROL to satisfy HTTPHdr::is_cache_control_set(), which asserts that type for any
// directive whose mask it consults.
constexpr bool
hdrtoken_cc_init_semantics_ok()
{
  uint32_t mask_seen = 0;

  for (auto const &c : _hdrtoken_strs_cc_initializers) {
    if (c.name == nullptr) {
      continue;
    }
    if (c.mask == 0 || (c.mask & (c.mask - 1)) != 0 || (mask_seen & c.mask) != 0) {
      return false;
    }
    mask_seen |= c.mask;

    bool cc_typed = false;

    for (auto const &b : _hdrtoken_strs_type_initializers) {
      if (b.name != nullptr && hdrtoken_literals_equal_nocase(b.name, c.name)) {
        cc_typed = (b.type == HdrTokenType::CACHE_CONTROL);
        break;
      }
    }
    if (!cc_typed) {
      return false;
    }
  }
  return true;
}

static_assert(hdrtoken_cc_init_semantics_ok(),
              "a Cache-Control initializer has a zero, multi-bit, or duplicate mask, or its entry is not typed CACHE_CONTROL");

constexpr bool
hdrtoken_wks_slots_unique()
{
  // std::sort and std::adjacent_find are only constexpr in libstdc++ 12 and later
  std::array<bool, HDRTOKEN_HASH_TABLE_SIZE> seen{};

  for (std::string_view const s : _hdrtoken_strs) {
    uint32_t const slot = hash_to_slot(hdrtoken_hash(s));

    if (seen[slot]) {
      return false;
    }
    seen[slot] = true;
  }
  return true;
}

static_assert(hdrtoken_wks_slots_unique(), "Two well-known strings hash to the same slot.  Change the table or the hash!");

constexpr std::array<HdrTokenWksEntry, std::size(_hdrtoken_strs)>
hdrtoken_build_wks_table()
{
  std::array<HdrTokenWksEntry, std::size(_hdrtoken_strs)> table{};

  for (size_t i = 0; i < std::size(_hdrtoken_strs); ++i) {
    HdrTokenWksEntry      &e    = table[i];
    std::string_view const name = _hdrtoken_strs[i];

    for (size_t k = 0; k < name.size(); ++k) {
      e.str[k] = name[k];
    }
    e.prefix.wks_idx         = static_cast<int>(i);
    e.prefix.wks_length      = static_cast<int>(name.size());
    e.prefix.wks_token_type  = HdrTokenType::OTHER;
    e.prefix.wks_info.slotid = MIME_SLOTID_NONE;
    e.prefix.wks_info.mask   = TOK_64_CONST(0);
    e.prefix.wks_info.flags  = HdrTokenInfoFlags::MULTVALS;
  }

  for (auto const &b : _hdrtoken_strs_type_initializers) {
    if (b.name != nullptr) {
      table[hdrtoken_index_of_literal(b.name)].prefix.wks_token_type = b.type;
    }
  }

  for (auto const &f : _hdrtoken_strs_field_initializers) {
    if (f.name != nullptr) {
      HdrTokenFieldInfo &info = table[hdrtoken_index_of_literal(f.name)].prefix.wks_info;

      info.slotid = f.slotid;
      info.mask   = f.mask;
      info.flags  = f.flags;
    }
  }

  for (auto const &c : _hdrtoken_strs_cc_initializers) {
    if (c.name != nullptr) {
      table[hdrtoken_index_of_literal(c.name)].prefix.wks_type_specific.u.cache_control.cc_mask = c.mask;
    }
  }

  return table;
}

constexpr std::array<HdrTokenWksEntry, std::size(_hdrtoken_strs)> hdrtoken_wks_table = hdrtoken_build_wks_table();

/***********************************************************************
 *                                                                     *
 *                        H A S H    T A B L E                         *
 *                                                                     *
 ***********************************************************************/

struct HdrTokenHashBucket {
  uint32_t wks_idx_plus_one; // biased by one so that a value-initialized bucket reads as empty
  uint32_t hash;
};

constexpr std::array<HdrTokenHashBucket, HDRTOKEN_HASH_TABLE_SIZE>
hdrtoken_build_hash_table()
{
  std::array<HdrTokenHashBucket, HDRTOKEN_HASH_TABLE_SIZE> table{};

  // static_assert(hdrtoken_wks_slots_unique()) proves no two strings share a slot, so no bucket is
  // assigned twice here.
  for (size_t i = 0; i < std::size(_hdrtoken_strs); i++) {
    uint32_t const hash = hdrtoken_hash(_hdrtoken_strs[i]);

    table[hash_to_slot(hash)] = {static_cast<uint32_t>(i) + 1, hash};
  }
  return table;
}

constexpr std::array<HdrTokenHashBucket, HDRTOKEN_HASH_TABLE_SIZE> hdrtoken_hash_table = hdrtoken_build_hash_table();

} // end anonymous namespace

// hdrtoken_wks_to_prefix() maps a string pointer back to its entry through this table.
const HdrTokenWksEntry *const hdrtoken_wks_entries = hdrtoken_wks_table.data();

// Header string pointers in this range are well-known.
const char *_hdrtoken_strs_heap_f = &hdrtoken_wks_table[0].str[0]; // storage first byte
const char *_hdrtoken_strs_heap_l = &hdrtoken_wks_table[std::size(_hdrtoken_strs) - 1].str[HDRTOKEN_WKS_STORAGE - 1];

int hdrtoken_num_wks = std::size(_hdrtoken_strs); // # of well-known strings

const char       *hdrtoken_strs[std::size(_hdrtoken_strs)];            // wks_idx -> string
int               hdrtoken_str_lengths[std::size(_hdrtoken_strs)];     // wks_idx -> length
HdrTokenType      hdrtoken_str_token_types[std::size(_hdrtoken_strs)]; // wks_idx -> token type
int32_t           hdrtoken_str_slotids[std::size(_hdrtoken_strs)];     // wks_idx -> slot id
uint64_t          hdrtoken_str_masks[std::size(_hdrtoken_strs)];       // wks_idx -> presence mask
HdrTokenInfoFlags hdrtoken_str_flags[std::size(_hdrtoken_strs)];       // wks_idx -> flags

/***********************************************************************
 *                                                                     *
 *                 M A I N    H D R T O K E N    C O D E               *
 *                                                                     *
 ***********************************************************************/

/**
 */
void
hdrtoken_init()
{
  static int inited = 0;

  if (!inited) {
    inited = 1;

    // hdrtoken_wks_table already holds every string with its prefix, resolved at compile time.
    // Copy the hot fields out into the parallel arrays.
    for (int i = 0; i < static_cast<int>(std::size(_hdrtoken_strs)); i++) {
      HdrTokenHeapPrefix const &prefix = hdrtoken_wks_table[i].prefix;

      hdrtoken_strs[i]            = hdrtoken_wks_table[i].str;
      hdrtoken_str_lengths[i]     = prefix.wks_length;
      hdrtoken_str_token_types[i] = prefix.wks_token_type;
      hdrtoken_str_slotids[i]     = prefix.wks_info.slotid;
      hdrtoken_str_masks[i]       = prefix.wks_info.mask;
      hdrtoken_str_flags[i]       = prefix.wks_info.flags;
    }
  }
}

/*-------------------------------------------------------------------------
  Have to work around that methods are case sensitive while hdrtoken_tokenize()
  is case insensitive.
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
  ink_assert(string != nullptr);

  if (hdrtoken_is_wks(string)) {
    int const wks_idx = hdrtoken_wks_to_index(string);

    if (wks_string_out) {
      *wks_string_out = string;
    }
    return wks_idx;
  }

  uint32_t const hash = hdrtoken_hash(std::string_view{string, static_cast<size_t>(string_len)});

  HdrTokenHashBucket const &bucket = hdrtoken_hash_table[hash_to_slot(hash)];

  if ((bucket.wks_idx_plus_one != 0) && (bucket.hash == hash)) {
    int const               wks_idx = static_cast<int>(bucket.wks_idx_plus_one - 1);
    HdrTokenWksEntry const &entry   = hdrtoken_wks_table[wks_idx];

    if (entry.prefix.wks_length == string_len) {
      if (wks_string_out) {
        *wks_string_out = entry.str;
      }
      return wks_idx;
    }
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
