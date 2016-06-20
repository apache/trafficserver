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

#include "ts/ink_platform.h"
#include "ts/HashFNV.h"
#include "ts/Diags.h"
#include "ts/ink_memory.h"
#include <stdio.h>
#include "ts/Allocator.h"
#include "HTTP.h"
#include "HdrToken.h"
#include "MIME.h"
#include "ts/Regex.h"
#include "URL.h"

/*
 You SHOULD add to _hdrtoken_commonly_tokenized_strs, with the same ordering
 ** important, ordering matters **

 You want a regexp like 'Accept' after "greedier" choices so it doesn't match 'Accept-Ranges' earlier than
 it should. The regexp are anchored (^Accept), but I dont see a way with the current system to
 match the word ONLY without making _hdrtoken_strs a real PCRE, but then that breaks the hashing
 hdrtoken_hash("^Accept$") != hdrtoken_hash("Accept")

 So, the current hack is to have "Accept" follow "Accept-.*", lame, I know

  /ericb
*/

static const char *_hdrtoken_strs[] = {
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
  "CONNECT", "DELETE", "GET", "POST", "HEAD", "ICP_QUERY", "OPTIONS", "PURGE", "PUT", "TRACE", "PUSH",

  // Header extensions
  "X-ID", "X-Forwarded-For", "TE", "Strict-Transport-Security", "100-continue"};

static HdrTokenTypeBinding _hdrtoken_strs_type_initializers[] = {{"file", HDRTOKEN_TYPE_SCHEME},
                                                                 {"ftp", HDRTOKEN_TYPE_SCHEME},
                                                                 {"gopher", HDRTOKEN_TYPE_SCHEME},
                                                                 {"http", HDRTOKEN_TYPE_SCHEME},
                                                                 {"https", HDRTOKEN_TYPE_SCHEME},
                                                                 {"mailto", HDRTOKEN_TYPE_SCHEME},
                                                                 {"news", HDRTOKEN_TYPE_SCHEME},
                                                                 {"nntp", HDRTOKEN_TYPE_SCHEME},
                                                                 {"prospero", HDRTOKEN_TYPE_SCHEME},
                                                                 {"telnet", HDRTOKEN_TYPE_SCHEME},
                                                                 {"tunnel", HDRTOKEN_TYPE_SCHEME},
                                                                 {"wais", HDRTOKEN_TYPE_SCHEME},
                                                                 {"pnm", HDRTOKEN_TYPE_SCHEME},
                                                                 {"rtsp", HDRTOKEN_TYPE_SCHEME},
                                                                 {"rtspu", HDRTOKEN_TYPE_SCHEME},
                                                                 {"mms", HDRTOKEN_TYPE_SCHEME},
                                                                 {"mmsu", HDRTOKEN_TYPE_SCHEME},
                                                                 {"mmst", HDRTOKEN_TYPE_SCHEME},
                                                                 {"wss", HDRTOKEN_TYPE_SCHEME},
                                                                 {"ws", HDRTOKEN_TYPE_SCHEME},

                                                                 {"CONNECT", HDRTOKEN_TYPE_METHOD},
                                                                 {"DELETE", HDRTOKEN_TYPE_METHOD},
                                                                 {"GET", HDRTOKEN_TYPE_METHOD},
                                                                 {"HEAD", HDRTOKEN_TYPE_METHOD},
                                                                 {"ICP_QUERY", HDRTOKEN_TYPE_METHOD},
                                                                 {"OPTIONS", HDRTOKEN_TYPE_METHOD},
                                                                 {"POST", HDRTOKEN_TYPE_METHOD},
                                                                 {"PURGE", HDRTOKEN_TYPE_METHOD},
                                                                 {"PUT", HDRTOKEN_TYPE_METHOD},
                                                                 {"TRACE", HDRTOKEN_TYPE_METHOD},
                                                                 {"PUSH", HDRTOKEN_TYPE_METHOD},

                                                                 {"max-age", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"max-stale", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"min-fresh", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"must-revalidate", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"no-cache", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"no-store", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"no-transform", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"only-if-cached", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"private", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"proxy-revalidate", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"public", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"s-maxage", HDRTOKEN_TYPE_CACHE_CONTROL},
                                                                 {"need-revalidate-once", HDRTOKEN_TYPE_CACHE_CONTROL},

                                                                 {(char *)NULL, (HdrTokenType)0}};

static HdrTokenFieldInfo _hdrtoken_strs_field_initializers[] = {
  {"Accept", MIME_SLOTID_ACCEPT, MIME_PRESENCE_ACCEPT, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Accept-Charset", MIME_SLOTID_ACCEPT_CHARSET, MIME_PRESENCE_ACCEPT_CHARSET, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Accept-Encoding", MIME_SLOTID_ACCEPT_ENCODING, MIME_PRESENCE_ACCEPT_ENCODING, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Accept-Language", MIME_SLOTID_ACCEPT_LANGUAGE, MIME_PRESENCE_ACCEPT_LANGUAGE, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Accept-Ranges", MIME_SLOTID_NONE, MIME_PRESENCE_ACCEPT_RANGES, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Age", MIME_SLOTID_AGE, MIME_PRESENCE_AGE, HTIF_NONE},
  {"Allow", MIME_SLOTID_NONE, MIME_PRESENCE_ALLOW, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Approved", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Authorization", MIME_SLOTID_AUTHORIZATION, MIME_PRESENCE_AUTHORIZATION, HTIF_NONE},
  {"Bytes", MIME_SLOTID_NONE, MIME_PRESENCE_BYTES, HTIF_NONE},
  {"Cache-Control", MIME_SLOTID_CACHE_CONTROL, MIME_PRESENCE_CACHE_CONTROL, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Client-ip", MIME_SLOTID_CLIENT_IP, MIME_PRESENCE_CLIENT_IP, HTIF_NONE},
  {"Connection", MIME_SLOTID_CONNECTION, MIME_PRESENCE_CONNECTION, (HTIF_COMMAS | HTIF_MULTVALS | HTIF_HOPBYHOP)},
  {"Content-Base", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Content-Encoding", MIME_SLOTID_CONTENT_ENCODING, MIME_PRESENCE_CONTENT_ENCODING, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Content-Language", MIME_SLOTID_CONTENT_LANGUAGE, MIME_PRESENCE_CONTENT_LANGUAGE, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Content-Length", MIME_SLOTID_CONTENT_LENGTH, MIME_PRESENCE_CONTENT_LENGTH, HTIF_NONE},
  {"Content-Location", MIME_SLOTID_NONE, MIME_PRESENCE_CONTENT_LOCATION, HTIF_NONE},
  {"Content-MD5", MIME_SLOTID_NONE, MIME_PRESENCE_CONTENT_MD5, HTIF_NONE},
  {"Content-Range", MIME_SLOTID_NONE, MIME_PRESENCE_CONTENT_RANGE, HTIF_NONE},
  {"Content-Type", MIME_SLOTID_CONTENT_TYPE, MIME_PRESENCE_CONTENT_TYPE, HTIF_NONE},
  {"Control", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Cookie", MIME_SLOTID_COOKIE, MIME_PRESENCE_COOKIE, (HTIF_MULTVALS)},
  {"Date", MIME_SLOTID_DATE, MIME_PRESENCE_DATE, HTIF_NONE},
  {"Distribution", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Etag", MIME_SLOTID_NONE, MIME_PRESENCE_ETAG, HTIF_NONE},
  {"Expires", MIME_SLOTID_EXPIRES, MIME_PRESENCE_EXPIRES, HTIF_NONE},
  {"Followup-To", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"From", MIME_SLOTID_NONE, MIME_PRESENCE_FROM, HTIF_NONE},
  {"Host", MIME_SLOTID_NONE, MIME_PRESENCE_HOST, HTIF_NONE},
  {"If-Match", MIME_SLOTID_IF_MATCH, MIME_PRESENCE_IF_MATCH, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"If-Modified-Since", MIME_SLOTID_IF_MODIFIED_SINCE, MIME_PRESENCE_IF_MODIFIED_SINCE, HTIF_NONE},
  {"If-None-Match", MIME_SLOTID_IF_NONE_MATCH, MIME_PRESENCE_IF_NONE_MATCH, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"If-Range", MIME_SLOTID_IF_RANGE, MIME_PRESENCE_IF_RANGE, HTIF_NONE},
  {"If-Unmodified-Since", MIME_SLOTID_IF_UNMODIFIED_SINCE, MIME_PRESENCE_IF_UNMODIFIED_SINCE, HTIF_NONE},
  {"Keep-Alive", MIME_SLOTID_NONE, MIME_PRESENCE_KEEP_ALIVE, (HTIF_HOPBYHOP)},
  {"Keywords", MIME_SLOTID_NONE, MIME_PRESENCE_KEYWORDS, HTIF_NONE},
  {"Last-Modified", MIME_SLOTID_LAST_MODIFIED, MIME_PRESENCE_LAST_MODIFIED, HTIF_NONE},
  {"Lines", MIME_SLOTID_NONE, MIME_PRESENCE_LINES, HTIF_NONE},
  {"Location", MIME_SLOTID_NONE, MIME_PRESENCE_LOCATION, (HTIF_MULTVALS)},
  {"Max-Forwards", MIME_SLOTID_NONE, MIME_PRESENCE_MAX_FORWARDS, HTIF_NONE},
  {"Message-ID", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Newsgroups", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Organization", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Path", MIME_SLOTID_NONE, MIME_PRESENCE_PATH, HTIF_NONE},
  {"Pragma", MIME_SLOTID_PRAGMA, MIME_PRESENCE_PRAGMA, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Proxy-Authenticate", MIME_SLOTID_NONE, MIME_PRESENCE_PROXY_AUTHENTICATE, (HTIF_HOPBYHOP | HTIF_PROXYAUTH)},
  {"Proxy-Authorization", MIME_SLOTID_NONE, MIME_PRESENCE_PROXY_AUTHORIZATION, (HTIF_HOPBYHOP | HTIF_PROXYAUTH)},
  {"Proxy-Connection", MIME_SLOTID_PROXY_CONNECTION, MIME_PRESENCE_PROXY_CONNECTION, (HTIF_COMMAS | HTIF_MULTVALS | HTIF_HOPBYHOP)},
  {"Public", MIME_SLOTID_NONE, MIME_PRESENCE_PUBLIC, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Range", MIME_SLOTID_RANGE, MIME_PRESENCE_RANGE, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"References", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Referer", MIME_SLOTID_NONE, MIME_PRESENCE_REFERER, HTIF_NONE},
  {"Reply-To", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Retry-After", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Sender", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Server", MIME_SLOTID_NONE, MIME_PRESENCE_SERVER, HTIF_NONE},
  {"Set-Cookie", MIME_SLOTID_SET_COOKIE, MIME_PRESENCE_SET_COOKIE, (HTIF_MULTVALS)},
  {"Strict-Transport-Security", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, (HTIF_MULTVALS)},
  {"Subject", MIME_SLOTID_NONE, MIME_PRESENCE_SUBJECT, HTIF_NONE},
  {"Summary", MIME_SLOTID_NONE, MIME_PRESENCE_SUMMARY, HTIF_NONE},
  {"TE", MIME_SLOTID_TE, MIME_PRESENCE_TE, (HTIF_COMMAS | HTIF_MULTVALS | HTIF_HOPBYHOP)},
  {"Transfer-Encoding", MIME_SLOTID_TRANSFER_ENCODING, MIME_PRESENCE_TRANSFER_ENCODING, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Upgrade", MIME_SLOTID_NONE, MIME_PRESENCE_UPGRADE, (HTIF_COMMAS | HTIF_MULTVALS | HTIF_HOPBYHOP)},
  {"User-Agent", MIME_SLOTID_USER_AGENT, MIME_PRESENCE_USER_AGENT, HTIF_NONE},
  {"Vary", MIME_SLOTID_VARY, MIME_PRESENCE_VARY, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Via", MIME_SLOTID_VIA, MIME_PRESENCE_VIA, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Warning", MIME_SLOTID_NONE, MIME_PRESENCE_WARNING, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Www-Authenticate", MIME_SLOTID_WWW_AUTHENTICATE, MIME_PRESENCE_WWW_AUTHENTICATE, HTIF_NONE},
  {"Xref", MIME_SLOTID_NONE, MIME_PRESENCE_XREF, HTIF_NONE},
  {"X-ID", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, (HTIF_COMMAS | HTIF_MULTVALS | HTIF_HOPBYHOP)},
  {"X-Forwarded-For", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, (HTIF_COMMAS | HTIF_MULTVALS)},
  {"Sec-WebSocket-Key", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {"Sec-WebSocket-Version", MIME_SLOTID_NONE, MIME_PRESENCE_NONE, HTIF_NONE},
  {NULL, 0, 0, 0}};

const char *_hdrtoken_strs_heap_f = NULL; // storage first byte
const char *_hdrtoken_strs_heap_l = NULL; // storage last byte

int hdrtoken_num_wks = SIZEOF(_hdrtoken_strs); // # of well-known strings

const char *hdrtoken_strs[SIZEOF(_hdrtoken_strs)];             // wks_idx -> heap ptr
int hdrtoken_str_lengths[SIZEOF(_hdrtoken_strs)];              // wks_idx -> length
HdrTokenType hdrtoken_str_token_types[SIZEOF(_hdrtoken_strs)]; // wks_idx -> token type
int32_t hdrtoken_str_slotids[SIZEOF(_hdrtoken_strs)];          // wks_idx -> slot id
uint64_t hdrtoken_str_masks[SIZEOF(_hdrtoken_strs)];           // wks_idx -> presence mask
uint32_t hdrtoken_str_flags[SIZEOF(_hdrtoken_strs)];           // wks_idx -> flags

DFA *hdrtoken_strs_dfa = NULL;

/***********************************************************************
 *                                                                     *
 *                        H A S H    T A B L E                         *
 *                                                                     *
 ***********************************************************************/

#define HDRTOKEN_HASH_TABLE_SIZE 65536
#define HDRTOKEN_HASH_TABLE_MASK HDRTOKEN_HASH_TABLE_SIZE - 1

struct HdrTokenHashBucket {
  const char *wks;
  uint32_t hash;
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

static const char *_hdrtoken_commonly_tokenized_strs[] = {
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
  "CONNECT", "DELETE", "GET", "POST", "HEAD", "ICP_QUERY", "OPTIONS", "PURGE", "PUT", "TRACE", "PUSH",

  // Header extensions
  "X-ID", "X-Forwarded-For", "TE", "Strict-Transport-Security", "100-continue"};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
hdrtoken_hash_init()
{
  uint32_t i;
  int num_collisions;

  memset(hdrtoken_hash_table, 0, sizeof(hdrtoken_hash_table));
  num_collisions = 0;

  for (i = 0; i < (int)SIZEOF(_hdrtoken_commonly_tokenized_strs); i++) {
    // convert the common string to the well-known token
    unsigned const char *wks;
    int wks_idx = hdrtoken_tokenize_dfa(_hdrtoken_commonly_tokenized_strs[i], (int)strlen(_hdrtoken_commonly_tokenized_strs[i]),
                                        (const char **)&wks);
    ink_release_assert(wks_idx >= 0);

    uint32_t hash = hdrtoken_hash(wks, hdrtoken_str_lengths[wks_idx]);
    uint32_t slot = hash_to_slot(hash);

    if (hdrtoken_hash_table[slot].wks) {
      printf("ERROR: hdrtoken_hash_table[%u] collision: '%s' replacing '%s'\n", slot, (const char *)wks,
             hdrtoken_hash_table[slot].wks);
      ++num_collisions;
    }
    hdrtoken_hash_table[slot].wks  = (const char *)wks;
    hdrtoken_hash_table[slot].hash = hash;
  }

  if (num_collisions > 0)
    abort();
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
    hdrtoken_strs_dfa->compile(_hdrtoken_strs, SIZEOF(_hdrtoken_strs), (REFlags)(RE_CASE_INSENSITIVE));

    // all the tokenized hdrtoken strings are placed in a special heap,
    // and each string is prepended with a HdrTokenHeapPrefix ---
    // this makes it easy to tell that a string is a tokenized
    // string (because its address is within the heap), and
    // makes it easy to find the length, index, flags, mask, and
    // other info from the prefix.

    int heap_size = 0;
    for (i = 0; i < (int)SIZEOF(_hdrtoken_strs); i++) {
      hdrtoken_str_lengths[i]   = (int)strlen(_hdrtoken_strs[i]);
      int sstr_len              = snap_up_to_multiple(hdrtoken_str_lengths[i] + 1, sizeof(HdrTokenHeapPrefix));
      int packed_prefix_str_len = sizeof(HdrTokenHeapPrefix) + sstr_len;
      heap_size += packed_prefix_str_len;
    }

    _hdrtoken_strs_heap_f = (const char *)ats_malloc(heap_size);
    _hdrtoken_strs_heap_l = _hdrtoken_strs_heap_f + heap_size - 1;

    char *heap_ptr = (char *)_hdrtoken_strs_heap_f;

    for (i = 0; i < (int)SIZEOF(_hdrtoken_strs); i++) {
      HdrTokenHeapPrefix prefix;

      memset(&prefix, 0, sizeof(HdrTokenHeapPrefix));

      prefix.wks_idx         = i;
      prefix.wks_length      = hdrtoken_str_lengths[i];
      prefix.wks_token_type  = HDRTOKEN_TYPE_OTHER; // default, can override later
      prefix.wks_info.name   = NULL;                // default, can override later
      prefix.wks_info.slotid = MIME_SLOTID_NONE;    // default, can override later
      prefix.wks_info.mask   = TOK_64_CONST(0);     // default, can override later
      prefix.wks_info.flags  = MIME_FLAGS_MULTVALS; // default, can override later

      int sstr_len = snap_up_to_multiple(hdrtoken_str_lengths[i] + 1, sizeof(HdrTokenHeapPrefix));

      *(HdrTokenHeapPrefix *)heap_ptr = prefix; // set string prefix
      heap_ptr += sizeof(HdrTokenHeapPrefix);   // advance heap ptr past index
      hdrtoken_strs[i] = heap_ptr;              // record string pointer
      // coverity[secure_coding]
      ink_strlcpy((char *)hdrtoken_strs[i], _hdrtoken_strs[i], heap_size - sizeof(HdrTokenHeapPrefix)); // copy string into heap
      heap_ptr += sstr_len; // advance heap ptr past string
      heap_size -= sstr_len;
    }

    // Set the token types for certain tokens
    for (i = 0; _hdrtoken_strs_type_initializers[i].name != NULL; i++) {
      int wks_idx;
      HdrTokenHeapPrefix *prefix;

      wks_idx =
        hdrtoken_tokenize_dfa(_hdrtoken_strs_type_initializers[i].name, (int)strlen(_hdrtoken_strs_type_initializers[i].name));

      ink_assert((wks_idx >= 0) && (wks_idx < (int)SIZEOF(hdrtoken_strs)));
      // coverity[negative_returns]
      prefix                 = hdrtoken_index_to_prefix(wks_idx);
      prefix->wks_token_type = _hdrtoken_strs_type_initializers[i].type;
    }

    // Set special data for field names
    for (i = 0; _hdrtoken_strs_field_initializers[i].name != NULL; i++) {
      int wks_idx;
      HdrTokenHeapPrefix *prefix;

      wks_idx =
        hdrtoken_tokenize_dfa(_hdrtoken_strs_field_initializers[i].name, (int)strlen(_hdrtoken_strs_field_initializers[i].name));

      ink_assert((wks_idx >= 0) && (wks_idx < (int)SIZEOF(hdrtoken_strs)));
      prefix                  = hdrtoken_index_to_prefix(wks_idx);
      prefix->wks_info.slotid = _hdrtoken_strs_field_initializers[i].slotid;
      prefix->wks_info.flags  = _hdrtoken_strs_field_initializers[i].flags;
      prefix->wks_info.mask   = _hdrtoken_strs_field_initializers[i].mask;
    }

    for (i = 0; i < (int)SIZEOF(_hdrtoken_strs); i++) {
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

  wks_idx = hdrtoken_strs_dfa->match(string, string_len);

  if (wks_idx < 0)
    wks_idx = -1;
  if (wks_string_out) {
    if (wks_idx >= 0)
      *wks_string_out = hdrtoken_index_to_wks(wks_idx);
    else
      *wks_string_out = NULL;
  }
  // printf("hdrtoken_tokenize_dfa(%d,*s) - return %d\n",string_len,string,wks_idx);

  return wks_idx;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
hdrtoken_tokenize(const char *string, int string_len, const char **wks_string_out)
{
  int wks_idx;
  HdrTokenHashBucket *bucket;

  ink_assert(string != NULL);

  if (hdrtoken_is_wks(string)) {
    wks_idx = hdrtoken_wks_to_index(string);
    if (wks_string_out)
      *wks_string_out = string;
    return wks_idx;
  }

  uint32_t hash = hdrtoken_hash((const unsigned char *)string, (unsigned int)string_len);
  uint32_t slot = hash_to_slot(hash);

  bucket = &(hdrtoken_hash_table[slot]);
  if ((bucket->wks != NULL) && (bucket->hash == hash) && (hdrtoken_wks_to_length(bucket->wks) == string_len)) {
    wks_idx = hdrtoken_wks_to_index(bucket->wks);
    if (wks_string_out)
      *wks_string_out = bucket->wks;
    return wks_idx;
  }

  Debug("hdr_token", "Did not find a WKS for '%.*s'", string_len, string);
  return -1;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
hdrtoken_string_to_wks(const char *string)
{
  const char *wks = NULL;
  hdrtoken_tokenize(string, (int)strlen(string), &wks);
  return wks;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
hdrtoken_string_to_wks(const char *string, int length)
{
  const char *wks = NULL;
  hdrtoken_tokenize(string, length, &wks);
  return wks;
}
